#include <sys/ioctl.h>
#include <sys/stat.h>

#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "sfm.h"
#include "config.h"

static int debug = 1;
#define LOG(format, ...)                                                    \
	{                                                                   \
		if (debug)                                                  \
			log_to_file(                                        \
				__func__, __LINE__, format, ##__VA_ARGS__); \
	};

struct termios orig_termios, new_termios;
static Pane *current_pane;
static Pane panes[2];
static int pane_idx;
char term_buffer[BUFFER_SIZE];
int term_buffer_len = 0;
char status_message[BUFFER_SIZE] = "";
static int rows, cols;
enum { Left, Right }; /* panes */

void
log_to_file(const char *function, int line, const char *format, ...)
{
	static int file_initialized = 0;

	if (!file_initialized) {
		remove("sfm.log");
		file_initialized = 1;
	}

	FILE *file = fopen("sfm.log", "a");
	if (file == NULL) {
		perror("Failed to open log file");
		exit(EXIT_FAILURE);
	}

	time_t now;
	time(&now);
	char time_str[20];
	strftime(time_str, sizeof(time_str), "%F %R:%S", localtime(&now));

	// Write the log message with the specified format
	fprintf(file, "[%s] %s() %d: ", time_str, function, line);

	va_list args;
	va_start(args, format);
	vfprintf(file, format, args);
	va_end(args);

	fprintf(file, "\n");

	fclose(file);
}

void
die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	if (fmt[0] != '\0' && fmt[strlen(fmt) - 1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	} else {
		fputc('\n', stderr);
	}
	exit(EXIT_FAILURE);
}

void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;
	if ((p = calloc(nmemb, size)) == NULL)
		die("calloc:");
	return p;
}

void
enable_raw_mode()
{
	tcgetattr(STDIN_FILENO, &orig_termios);
	new_termios = orig_termios;
	new_termios.c_lflag &= ~(ECHO | ICANON | ISIG);
	new_termios.c_iflag &= ~(IXON | ICRNL);
	new_termios.c_oflag &= ~(OPOST);
	new_termios.c_cflag |= (CS8);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_termios);
	if (write(STDOUT_FILENO, "\x1b[?1049h", 8) < 0)
		die("write:");
}

void
disable_raw_mode()
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
	if (write(STDOUT_FILENO, "\x1b[?1049l", 8) < 0)
		die("write:");
}

void
get_term_size(int *rows, int *cols)
{
	struct winsize ws;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
	*rows = ws.ws_row;
	*cols = ws.ws_col;
}

int
count_entries(const char *path)
{
	DIR *dir;
	struct dirent *entry;
	int count, fd;

	count = 0;
	fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) {
		die("open:");
	}

	dir = fdopendir(fd);
	if (!dir) {
		close(fd);
		die("fdopendir:");
	}

	while ((entry = readdir(dir)) != NULL) {
		if (should_skip_entry(entry))
			continue;
		count++;
	}

	closedir(dir);
	close(fd);
	return count;
}

int
entry_compare(const void *const A, const void *const B)
{
	int result;
	mode_t data1 = (*(Entry *)A).st.st_mode;
	mode_t data2 = (*(Entry *)B).st.st_mode;

	if (data1 < data2) {
		return -1;
	} else if (data1 == data2) {
		result = strncmp(
			(*(Entry *)A).name, (*(Entry *)B).name, NAME_MAX);
		return result;
	} else {
		return 1;
	}
}

void
list_dir(Pane *pane)
{
	int fd, i;
	DIR *dir;
	char tmpfull[PATH_MAX];
	struct dirent *entry;
	struct stat status;

	LOG("listing %s", pane->path);
	fd = open(pane->path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) {
		die("open:");
	}

	dir = fdopendir(fd);
	if (!dir) {
		close(fd);
		die("fdopendir:");
	}

	pane->entry_count = count_entries(pane->path);
	LOG("dir entries count = %d", pane->entry_count);
	pane->entries = ecalloc(pane->entry_count, sizeof(Entry));

	i = 0;
	while ((entry = readdir(dir)) != NULL) {
		if (should_skip_entry(entry)) {
			continue;
		}
		get_fullpath(tmpfull, pane->path, entry->d_name);
		strncpy(pane->entries[i].fullpath, tmpfull, PATH_MAX);
		strncpy(pane->entries[i].name, entry->d_name, NAME_MAX);
		pane->entries[i].fullpath[PATH_MAX - 1] = '\0';
		pane->entries[i].name[NAME_MAX - 1] = '\0';

		if (lstat(tmpfull, &status) == 0)
			pane->entries[i].st = status;
		i++;
	}

	closedir(dir);
	close(fd);
	qsort(pane->entries, pane->entry_count, sizeof(Entry), entry_compare);
	LOG("dir (%s) sorted", pane->path);
}

static void
get_fullpath(char *full_path, char *first, char *second)
{
	if (first[0] == '/' && first[1] == '\0')
		(void)snprintf(full_path, PATH_MAX, "/%s", second);
	else if (snprintf(full_path, PATH_MAX, "%s/%s", first, second) >
		PATH_MAX) {
		die("Path exided maximun length");
	}
}

static int
should_skip_entry(const struct dirent *entry)
{
	if (show_dotfiles == 1) {
		if (entry->d_name[0] == '.' &&
			(entry->d_name[1] == '\0' || entry->d_name[1] == '.')) {
			return 1;
		}
	} else {
		if (entry->d_name[0] == '.') {
			return 1;
		}
	}
	return 0;
}

void
get_entry_color(ColorPair *col, mode_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFREG:
		*col = color_file;
		if ((S_IXUSR | S_IXGRP | S_IXOTH) & mode)
			*col = color_exec;
		break;
	case S_IFDIR:
		*col = color_dir;
		break;
	case S_IFLNK:
		*col = color_lnk;
		break;
	case S_IFBLK:
		*col = color_blk;
		break;
	case S_IFCHR:
		*col = color_chr;
		break;
	case S_IFIFO:
		*col = color_ifo;
		break;
	case S_IFSOCK:
		*col = color_sock;
		break;
	default:
		*col = color_other;
		break;
	}
}

void
termb_append(const char *str, size_t len)
{
	if (term_buffer_len + len < BUFFER_SIZE) {
		strcpy(&term_buffer[term_buffer_len], str);
		term_buffer_len += len;
		int left = BUFFER_SIZE - term_buffer_len;
		LOG("buffer size = %d| left=%d", term_buffer_len, left);
	}
}

void
termb_write()
{
	if (write(STDOUT_FILENO, term_buffer, term_buffer_len) < 0)
		die("write:");
	term_buffer_len = 0;
}

void
display_entries(Pane *pane, int rows, int cols, int col_offset)
{
	int i;
	ColorPair col;

	for (i = 0; i < rows - 2 && pane->start_index + i < pane->entry_count;
		i++) {
		get_entry_color(
			&col, pane->entries[pane->start_index + i].st.st_mode);

		if (pane == current_pane &&
			pane->start_index + i == pane->current_index) {
			col.attr = col.attr | RVS;
		}
		termb_print_at(i + 2, col_offset, col,
			col_offset + (cols / 2) - 1,
			pane->entries[pane->start_index + i].name);
	}
}

void
print_status(ColorPair color, const char *fmt, ...)
{
	char buf[cols];
	int buf_len;
	size_t max_result_size;
	size_t result_len;
	va_list vl;

	va_start(vl, fmt);
	buf_len = vsnprintf(buf, cols, fmt, vl);
	va_end(vl);

	max_result_size = 5 + UINT16_LEN + 4 + 15 + UINT8_LEN + UINT8_LEN +
		UINT8_LEN + buf_len + 6 + 1;

	char result[max_result_size];
	result_len = snprintf(result, max_result_size,
		"\x1b[%hu;1f" // moves cursor to last line, column 1
		"\x1b[2K"     // erase the entire line
		"\x1b[%hu;38;5;%hu;48;5;%hum" // set string colors
		"%s"
		"\x1b[0;0m", // reset colors
		rows, color.attr, color.fg, color.bg, buf);

	if (write(STDOUT_FILENO, result, result_len) < 0)
		die("write:");
}

void
display_entry_details(Pane *pane, int cols, int rows)
{
	if (pane->current_index < pane->entry_count) {
		char *size_str = NULL;
		if (S_ISREG(pane->entries[pane->current_index].st.st_mode)) {
			size_str = get_fsize(
				pane->entries[pane->current_index].st.st_size);
		}

		print_info();
		if (size_str) {
			free(size_str);
		}
	} else {
		print_status(color_err, "No entry details available.");
	}
}

static int
get_fdt(char *result, time_t status)
{
	struct tm lt;
	localtime_r(&status, &lt);
	return strftime(result, MAX_DTF, "%Y-%m-%d %H:%M", &lt);
}

static void
get_entry_owner_group(uid_t uid, gid_t gid)
{

	struct group grp;
	struct group *result;
	char *buffer;
	int s;

	buffer = ecalloc(NAME_MAX, sizeof(char));
	s = getgrgid_r(gid, &grp, buffer, NAME_MAX, &result);
	LOG("uid = %d", uid);
	LOG("gid = %d", gid);
	LOG("group name = %s", result->gr_name);
	LOG("s = %d", s);
	free(buffer);
	//struct passwd *pw = getpwuid(uid);
	//LOG("user name = %s", pw->pw_name);
	//LOG("pass name = %s", pw->pw_passwd);
	//return buffer;
}

static char *
get_fperm(mode_t mode)
{
	char *buf;
	size_t i;

	const char chars[] = "rwxrwxrwx";
	buf = ecalloc(11, sizeof(char));

	if (S_ISDIR(mode))
		buf[0] = 'd';
	else if (S_ISREG(mode))
		buf[0] = '-';
	else if (S_ISLNK(mode))
		buf[0] = 'l';
	else if (S_ISBLK(mode))
		buf[0] = 'b';
	else if (S_ISCHR(mode))
		buf[0] = 'c';
	else if (S_ISFIFO(mode))
		buf[0] = 'p';
	else if (S_ISSOCK(mode))
		buf[0] = 's';
	else
		buf[0] = '?';

	for (i = 1; i < 10; i++) {
		buf[i] = (mode & (1 << (9 - i))) ? chars[i - 1] : '-';
	}
	buf[10] = '\0';

	return buf;
}

static char *
get_fsize(off_t size)
{
	char *result; /* need to be freed */
	char unit;
	int result_len;
	int counter;

	counter = 0;
	result_len = 6; /* 9999X/0 */
	result = ecalloc(result_len, sizeof(char));

	while (size >= 1000) {
		size /= 1024;
		++counter;
	}

	switch (counter) {
	case 0:
		unit = 'B';
		break;
	case 1:
		unit = 'K';
		break;
	case 2:
		unit = 'M';
		break;
	case 3:
		unit = 'G';
		break;
	case 4:
		unit = 'T';
		break;
	default:
		unit = '?';
	}

	if (snprintf(result, result_len, "%ld%c", size, unit) < 0)
		strncat(result, "???", result_len);

	return result;
}

static void
print_info(void)
{
	//char *sz, *ur, *gr, *dt, *prm;
	//char *ur, *gr, *dt, *prm;

	//dt = ecalloc(MAX_DTF, sizeof(char));

	//prm = get_fperm(current_pane->entries[current_pane->current_index].st.st_mode);
	get_entry_owner_group(
		current_pane->entries[current_pane->current_index].st.st_uid,
		current_pane->entries[current_pane->current_index].st.st_gid);
	//gr = get_fgrp(pane->entries[pane->current_index].st.st_gid);

	// if (get_fdt(dt, pane->entries[pane->current_index].st.st_mtime) < 0)
	// 	*dt = '\0';

	// if (S_ISREG(pane->entries[pane->current_index].st.st_mode)) {
	// 	sz = get_fsize(pane->entries[pane->current_index].st.st_size);
	// } else {
	// 	if (dirsize == NULL) {
	// 		sz = ecalloc(1, sizeof(char));
	// 		*sz = '\0';
	// 	} else {
	// 		sz = dirsize;
	// 	}
	// }

	//print_status(color_file, "%02d/%02d %s %s:%s %s",
	//	pane->current_index + 1, pane->entry_count, prm, ur, gr, dt
	//	);

	// free(prm);
	// free(ur);
	// free(gr);
	// free(dt);
	//free(sz);
}

void
handle_keypress(char c)
{
	grabkeys(c, nkeys, sizeof(nkeys) / sizeof(Key));
}

void
move_cursor(const Arg *arg)
{
	current_pane->current_index += arg->i;

	// Ensure the cursor stays within the bounds of the entry count
	if (current_pane->current_index < 0) {
		current_pane->current_index = 0;
	} else if (current_pane->current_index >= current_pane->entry_count) {
		current_pane->current_index = current_pane->entry_count - 1;
	}

	// Ensure the current index stays within the visible range by adjusting the start index
	if (current_pane->current_index < current_pane->start_index) {
		current_pane->start_index = current_pane->current_index;
	} else if (current_pane->current_index >=
		current_pane->start_index + rows - 3) {
		current_pane->start_index =
			current_pane->current_index - (rows - 3);
	}
}

void
switch_pane(const Arg *arg)
{
	LOG("pane was=%p", current_pane);
	current_pane = &panes[pane_idx ^= 1];
	LOG("pane current=%p", current_pane);
}

void
quit(const Arg *arg)
{
	free(panes[Left].entries);
	free(panes[Right].entries);
	disable_raw_mode();
	exit(arg->i);
}

void
grabkeys(uint32_t k, Key *key, size_t max_keys)
{
	LOG("key (%c) is pressed", k);
	size_t i;
	for (i = 0; i < max_keys; i++) {
		if (k == key[i].k) {
			key[i].func(&key[i].arg);
			return;
		}
	}
	print_status(color_err, "No key binding found for key 0x%x", k);
}

void
update_screen()
{
	termb_append("\033[2J", 4);

	termb_print_at(1, 1, color_panell, cols / 2 - 1, panes[Left].path);
	termb_print_at(1, cols / 2, color_panelr, cols, panes[Right].path);

	display_entries(&panes[Left], rows, cols, 0);
	display_entries(&panes[Right], rows, cols, cols / 2);

	display_entry_details(current_pane, cols, rows);
	termb_write();
}

static void
termb_print_at(
	uint16_t x, uint16_t y, ColorPair col, int end, const char *fmt, ...)
{
	char buf[cols];
	int buf_len;
	size_t max_result_size;
	size_t result_len;
	va_list vl;

	va_start(vl, fmt);
	buf_len = vsnprintf(buf, cols, fmt, vl);
	va_end(vl);

	// Fill the rest of the line with spaces to ensure the highlight spans the entire line
	int padding_len = end - y - buf_len;
	if (padding_len < 0)
		padding_len = 0; // Ensure we do not have negative padding

	max_result_size = 4 + UINT16_LEN + UINT16_LEN + 15 + UINT8_LEN +
		UINT8_LEN + UINT8_LEN + buf_len + padding_len + 6 + 1;

	char result[max_result_size];
	result_len = snprintf(result, max_result_size,
		"\x1b[%hu;%huf"               // Move cursor to x y positions
		"\x1b[%hu;38;5;%hu;48;5;%hum" // Set string colors
		"%s%*s"      // String with padding to 'end' position
		"\x1b[0;0m", // Reset colors
		x, y, col.attr, col.fg, col.bg, buf, padding_len, "");

	termb_append(result, result_len);
}

static void
set_panes(void)
{
	char *home;
	char cwd[PATH_MAX];

	home = getenv("HOMEs");
	if (home == NULL)
		home = "/";
	if ((getcwd(cwd, sizeof(cwd)) == NULL))
		strncpy(cwd, home, PATH_MAX - 1);
	strncpy(cwd, "/etc", PATH_MAX - 1);

	strncpy(panes[Left].path, cwd, PATH_MAX - 1);
	panes[Left].entries = NULL;
	panes[Left].entry_count = 0;
	panes[Left].start_index = 0;
	panes[Left].current_index = 0;

	strncpy(panes[Right].path, home, PATH_MAX - 1);
	panes[Right].entries = NULL;
	panes[Right].entry_count = 0;
	panes[Right].start_index = 0;
	panes[Right].current_index = 0;
	LOG("home=%s", home);
	LOG("cwd=%s", cwd);

	pane_idx = Left; /* cursor pane */
	current_pane = &panes[pane_idx];
}

int
main()
{
	enable_raw_mode();
	get_term_size(&rows, &cols);
	LOG("size = %d", rows * cols);
	set_panes();
	list_dir(&panes[Left]);
	list_dir(&panes[Right]);

	if (write(STDOUT_FILENO, "\x1b[2J", 4) < 0) // clear screen
		die("write:");
	update_screen();

	while (1) {
		char c = getchar();
		handle_keypress(c);
		update_screen();
	}

	return 0;
}
