#if defined(__linux__)
#define _GNU_SOURCE
#elif defined(__APPLE__)
#define _DARWIN_C_SOURCE
#elif defined(__FreeBSD__)
#define __BSD_VISIBLE 1
#endif
#include "termbox.h"

#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>

#define ENTER_MOUSE_SEQ "\x1b[?1000h\x1b[?1002h\x1b[?1015h\x1b[?1006h"
#define EXIT_MOUSE_SEQ "\x1b[?1006l\x1b[?1015l\x1b[?1002l\x1b[?1000l"
#define EUNSUPPORTED_TERM -1
#define TI_MAGIC 0432
#define TI_ALT_MAGIC 542
#define TI_HEADER_LENGTH 12
#define TB_KEYS_NUM 22
#define CELL(buf, x, y) (buf)->cells[(y) * (buf)->width + (x)]
#define IS_CURSOR_HIDDEN(cx, cy) (cx == -1 || cy == -1)
#define LAST_COORD_INIT -1
#define WRITE_LITERAL(X) bytebuffer_append(&output_buffer, (X), sizeof(X) - 1)
#define WRITE_INT(X) \
	bytebuffer_append(&output_buffer, buf, convertnum((X), buf))

struct cellbuf {
	int width;
	int height;
	struct tb_cell *cells;
};

struct bytebuffer {
	char *buf;
	int len;
	int cap;
};

enum {
	T_ENTER_CA,
	T_EXIT_CA,
	T_SHOW_CURSOR,
	T_HIDE_CURSOR,
	T_CLEAR_SCREEN,
	T_SGR0,
	T_UNDERLINE,
	T_BOLD,
	T_BLINK,
	T_REVERSE,
	T_ENTER_KEYPAD,
	T_EXIT_KEYPAD,
	T_ENTER_MOUSE,
	T_EXIT_MOUSE,
	T_FUNCS_NUM,
};

static int init_term_builtin(void);
static char *read_file(const char *file);
static char *terminfo_try_path(const char *path, const char *term);
static char *load_terminfo(void);
static const char *terminfo_copy_string(char *data, int str, int table);
static int init_term(void);
static void shutdown_term(void);
static bool starts_with(const char *s1, int len, const char *s2);
static int parse_mouse_event(struct tb_event *event, const char *buf, int len);
static int parse_escape_seq(struct tb_event *event, const char *buf, int len);
static int convertnum(uint32_t num, char *buf);
static void write_cursor(int x, int y);
static void write_sgr(uint16_t fg, uint16_t bg);
static void cellbuf_init(struct cellbuf *buf, int width, int height);
static void cellbuf_resize(struct cellbuf *buf, int width, int height);
static void cellbuf_clear(struct cellbuf *buf);
static void cellbuf_free(struct cellbuf *buf);
static void get_term_size(int *w, int *h);
static void update_term_size(void);
static void send_attr(uint16_t fg, uint16_t bg);
static void send_char(int x, int y, uint32_t c);
static void send_clear(void);
static void sigwinch_handler(int xxx);
static void update_size(void);
static int read_up_to(int n);
static int wait_fill_event(struct tb_event *event, struct timeval *timeout);
static void bytebuffer_reserve(struct bytebuffer *b, int cap);
static void bytebuffer_init(struct bytebuffer *b, int cap);
static void bytebuffer_free(struct bytebuffer *b);
static void bytebuffer_clear(struct bytebuffer *b);
static void bytebuffer_append(struct bytebuffer *b, const char *data, int len);
static void bytebuffer_puts(struct bytebuffer *b, const char *str);
static void bytebuffer_resize(struct bytebuffer *b, int len);
static void bytebuffer_flush(struct bytebuffer *b, int fd);
static void bytebuffer_truncate(struct bytebuffer *b, int n);

static struct termios orig_tios;
static struct cellbuf back_buffer;
static struct cellbuf front_buffer;
static struct bytebuffer output_buffer;
static struct bytebuffer input_buffer;
static int termw = -1;
static int termh = -1;
static int inputmode = TB_INPUT_ESC;
static int outputmode = TB_OUTPUT_NORMAL;
static int inout;
static int winch_fds[2];
static int lastx = LAST_COORD_INIT;
static int lasty = LAST_COORD_INIT;
static int cursor_x = -1;
static int cursor_y = -1;
static uint16_t background = TB_DEFAULT;
static uint16_t foreground = TB_DEFAULT;
/* may happen in a different thread */
static volatile int buffer_size_change_request;
// rxvt-256color
static const char *rxvt_256color_keys[] = { "\033[11~", "\033[12~", "\033[13~",
	"\033[14~", "\033[15~", "\033[17~", "\033[18~", "\033[19~", "\033[20~",
	"\033[21~", "\033[23~", "\033[24~", "\033[2~", "\033[3~", "\033[7~",
	"\033[8~", "\033[5~", "\033[6~", "\033[A", "\033[B", "\033[D", "\033[C",
	0 };
static const char *rxvt_256color_funcs[] = {
	"\0337\033[?47h",
	"\033[2J\033[?47l\0338",
	"\033[?25h",
	"\033[?25l",
	"\033[H\033[2J",
	"\033[m",
	"\033[4m",
	"\033[1m",
	"\033[5m",
	"\033[7m",
	"\033=",
	"\033>",
	ENTER_MOUSE_SEQ,
	EXIT_MOUSE_SEQ,
};
// Eterm
static const char *eterm_keys[] = { "\033[11~", "\033[12~", "\033[13~",
	"\033[14~", "\033[15~", "\033[17~", "\033[18~", "\033[19~", "\033[20~",
	"\033[21~", "\033[23~", "\033[24~", "\033[2~", "\033[3~", "\033[7~",
	"\033[8~", "\033[5~", "\033[6~", "\033[A", "\033[B", "\033[D", "\033[C",
	0 };
static const char *eterm_funcs[] = {
	"\0337\033[?47h",
	"\033[2J\033[?47l\0338",
	"\033[?25h",
	"\033[?25l",
	"\033[H\033[2J",
	"\033[m",
	"\033[4m",
	"\033[1m",
	"\033[5m",
	"\033[7m",
	"",
	"",
	"",
	"",
};
// screen
static const char *screen_keys[] = { "\033OP", "\033OQ", "\033OR", "\033OS",
	"\033[15~", "\033[17~", "\033[18~", "\033[19~", "\033[20~", "\033[21~",
	"\033[23~", "\033[24~", "\033[2~", "\033[3~", "\033[1~", "\033[4~",
	"\033[5~", "\033[6~", "\033OA", "\033OB", "\033OD", "\033OC", 0 };
static const char *screen_funcs[] = {
	"\033[?1049h",
	"\033[?1049l",
	"\033[34h\033[?25h",
	"\033[?25l",
	"\033[H\033[J",
	"\033[m",
	"\033[4m",
	"\033[1m",
	"\033[5m",
	"\033[7m",
	"\033[?1h\033=",
	"\033[?1l\033>",
	ENTER_MOUSE_SEQ,
	EXIT_MOUSE_SEQ,
};
// rxvt-unicode
static const char *rxvt_unicode_keys[] = { "\033[11~", "\033[12~", "\033[13~",
	"\033[14~", "\033[15~", "\033[17~", "\033[18~", "\033[19~", "\033[20~",
	"\033[21~", "\033[23~", "\033[24~", "\033[2~", "\033[3~", "\033[7~",
	"\033[8~", "\033[5~", "\033[6~", "\033[A", "\033[B", "\033[D", "\033[C",
	0 };
static const char *rxvt_unicode_funcs[] = {
	"\033[?1049h",
	"\033[r\033[?1049l",
	"\033[?25h",
	"\033[?25l",
	"\033[H\033[2J",
	"\033[m\033(B",
	"\033[4m",
	"\033[1m",
	"\033[5m",
	"\033[7m",
	"\033=",
	"\033>",
	ENTER_MOUSE_SEQ,
	EXIT_MOUSE_SEQ,
};
// linux
static const char *linux_keys[] = { "\033[[A", "\033[[B", "\033[[C", "\033[[D",
	"\033[[E", "\033[17~", "\033[18~", "\033[19~", "\033[20~", "\033[21~",
	"\033[23~", "\033[24~", "\033[2~", "\033[3~", "\033[1~", "\033[4~",
	"\033[5~", "\033[6~", "\033[A", "\033[B", "\033[D", "\033[C", 0 };
static const char *linux_funcs[] = {
	"",
	"",
	"\033[?25h\033[?0c",
	"\033[?25l\033[?1c",
	"\033[H\033[J",
	"\033[0;10m",
	"\033[4m",
	"\033[1m",
	"\033[5m",
	"\033[7m",
	"",
	"",
	"",
	"",
};
// xterm
static const char *xterm_keys[] = { "\033OP", "\033OQ", "\033OR", "\033OS",
	"\033[15~", "\033[17~", "\033[18~", "\033[19~", "\033[20~", "\033[21~",
	"\033[23~", "\033[24~", "\033[2~", "\033[3~", "\033OH", "\033OF",
	"\033[5~", "\033[6~", "\033OA", "\033OB", "\033OD", "\033OC", 0 };
static const char *xterm_funcs[] = {
	"\033[?1049h",
	"\033[?1049l",
	"\033[?12l\033[?25h",
	"\033[?25l",
	"\033[H\033[2J",
	"\033(B\033[m",
	"\033[4m",
	"\033[1m",
	"\033[5m",
	"\033[7m",
	"\033[?1h\033=",
	"\033[?1l\033>",
	ENTER_MOUSE_SEQ,
	EXIT_MOUSE_SEQ,
};
static struct term {
	const char *name;
	const char **keys;
	const char **funcs;
} terms[] = {
	{ "rxvt-256color", rxvt_256color_keys, rxvt_256color_funcs },
	{ "Eterm", eterm_keys, eterm_funcs },
	{ "screen", screen_keys, screen_funcs },
	{ "rxvt-unicode", rxvt_unicode_keys, rxvt_unicode_funcs },
	{ "linux", linux_keys, linux_funcs },
	{ "xterm", xterm_keys, xterm_funcs },
	{ 0, 0, 0 },
};
static bool init_from_terminfo = false;
static const char **keys;
static const char **funcs;

static int
try_compatible(const char *term, const char *name, const char **tkeys,
	const char **tfuncs)
{
	if (strstr(term, name)) {
		keys = tkeys;
		funcs = tfuncs;
		return 0;
	}

	return EUNSUPPORTED_TERM;
}

static int
init_term_builtin(void)
{
	int i;
	const char *term = getenv("TERM");

	if (term) {
		for (i = 0; terms[i].name; i++) {
			if (!strcmp(terms[i].name, term)) {
				keys = terms[i].keys;
				funcs = terms[i].funcs;
				return 0;
			}
		}

		/* let's do some heuristic, maybe it's a compatible terminal */
		if (try_compatible(term, "xterm", xterm_keys, xterm_funcs) == 0)
			return 0;
		if (try_compatible(term, "rxvt", rxvt_unicode_keys,
			    rxvt_unicode_funcs) == 0)
			return 0;
		if (try_compatible(term, "linux", linux_keys, linux_funcs) == 0)
			return 0;
		if (try_compatible(term, "Eterm", eterm_keys, eterm_funcs) == 0)
			return 0;
		if (try_compatible(term, "screen", screen_keys, screen_funcs) ==
			0)
			return 0;
		if (try_compatible(term, "tmux", screen_keys, screen_funcs) ==
			0)
			return 0;
		/* let's assume that 'cygwin' is xterm compatible */
		if (try_compatible(term, "cygwin", xterm_keys, xterm_funcs) ==
			0)
			return 0;
	}

	return EUNSUPPORTED_TERM;
}

//----------------------------------------------------------------------
// terminfo
//----------------------------------------------------------------------

static char *
read_file(const char *file)
{
	FILE *f = fopen(file, "rb");
	if (!f)
		return 0;

	struct stat st;
	if (fstat(fileno(f), &st) != 0) {
		fclose(f);
		return 0;
	}

	char *data = malloc(st.st_size);
	if (!data) {
		fclose(f);
		return 0;
	}

	if (fread(data, 1, st.st_size, f) != (size_t)st.st_size) {
		fclose(f);
		free(data);
		return 0;
	}

	fclose(f);
	return data;
}

static char *
terminfo_try_path(const char *path, const char *term)
{
	char tmp[4096];
	snprintf(tmp, sizeof(tmp), "%s/%c/%s", path, term[0], term);
	tmp[sizeof(tmp) - 1] = '\0';
	char *data = read_file(tmp);
	if (data) {
		return data;
	}

	// fallback to darwin specific dirs structure
	snprintf(tmp, sizeof(tmp), "%s/%x/%s", path, term[0], term);
	tmp[sizeof(tmp) - 1] = '\0';
	return read_file(tmp);
}

static char *
load_terminfo(void)
{
	char tmp[4096];
	const char *term = getenv("TERM");
	if (!term) {
		return 0;
	}

	// if TERMINFO is set, no other directory should be searched
	const char *terminfo = getenv("TERMINFO");
	if (terminfo) {
		return terminfo_try_path(terminfo, term);
	}

	// next, consider ~/.terminfo
	const char *home = getenv("HOME");
	if (home) {
		snprintf(tmp, sizeof(tmp), "%s/.terminfo", home);
		tmp[sizeof(tmp) - 1] = '\0';
		char *data = terminfo_try_path(tmp, term);
		if (data)
			return data;
	}

	// next, TERMINFO_DIRS
	const char *dirs = getenv("TERMINFO_DIRS");
	if (dirs) {
		snprintf(tmp, sizeof(tmp), "%s", dirs);
		tmp[sizeof(tmp) - 1] = '\0';
		char *dir = strtok(tmp, ":");
		while (dir) {
			const char *cdir = dir;
			if (strcmp(cdir, "") == 0) {
				cdir = "/usr/share/terminfo";
			}
			char *data = terminfo_try_path(cdir, term);
			if (data)
				return data;
			dir = strtok(0, ":");
		}
	}

	// fallback to /usr/share/terminfo
	return terminfo_try_path("/usr/share/terminfo", term);
}

static const char *
terminfo_copy_string(char *data, int str, int table)
{
	const int16_t off = *(int16_t *)(data + str);
	const char *src = data + table + off;
	int len = strlen(src);
	char *dst = malloc(len + 1);
	strcpy(dst, src);
	return dst;
}

static const int16_t ti_funcs[] = {
	28,
	40,
	16,
	13,
	5,
	39,
	36,
	27,
	26,
	34,
	89,
	88,
};

static const int16_t ti_keys[] = {
	66,
	68 /* apparently not a typo; 67 is F10 for whatever reason */,
	69,
	70,
	71,
	72,
	73,
	74,
	75,
	67,
	216,
	217,
	77,
	59,
	76,
	164,
	82,
	81,
	87,
	61,
	79,
	83,
};

static int
init_term(void)
{
	int i;
	char *data = load_terminfo();
	if (!data) {
		init_from_terminfo = false;
		return init_term_builtin();
	}

	int16_t *header = (int16_t *)data;

	const int number_sec_len = header[0] == TI_ALT_MAGIC ? 4 : 2;

	if ((header[1] + header[2]) % 2) {
		// old quirk to align everything on word boundaries
		header[2] += 1;
	}

	const int str_offset = TI_HEADER_LENGTH + header[1] + header[2] +
		number_sec_len * header[3];
	const int table_offset = str_offset + 2 * header[4];

	keys = malloc(sizeof(const char *) * (TB_KEYS_NUM + 1));
	for (i = 0; i < TB_KEYS_NUM; i++) {
		keys[i] = terminfo_copy_string(
			data, str_offset + 2 * ti_keys[i], table_offset);
	}
	keys[TB_KEYS_NUM] = 0;

	funcs = malloc(sizeof(const char *) * T_FUNCS_NUM);
	// the last two entries are reserved for mouse. because the table offset is
	// not there, the two entries have to fill in manually
	for (i = 0; i < T_FUNCS_NUM - 2; i++) {
		funcs[i] = terminfo_copy_string(
			data, str_offset + 2 * ti_funcs[i], table_offset);
	}

	funcs[T_FUNCS_NUM - 2] = ENTER_MOUSE_SEQ;
	funcs[T_FUNCS_NUM - 1] = EXIT_MOUSE_SEQ;

	init_from_terminfo = true;
	free(data);
	return 0;
}

static void
shutdown_term(void)
{
	if (init_from_terminfo) {
		int i;
		for (i = 0; i < TB_KEYS_NUM; i++) {
			free((void *)keys[i]);
		}
		// the last two entries are reserved for mouse. because the table offset
		// is not there, the two entries have to fill in manually and do not
		// need to be freed.
		for (i = 0; i < T_FUNCS_NUM - 2; i++) {
			free((void *)funcs[i]);
		}
		free(keys);
		free(funcs);
	}
}

// if s1 starts with s2 returns true, else false
// len is the length of s1
// s2 should be null-terminated
static bool
starts_with(const char *s1, int len, const char *s2)
{
	int n = 0;
	while (*s2 && n < len) {
		if (*s1++ != *s2++)
			return false;
		n++;
	}
	return *s2 == 0;
}

static int
parse_mouse_event(struct tb_event *event, const char *buf, int len)
{
	if (len >= 6 && starts_with(buf, len, "\033[M")) {
		// X10 mouse encoding, the simplest one
		// \033 [ M Cb Cx Cy
		int b = buf[3] - 32;
		switch (b & 3) {
		case 0:
			if ((b & 64) != 0)
				event->key = TB_KEY_MOUSE_WHEEL_UP;
			else
				event->key = TB_KEY_MOUSE_LEFT;
			break;
		case 1:
			if ((b & 64) != 0)
				event->key = TB_KEY_MOUSE_WHEEL_DOWN;
			else
				event->key = TB_KEY_MOUSE_MIDDLE;
			break;
		case 2:
			event->key = TB_KEY_MOUSE_RIGHT;
			break;
		case 3:
			event->key = TB_KEY_MOUSE_RELEASE;
			break;
		default:
			return -6;
		}
		event->type = TB_EVENT_MOUSE; // TB_EVENT_KEY by default
		if ((b & 32) != 0)
			event->mod |= TB_MOD_MOTION;

		// the coord is 1,1 for upper left
		event->x = (uint8_t)buf[4] - 1 - 32;
		event->y = (uint8_t)buf[5] - 1 - 32;

		return 6;
	} else if (starts_with(buf, len, "\033[<") ||
		starts_with(buf, len, "\033[")) {
		// xterm 1006 extended mode or urxvt 1015 extended mode
		// xterm: \033 [ < Cb ; Cx ; Cy (M or m)
		// urxvt: \033 [ Cb ; Cx ; Cy M
		int i, mi = -1, starti = -1;
		int isM, isU, s1 = -1, s2 = -1;
		int n1 = 0, n2 = 0, n3 = 0;

		for (i = 0; i < len; i++) {
			// We search the first (s1) and the last (s2) ';'
			if (buf[i] == ';') {
				if (s1 == -1)
					s1 = i;
				s2 = i;
			}

			// We search for the first 'm' or 'M'
			if ((buf[i] == 'm' || buf[i] == 'M') && mi == -1) {
				mi = i;
				break;
			}
		}
		if (mi == -1)
			return 0;

		// whether it's a capital M or not
		isM = (buf[mi] == 'M');

		if (buf[2] == '<') {
			isU = 0;
			starti = 3;
		} else {
			isU = 1;
			starti = 2;
		}

		if (s1 == -1 || s2 == -1 || s1 == s2)
			return 0;

		n1 = strtoul(&buf[starti], NULL, 10);
		n2 = strtoul(&buf[s1 + 1], NULL, 10);
		n3 = strtoul(&buf[s2 + 1], NULL, 10);

		if (isU)
			n1 -= 32;

		switch (n1 & 3) {
		case 0:
			if ((n1 & 64) != 0) {
				event->key = TB_KEY_MOUSE_WHEEL_UP;
			} else {
				event->key = TB_KEY_MOUSE_LEFT;
			}
			break;
		case 1:
			if ((n1 & 64) != 0) {
				event->key = TB_KEY_MOUSE_WHEEL_DOWN;
			} else {
				event->key = TB_KEY_MOUSE_MIDDLE;
			}
			break;
		case 2:
			event->key = TB_KEY_MOUSE_RIGHT;
			break;
		case 3:
			event->key = TB_KEY_MOUSE_RELEASE;
			break;
		default:
			return mi + 1;
		}

		if (!isM) {
			// on xterm mouse release is signaled by lowercase m
			event->key = TB_KEY_MOUSE_RELEASE;
		}

		event->type = TB_EVENT_MOUSE; // TB_EVENT_KEY by default
		if ((n1 & 32) != 0)
			event->mod |= TB_MOD_MOTION;

		event->x = (uint8_t)n2 - 1;
		event->y = (uint8_t)n3 - 1;

		return mi + 1;
	}

	return 0;
}

// convert escape sequence to event, and return consumed bytes on success
// (failure == 0)
static int
parse_escape_seq(struct tb_event *event, const char *buf, int len)
{
	int mouse_parsed = parse_mouse_event(event, buf, len);

	if (mouse_parsed != 0)
		return mouse_parsed;

	// it's pretty simple here, find 'starts_with' match and return
	// success, else return failure
	int i;
	for (i = 0; keys[i]; i++) {
		if (starts_with(buf, len, keys[i])) {
			event->ch = 0;
			event->key = 0xFFFF - i;
			return strlen(keys[i]);
		}
	}
	return 0;
}

static bool
extract_event(struct tb_event *event, struct bytebuffer *inbuf, int in)
{
	const char *buf = inbuf->buf;
	const int len = inbuf->len;
	if (len == 0)
		return false;

	if (buf[0] == '\033') {
		int n = parse_escape_seq(event, buf, len);
		if (n != 0) {
			bool success = true;
			if (n < 0) {
				success = false;
				n = -n;
			}
			bytebuffer_truncate(inbuf, n);
			return success;
		} else {
			// it's not escape sequence, then it's ALT or ESC,
			// check inputmode
			if (in & TB_INPUT_ESC) {
				// if we're in escape mode, fill ESC event, pop
				// buffer, return success
				event->ch = 0;
				event->key = TB_KEY_ESC;
				event->mod = 0;
				bytebuffer_truncate(inbuf, 1);
				return true;
			} else if (in & TB_INPUT_ALT) {
				// if we're in alt mode, set ALT modifier to
				// event and redo parsing
				event->mod = TB_MOD_ALT;
				bytebuffer_truncate(inbuf, 1);
				return extract_event(event, inbuf, in);
			}
			assert(!"never got here");
		}
	}

	// if we're here, this is not an escape sequence and not an alt sequence
	// so, it's a FUNCTIONAL KEY or a UNICODE character

	// first of all check if it's a functional key
	if ((unsigned char)buf[0] <= TB_KEY_SPACE ||
		(unsigned char)buf[0] == TB_KEY_BACKSPACE2) {
		// fill event, pop buffer, return success */
		event->ch = 0;
		event->key = (uint16_t)buf[0];
		bytebuffer_truncate(inbuf, 1);
		return true;
	}

	// feh... we got utf8 here

	// check if there is all bytes
	if (len >= tb_utf8_char_length(buf[0])) {
		/* everything ok, fill event, pop buffer, return success */
		tb_utf8_char_to_unicode(&event->ch, buf);
		event->key = 0;
		bytebuffer_truncate(inbuf, tb_utf8_char_length(buf[0]));
		return true;
	}

	// event isn't recognized, perhaps there is not enough bytes in utf8
	// sequence
	return false;
}

/* -------------------------------------------------------- */

int
tb_init_fd(int inout_)
{
	inout = inout_;
	if (inout == -1) {
		return TB_EFAILED_TO_OPEN_TTY;
	}

	if (init_term() < 0) {
		close(inout);
		return TB_EUNSUPPORTED_TERMINAL;
	}

	if (pipe(winch_fds) < 0) {
		close(inout);
		return TB_EPIPE_TRAP_ERROR;
	}

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigwinch_handler;
	sa.sa_flags = 0;
	sigaction(SIGWINCH, &sa, 0);

	tcgetattr(inout, &orig_tios);

	struct termios tios;
	memcpy(&tios, &orig_tios, sizeof(tios));

	tios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
		ICRNL | IXON);
	tios.c_oflag &= ~OPOST;
	tios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	tios.c_cflag &= ~(CSIZE | PARENB);
	tios.c_cflag |= CS8;
	tios.c_cc[VMIN] = 0;
	tios.c_cc[VTIME] = 0;
	tcsetattr(inout, TCSAFLUSH, &tios);

	bytebuffer_init(&input_buffer, 128);
	bytebuffer_init(&output_buffer, 32 * 1024);

	bytebuffer_puts(&output_buffer, funcs[T_ENTER_CA]);
	bytebuffer_puts(&output_buffer, funcs[T_ENTER_KEYPAD]);
	bytebuffer_puts(&output_buffer, funcs[T_HIDE_CURSOR]);
	send_clear();

	update_term_size();
	cellbuf_init(&back_buffer, termw, termh);
	cellbuf_init(&front_buffer, termw, termh);
	cellbuf_clear(&back_buffer);
	cellbuf_clear(&front_buffer);

	return 0;
}

int
tb_init_file(const char *name)
{
	return tb_init_fd(open(name, O_RDWR));
}

int
tb_init(void)
{
	return tb_init_file("/dev/tty");
}

void
tb_shutdown(void)
{
	if (termw == -1) {
		fputs("tb_shutdown() should not be called twice.", stderr);
		abort();
	}

	bytebuffer_puts(&output_buffer, funcs[T_SHOW_CURSOR]);
	bytebuffer_puts(&output_buffer, funcs[T_SGR0]);
	bytebuffer_puts(&output_buffer, funcs[T_CLEAR_SCREEN]);
	bytebuffer_puts(&output_buffer, funcs[T_EXIT_CA]);
	bytebuffer_puts(&output_buffer, funcs[T_EXIT_KEYPAD]);
	bytebuffer_puts(&output_buffer, funcs[T_EXIT_MOUSE]);
	bytebuffer_flush(&output_buffer, inout);
	tcsetattr(inout, TCSAFLUSH, &orig_tios);

	shutdown_term();
	close(inout);
	close(winch_fds[0]);
	close(winch_fds[1]);

	cellbuf_free(&back_buffer);
	cellbuf_free(&front_buffer);
	bytebuffer_free(&output_buffer);
	bytebuffer_free(&input_buffer);
	termw = termh = -1;
}

void
tb_present(void)
{
	int x, y, w, i;
	struct tb_cell *back, *front;

	/* invalidate cursor position */
	lastx = LAST_COORD_INIT;
	lasty = LAST_COORD_INIT;

	if (buffer_size_change_request) {
		update_size();
		buffer_size_change_request = 0;
	}

	for (y = 0; y < front_buffer.height; ++y) {
		for (x = 0; x < front_buffer.width;) {
			back = &CELL(&back_buffer, x, y);
			front = &CELL(&front_buffer, x, y);
			w = wcwidth(back->ch);
			if (w < 1)
				w = 1;
			if (memcmp(back, front, sizeof(struct tb_cell)) == 0) {
				x += w;
				continue;
			}
			memcpy(front, back, sizeof(struct tb_cell));
			send_attr(back->fg, back->bg);
			if (w > 1 && x >= front_buffer.width - (w - 1)) {
				// Not enough room for wide ch, so send spaces
				for (i = x; i < front_buffer.width; ++i) {
					send_char(i, y, ' ');
				}
			} else {
				send_char(x, y, back->ch);
				for (i = 1; i < w; ++i) {
					front = &CELL(&front_buffer, x + i, y);
					front->ch = 0;
					front->fg = back->fg;
					front->bg = back->bg;
				}
			}
			x += w;
		}
	}
	if (!IS_CURSOR_HIDDEN(cursor_x, cursor_y))
		write_cursor(cursor_x, cursor_y);
	bytebuffer_flush(&output_buffer, inout);
}

void
tb_set_cursor(int cx, int cy)
{
	if (IS_CURSOR_HIDDEN(cursor_x, cursor_y) && !IS_CURSOR_HIDDEN(cx, cy))
		bytebuffer_puts(&output_buffer, funcs[T_SHOW_CURSOR]);

	if (!IS_CURSOR_HIDDEN(cursor_x, cursor_y) && IS_CURSOR_HIDDEN(cx, cy))
		bytebuffer_puts(&output_buffer, funcs[T_HIDE_CURSOR]);

	cursor_x = cx;
	cursor_y = cy;
	if (!IS_CURSOR_HIDDEN(cursor_x, cursor_y))
		write_cursor(cursor_x, cursor_y);
}

void
tb_put_cell(int x, int y, const struct tb_cell *cell)
{
	if ((unsigned)x >= (unsigned)back_buffer.width)
		return;
	if ((unsigned)y >= (unsigned)back_buffer.height)
		return;
	CELL(&back_buffer, x, y) = *cell;
}

void
tb_change_cell(int x, int y, uint32_t ch, uint16_t fg, uint16_t bg)
{
	struct tb_cell c = { ch, fg, bg };
	tb_put_cell(x, y, &c);
}

void
tb_blit(int x, int y, int w, int h, const struct tb_cell *cells)
{
	if (x + w < 0 || x >= back_buffer.width)
		return;
	if (y + h < 0 || y >= back_buffer.height)
		return;
	int xo = 0, yo = 0, ww = w, hh = h;
	if (x < 0) {
		xo = -x;
		ww -= xo;
		x = 0;
	}
	if (y < 0) {
		yo = -y;
		hh -= yo;
		y = 0;
	}
	if (ww > back_buffer.width - x)
		ww = back_buffer.width - x;
	if (hh > back_buffer.height - y)
		hh = back_buffer.height - y;

	int sy;
	struct tb_cell *dst = &CELL(&back_buffer, x, y);
	const struct tb_cell *src = cells + yo * w + xo;
	size_t size = sizeof(struct tb_cell) * ww;

	for (sy = 0; sy < hh; ++sy) {
		memcpy(dst, src, size);
		dst += back_buffer.width;
		src += w;
	}
}

struct tb_cell *
tb_cell_buffer(void)
{
	return back_buffer.cells;
}

int
tb_poll_event(struct tb_event *event)
{
	return wait_fill_event(event, 0);
}

int
tb_peek_event(struct tb_event *event, int timeout)
{
	struct timeval tv;
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout - (tv.tv_sec * 1000)) * 1000;
	return wait_fill_event(event, &tv);
}

int
tb_width(void)
{
	return termw;
}

int
tb_height(void)
{
	return termh;
}

void
tb_clear(void)
{
	if (buffer_size_change_request) {
		update_size();
		buffer_size_change_request = 0;
	}
	cellbuf_clear(&back_buffer);
}

int
tb_select_input_mode(int mode)
{
	if (mode) {
		if ((mode & (TB_INPUT_ESC | TB_INPUT_ALT)) == 0)
			mode |= TB_INPUT_ESC;

		/* technically termbox can handle that, but let's be nice and show here
           what mode is actually used */
		if ((mode & (TB_INPUT_ESC | TB_INPUT_ALT)) ==
			(TB_INPUT_ESC | TB_INPUT_ALT))
			mode &= ~TB_INPUT_ALT;

		inputmode = mode;
		if (mode & TB_INPUT_MOUSE) {
			bytebuffer_puts(&output_buffer, funcs[T_ENTER_MOUSE]);
			bytebuffer_flush(&output_buffer, inout);
		} else {
			bytebuffer_puts(&output_buffer, funcs[T_EXIT_MOUSE]);
			bytebuffer_flush(&output_buffer, inout);
		}
	}
	return inputmode;
}

int
tb_select_output_mode(int mode)
{
	if (mode)
		outputmode = mode;
	return outputmode;
}

void
tb_set_clear_attributes(uint16_t fg, uint16_t bg)
{
	foreground = fg;
	background = bg;
}

/* -------------------------------------------------------- */

static int
convertnum(uint32_t num, char *buf)
{
	int i, l = 0;
	int ch;
	do {
		buf[l++] = '0' + (num % 10);
		num /= 10;
	} while (num);
	for (i = 0; i < l / 2; i++) {
		ch = buf[i];
		buf[i] = buf[l - 1 - i];
		buf[l - 1 - i] = ch;
	}
	return l;
}

static void
write_cursor(int x, int y)
{
	char buf[32];
	WRITE_LITERAL("\033[");
	WRITE_INT(y + 1);
	WRITE_LITERAL(";");
	WRITE_INT(x + 1);
	WRITE_LITERAL("H");
}

static void
write_sgr(uint16_t fg, uint16_t bg)
{
	char buf[32];

	if (fg == TB_DEFAULT && bg == TB_DEFAULT)
		return;

	switch (outputmode) {
	case TB_OUTPUT_256:
	case TB_OUTPUT_216:
	case TB_OUTPUT_GRAYSCALE:
		WRITE_LITERAL("\033[");
		if (fg != TB_DEFAULT) {
			WRITE_LITERAL("38;5;");
			WRITE_INT(fg);
			if (bg != TB_DEFAULT) {
				WRITE_LITERAL(";");
			}
		}
		if (bg != TB_DEFAULT) {
			WRITE_LITERAL("48;5;");
			WRITE_INT(bg);
		}
		WRITE_LITERAL("m");
		break;
	case TB_OUTPUT_NORMAL:
	default:
		WRITE_LITERAL("\033[");
		if (fg != TB_DEFAULT) {
			WRITE_LITERAL("3");
			WRITE_INT(fg - 1);
			if (bg != TB_DEFAULT) {
				WRITE_LITERAL(";");
			}
		}
		if (bg != TB_DEFAULT) {
			WRITE_LITERAL("4");
			WRITE_INT(bg - 1);
		}
		WRITE_LITERAL("m");
		break;
	}
}

static void
cellbuf_init(struct cellbuf *buf, int width, int height)
{
	buf->cells = (struct tb_cell *)malloc(
		sizeof(struct tb_cell) * width * height);
	assert(buf->cells);
	buf->width = width;
	buf->height = height;
}

static void
cellbuf_resize(struct cellbuf *buf, int width, int height)
{
	if (buf->width == width && buf->height == height)
		return;

	int oldw = buf->width;
	int oldh = buf->height;
	struct tb_cell *oldcells = buf->cells;

	cellbuf_init(buf, width, height);
	cellbuf_clear(buf);

	int minw = (width < oldw) ? width : oldw;
	int minh = (height < oldh) ? height : oldh;
	int i;

	for (i = 0; i < minh; ++i) {
		struct tb_cell *csrc = oldcells + (i * oldw);
		struct tb_cell *cdst = buf->cells + (i * width);
		memcpy(cdst, csrc, sizeof(struct tb_cell) * minw);
	}

	free(oldcells);
}

static void
cellbuf_clear(struct cellbuf *buf)
{
	int i;
	int ncells = buf->width * buf->height;

	for (i = 0; i < ncells; ++i) {
		buf->cells[i].ch = ' ';
		buf->cells[i].fg = foreground;
		buf->cells[i].bg = background;
	}
}

static void
cellbuf_free(struct cellbuf *buf)
{
	free(buf->cells);
}

static void
get_term_size(int *w, int *h)
{
	struct winsize sz;
	memset(&sz, 0, sizeof(sz));

	ioctl(inout, TIOCGWINSZ, &sz);

	*w = sz.ws_col > 0 ? sz.ws_col : 80;
	*h = sz.ws_row > 0 ? sz.ws_row : 24;
}

static void
update_term_size(void)
{
	struct winsize sz;
	memset(&sz, 0, sizeof(sz));

	ioctl(inout, TIOCGWINSZ, &sz);

	termw = sz.ws_col > 0 ? sz.ws_col : 80;
	termh = sz.ws_row > 0 ? sz.ws_row : 24;
}

static void
send_attr(uint16_t fg, uint16_t bg)
{
#define LAST_ATTR_INIT 0xFFFF
	static uint16_t lastfg = LAST_ATTR_INIT, lastbg = LAST_ATTR_INIT;
	if (fg != lastfg || bg != lastbg) {
		bytebuffer_puts(&output_buffer, funcs[T_SGR0]);

		uint16_t fgcol;
		uint16_t bgcol;

		switch (outputmode) {
		case TB_OUTPUT_256:
			fgcol = fg & 0xFF;
			bgcol = bg & 0xFF;
			break;

		case TB_OUTPUT_216:
			fgcol = fg & 0xFF;
			if (fgcol > 215)
				fgcol = 7;
			bgcol = bg & 0xFF;
			if (bgcol > 215)
				bgcol = 0;
			fgcol += 0x10;
			bgcol += 0x10;
			break;

		case TB_OUTPUT_GRAYSCALE:
			fgcol = fg & 0xFF;
			if (fgcol > 23)
				fgcol = 23;
			bgcol = bg & 0xFF;
			if (bgcol > 23)
				bgcol = 0;
			fgcol += 0xe8;
			bgcol += 0xe8;
			break;

		case TB_OUTPUT_NORMAL:
		default:
			fgcol = fg & 0x0F;
			bgcol = bg & 0x0F;
		}

		if (fg & TB_BOLD)
			bytebuffer_puts(&output_buffer, funcs[T_BOLD]);
		if (bg & TB_BOLD)
			bytebuffer_puts(&output_buffer, funcs[T_BLINK]);
		if (fg & TB_UNDERLINE)
			bytebuffer_puts(&output_buffer, funcs[T_UNDERLINE]);
		if ((fg & TB_REVERSE) || (bg & TB_REVERSE))
			bytebuffer_puts(&output_buffer, funcs[T_REVERSE]);

		write_sgr(fgcol, bgcol);

		lastfg = fg;
		lastbg = bg;
	}
}

static void
send_char(int x, int y, uint32_t c)
{
	char buf[7];
	int bw = tb_utf8_unicode_to_char(buf, c);
	if (x - 1 != lastx || y != lasty)
		write_cursor(x, y);
	lastx = x;
	lasty = y;
	if (!c)
		buf[0] = ' '; // replace 0 with whitespace
	bytebuffer_append(&output_buffer, buf, bw);
}

static void
send_clear(void)
{
	send_attr(foreground, background);
	bytebuffer_puts(&output_buffer, funcs[T_CLEAR_SCREEN]);
	if (!IS_CURSOR_HIDDEN(cursor_x, cursor_y))
		write_cursor(cursor_x, cursor_y);
	bytebuffer_flush(&output_buffer, inout);

	/* we need to invalidate cursor position too and these two vars are
     * used only for simple cursor positioning optimization, cursor
     * actually may be in the correct place, but we simply discard
     * optimization once and it gives us simple solution for the case when
     * cursor moved */
	lastx = LAST_COORD_INIT;
	lasty = LAST_COORD_INIT;
}

static void
sigwinch_handler(int xxx)
{
	(void)xxx;
	const int zzz = 1;
	write(winch_fds[1], &zzz, sizeof(int));
}

static void
update_size(void)
{
	update_term_size();
	cellbuf_resize(&back_buffer, termw, termh);
	cellbuf_resize(&front_buffer, termw, termh);
	cellbuf_clear(&front_buffer);
	send_clear();
}

static int
read_up_to(int n)
{
	assert(n > 0);
	const int prevlen = input_buffer.len;
	bytebuffer_resize(&input_buffer, prevlen + n);

	int read_n = 0;
	while (read_n <= n) {
		ssize_t r = 0;
		if (read_n < n) {
			r = read(inout, input_buffer.buf + prevlen + read_n,
				n - read_n);
		}
#ifdef __CYGWIN__
		// While linux man for tty says when VMIN == 0 && VTIME == 0, read
		// should return 0 when there is nothing to read, cygwin's read returns
		// -1. Not sure why and if it's correct to ignore it, but let's pretend
		// it's zero.
		if (r < 0)
			r = 0;
#endif
		if (r < 0) {
			// EAGAIN / EWOULDBLOCK shouldn't occur here
			assert(errno != EAGAIN && errno != EWOULDBLOCK);
			return -1;
		} else if (r > 0) {
			read_n += r;
		} else {
			bytebuffer_resize(&input_buffer, prevlen + read_n);
			return read_n;
		}
	}
	assert(!"unreachable");
	return 0;
}

static int
wait_fill_event(struct tb_event *event, struct timeval *timeout)
{
	// ;-)
#define ENOUGH_DATA_FOR_PARSING 64
	fd_set events;
	memset(event, 0, sizeof(struct tb_event));

	// try to extract event from input buffer, return on success
	event->type = TB_EVENT_KEY;
	if (extract_event(event, &input_buffer, inputmode))
		return event->type;

	// it looks like input buffer is incomplete, let's try the short path,
	// but first make sure there is enough space
	int n = read_up_to(ENOUGH_DATA_FOR_PARSING);
	if (n < 0)
		return -1;
	if (n > 0 && extract_event(event, &input_buffer, inputmode))
		return event->type;

	// n == 0, or not enough data, let's go to select
	while (1) {
		FD_ZERO(&events);
		FD_SET(inout, &events);
		FD_SET(winch_fds[0], &events);
		int maxfd = (winch_fds[0] > inout) ? winch_fds[0] : inout;
		int result = select(maxfd + 1, &events, 0, 0, timeout);
		if (!result)
			return 0;

		if (FD_ISSET(inout, &events)) {
			event->type = TB_EVENT_KEY;
			n = read_up_to(ENOUGH_DATA_FOR_PARSING);
			if (n < 0)
				return -1;

			if (n == 0)
				continue;

			if (extract_event(event, &input_buffer, inputmode))
				return event->type;
		}
		if (FD_ISSET(winch_fds[0], &events)) {
			event->type = TB_EVENT_RESIZE;
			int zzz = 0;
			read(winch_fds[0], &zzz, sizeof(int));
			buffer_size_change_request = 1;
			get_term_size(&event->w, &event->h);
			return TB_EVENT_RESIZE;
		}
	}
}

static void
bytebuffer_reserve(struct bytebuffer *b, int cap)
{
	if (b->cap >= cap) {
		return;
	}

	// prefer doubling capacity
	if (b->cap * 2 >= cap) {
		cap = b->cap * 2;
	}

	char *newbuf = realloc(b->buf, cap);
	b->buf = newbuf;
	b->cap = cap;
}

static void
bytebuffer_init(struct bytebuffer *b, int cap)
{
	b->cap = 0;
	b->len = 0;
	b->buf = 0;

	if (cap > 0) {
		b->cap = cap;
		b->buf = malloc(cap); // just assume malloc works always
	}
}

static void
bytebuffer_free(struct bytebuffer *b)
{
	if (b->buf)
		free(b->buf);
}

static void
bytebuffer_clear(struct bytebuffer *b)
{
	b->len = 0;
}

static void
bytebuffer_append(struct bytebuffer *b, const char *data, int len)
{
	bytebuffer_reserve(b, b->len + len);
	memcpy(b->buf + b->len, data, len);
	b->len += len;
}

static void
bytebuffer_puts(struct bytebuffer *b, const char *str)
{
	bytebuffer_append(b, str, strlen(str));
}

static void
bytebuffer_resize(struct bytebuffer *b, int len)
{
	bytebuffer_reserve(b, len);
	b->len = len;
}

static void
bytebuffer_flush(struct bytebuffer *b, int fd)
{
	write(fd, b->buf, b->len);
	bytebuffer_clear(b);
}

static void
bytebuffer_truncate(struct bytebuffer *b, int n)
{
	if (n <= 0)
		return;
	if (n > b->len)
		n = b->len;
	const int nmove = b->len - n;
	memmove(b->buf, b->buf + n, nmove);
	b->len -= n;
}
