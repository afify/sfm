/* See LICENSE file for copyright and license details. */

#if defined(__linux__)
	#define _GNU_SOURCE
	#include <sys/types.h>
	#include <sys/inotify.h>
	#define EV_BUF_LEN (1024 * (sizeof(struct inotify_event) + 16))
	#define OFF_T      "%ld"
	#define M_TIME     st_mtim

#elif defined(__APPLE__)
	#define _DARWIN_C_SOURCE
	#include <sys/types.h>
	#include <sys/time.h>
	#include <sys/event.h>

	#include <fcntl.h>
	#define OFF_T  "%lld"
	#define M_TIME st_mtimespec

#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
	#define __BSD_VISIBLE 1
	#include <sys/types.h>
	#include <sys/time.h>
	#include <sys/event.h>

	#include <fcntl.h>
	#define OFF_T  "%ld"
	#define M_TIME st_mtim

#elif defined(__OpenBSD__)
	#include <sys/types.h>
	#include <sys/time.h>
	#include <sys/event.h>

	#include <fcntl.h>
	#define OFF_T  "%lld"
	#define M_TIME st_mtim

#endif

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "sfm.h"
#include "config.h"

/* global variables */
static Terminal term;
static Pane *current_pane;
static Pane panes[2];
static int pane_idx;
char *editor[2] = { default_editor, NULL };
char *shell[2] = { default_shell, NULL };
char *home = default_home;
static pid_t fork_pid, main_pid;

enum { Left, Right };    /* panes */
enum { Wait, DontWait }; /* spawn forks */

static void
log_to_file(const char *func, int line, const char *format, ...)
{
	FILE *logfile = fopen("/tmp/sfm.log", "a");
	if (logfile) {
		va_list args;
		va_start(args, format);
		fprintf(logfile, "[%s:%d] ", func, line);
		vfprintf(logfile, format, args);
		va_end(args);
		fprintf(logfile, "\n");
		if (fclose(logfile) != 0) {
			fprintf(stderr, "Error closing log file\n");
		}
	} else {
		fprintf(stderr, "Error opening log file\n");
	}
}

static void
init_term(void)
{
	get_term_size();
	term.buffer_size = term.rows * term.cols * 4;
	term.buffer = ecalloc(term.buffer_size, sizeof(char));
	term.buffer_left = term.buffer_size;
	term.buffer_index = 0;
}

static void
enable_raw_mode(void)
{
	tcgetattr(STDIN_FILENO, &term.orig);
	term.new = term.orig;
	term.new.c_lflag &= ~(ECHO | ICANON | ISIG);
	term.new.c_iflag &= ~(IXON | ICRNL);
	term.new.c_oflag &= ~(OPOST);
	term.new.c_cflag |= (CS8);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &term.new);
	if (write(STDOUT_FILENO, "\x1b[?1049h", 8) < 0)
		die("write:");
}

static void
get_term_size(void)
{
	struct winsize ws;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
	term.rows = ws.ws_row;
	term.cols = ws.ws_col;
}

static void
get_env(void)
{
	char *env_editor;
	char *env_shell;
	char *env_home;

	env_editor = getenv("EDITOR");
	if (env_editor != NULL)
		editor[0] = env_editor;

	env_shell = getenv("SHELL");
	if (env_shell != NULL)
		shell[0] = env_shell;

	env_home = getenv("HOME");
	if (env_home != NULL)
		home = env_home;
}

static int
start_signal(void)
{
	struct sigaction sa;
	main_pid = getpid();
	sa.sa_handler = sighandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGUSR1, &sa, 0);
	sigaction(SIGUSR2, &sa, 0);
	sigaction(SIGWINCH, &sa, 0);
	return 0;
}

static void
sighandler(int signo)
{
	if (fork_pid > 0) /* while forking ignore signals */
		return;

	switch (signo) {
	case SIGWINCH:
		termb_resize();
		break;
	case SIGUSR1:
		set_pane_entries(&panes[Left]);
		update_screen();
		break;
	case SIGUSR2:
		set_pane_entries(&panes[Right]);
		update_screen();
		break;
	}
}

static void
set_panes(void)
{
	char cwd[PATH_MAX];

	if ((getcwd(cwd, sizeof(cwd)) == NULL))
		strncpy(cwd, home, PATH_MAX - 1);

	strncpy(panes[Left].path, cwd, PATH_MAX - 1);
	panes[Left].entries = NULL;
	panes[Left].entry_count = 0;
	panes[Left].start_index = 0;
	panes[Left].current_index = 0;
	panes[Left].watcher.fd = -1;
	panes[Left].watcher.signal = SIGUSR1;

	strncpy(panes[Right].path, home, PATH_MAX - 1);
	panes[Right].entries = NULL;
	panes[Right].entry_count = 0;
	panes[Right].start_index = 0;
	panes[Right].current_index = 0;
	panes[Right].watcher.fd = -1;
	panes[Right].watcher.signal = SIGUSR2;

	pane_idx = Left; /* cursor pane */
	current_pane = &panes[pane_idx];

	set_pane_entries(&panes[Left]);
	set_pane_entries(&panes[Right]);
}

static void
set_pane_entries(Pane *pane)
{
	int fd, i;
	DIR *dir;
	char tmpfull[PATH_MAX];
	struct dirent *entry;
	struct stat status;

	if (pane->entries != NULL) {
		free(pane->entries);
		pane->entries = NULL;
	}

	fd = open(pane->path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) {
		print_status(color_err, strerror(errno));
		return;
	}

	dir = fdopendir(fd);
	if (!dir) {
		close(fd);
		print_status(color_err, strerror(errno));
		return;
	}

	pane->entry_count = 0;
	while ((entry = readdir(dir)) != NULL) {
		if (!should_skip_entry(entry)) {
			pane->entry_count++;
		}
	}

	pane->entries = ecalloc(pane->entry_count, sizeof(Entry));

	rewinddir(dir);

	i = 0;
	while ((entry = readdir(dir)) != NULL) {
		if (should_skip_entry(entry)) {
			continue;
		}
		get_fullpath(tmpfull, pane->path, entry->d_name);

		if (i >= pane->entry_count) {
			log_to_file(__func__, __LINE__,
				"Entry count exceeded allocated memory");
			break;
		}

		// file deleted while getting its details
		if (lstat(tmpfull, &status) != 0) {
			log_to_file(__func__, __LINE__,
				"lstat error for %s: %s", tmpfull,
				strerror(errno));
			memset(&pane->entries[i], 0, sizeof(Entry));
			strncpy(pane->entries[i].fullpath, tmpfull,
				PATH_MAX - 1);
			strncpy(pane->entries[i].name, entry->d_name,
				NAME_MAX - 1);
			i++;
			continue;
		}

		size_t fullpath_len = strlen(tmpfull);
		size_t name_len = strlen(entry->d_name);

		memcpy(pane->entries[i].fullpath, tmpfull, fullpath_len);
		pane->entries[i].fullpath[fullpath_len] = '\0';

		memcpy(pane->entries[i].name, entry->d_name, name_len);
		pane->entries[i].name[name_len] = '\0';

		pane->entries[i].st = status;
		i++;
	}

	pane->entry_count = i;

	closedir(dir);
	close(fd);
	qsort(pane->entries, pane->entry_count, sizeof(Entry), entry_compare);
}

static int
should_skip_entry(const struct dirent *entry)
{
	if (entry->d_name[0] == '.') {
		if (entry->d_name[1] == '\0' ||
			(entry->d_name[1] == '.' && entry->d_name[2] == '\0'))
			return 1;
		if (show_dotfiles != 1)
			return 1;
	}
	return 0;
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

// static int
// entry_compare(const void *const A, const void *const B)
// {
// 	int result;
// 	mode_t data1 = (*(Entry *)A).st.st_mode;
// 	mode_t data2 = (*(Entry *)B).st.st_mode;
//
// 	if (data1 < data2) {
// 		return -1;
// 	} else if (data1 == data2) {
// 		result = strncmp(
// 			(*(Entry *)A).name, (*(Entry *)B).name, NAME_MAX);
// 		return result;
// 	} else {
// 		return 1;
// 	}
// }

static int
entry_compare(const void *a, const void *b)
{
	const Entry *entryA = (const Entry *)a;
	const Entry *entryB = (const Entry *)b;

	if (!entryA || !entryB) {
		return 0;
	}

	mode_t modeA = entryA->st.st_mode;
	mode_t modeB = entryB->st.st_mode;

	if (modeA < modeB) {
		return -1;
	} else if (modeA == modeB) {
		return strncmp(entryA->name, entryB->name, NAME_MAX);
	} else {
		return 1;
	}
}

static void
update_screen(void)
{
	termb_append("\x1b[H\x1b[999B\x1b[1J",
		13); // Move to the last line and clear above

	termb_print_at(1, 1, color_panell, term.cols / 2, panes[Left].path);
	termb_print_at(
		1, term.cols / 2, color_panelr, term.cols, panes[Right].path);

	append_entries(&panes[Left], 0);
	append_entries(&panes[Right], term.cols / 2);
	termb_write();

	//display_entry_details();
}

static void
disable_raw_mode(void)
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &term.orig);
	if (write(STDOUT_FILENO, "\x1b[?1049l", 8) < 0)
		die("write:");
}

static void
append_entries(Pane *pane, int col_offset)
{
	int i;
	ColorPair col;
	size_t index = 0;
	char if_selected[] = "\u2B1C";

	if (pane->entries == NULL) {
		return;
	}
	char *buffer = ecalloc(term.buffer_size, sizeof(char));
	termb_append("\x1b[2;1f", 6); // move to top left
	for (i = 0;
		i < term.rows - 2 && pane->start_index + i < pane->entry_count;
		i++) {

		if (pane->start_index + i >= pane->entry_count ||
			pane->entries == NULL) {
			continue;
		}

		get_entry_color(
			&col, pane->entries[pane->start_index + i].st.st_mode);

		if (pane == current_pane &&
			pane->start_index + i == pane->current_index) {
			col.attr = col.attr | RVS;
		}

		if (pane->entries[i].selected == 1) {
			strcpy(if_selected, "\u2705");
		} else {
			strcpy(if_selected, "\u2B1C");
		}

		// Truncate string based on byte length
		size_t max_len = (term.cols / 2);
		char truncated_name[max_len + 1];
		strncpy(truncated_name,
			pane->entries[pane->start_index + i].name, max_len);
		truncated_name[max_len - 1] = '.';
		truncated_name[max_len] = '\0';

		index += snprintf(buffer + index, term.buffer_size - index,
			"\x1b[%dG"
			"\x1b[%d;38;5;%d;48;5;%dm%s\x1b[0m"
			"\x1b[%d;38;5;%dm%s\x1b[0m\r\n",
			col_offset, color_frame.attr, color_frame.fg,
			color_frame.bg, if_selected, col.attr, col.fg,
			truncated_name);
	}
	termb_append(buffer, index);
	free(buffer);
}

static void
handle_keypress(char c)
{
	log_to_file(__func__, __LINE__, "key: (%c)", c);
	grabkeys(c, nkeys, nkeyslen);
}

static void
grabkeys(uint32_t k, Key *key, size_t max_keys)
{
	size_t i;
	for (i = 0; i < max_keys; i++) {
		if (k == key[i].k) {
			key[i].func(&key[i].arg);
			return;
		}
	}
	print_status(color_err, "No key binding found for key 0x%x", k);
}

static void
print_status(ColorPair color, const char *fmt, ...)
{
	char buf[term.cols];
	int buf_len;
	size_t max_result_size;
	size_t result_len;
	va_list vl;

	va_start(vl, fmt);
	buf_len = vsnprintf(buf, term.cols, fmt, vl);
	va_end(vl);

	max_result_size = 5 + UINT16_LEN + 4 + 15 + UINT8_LEN + UINT8_LEN +
		UINT8_LEN + buf_len + 6 + 1;

	char result[max_result_size];
	result_len = snprintf(result, max_result_size,
		"\x1b[%d;1f" // moves cursor to last line, column 1
		"\x1b[2K"    // erase the entire line
		"\x1b[%hu;38;5;%hu;48;5;%hum" // set string colors
		"%s"
		"\x1b[0;0m", // reset colors
		term.rows, color.attr, color.fg, color.bg, buf);

	if (write(STDOUT_FILENO, result, result_len) < 0)
		die("write:");
}

static void
display_entry_details(void)
{
	char sz[32], ur[USER_MAX], gr[GROUP_MAX], dt[DATETIME_MAX], prm[11];
	struct stat st;

	if (current_pane == NULL || current_pane->entries == NULL ||
		current_pane->entry_count < 1) {
		print_status(color_warn, "Empty directory.");
		return;
	}

	if (current_pane->current_index >= current_pane->entry_count) {
		return;
	}

	st = current_pane->entries[current_pane->current_index].st;

	get_entry_permission(prm, st.st_mode);
	get_entry_owner(ur, st.st_uid);
	get_entry_group(gr, st.st_gid);
	get_entry_datetime(dt, st.M_TIME.tv_sec);
	get_file_size(sz, st.st_size);

	print_status(color_status, "%02d/%02d %s %s:%s %s %s",
		current_pane->current_index + 1, current_pane->entry_count, prm,
		ur, gr, dt, sz);
}

static void
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

static void
get_entry_datetime(char *buf, time_t status)
{
	struct tm lt;
	localtime_r(&status, &lt);
	strftime(buf, DATETIME_MAX, "%Y-%m-%d %H:%M", &lt);
	buf[DATETIME_MAX - 1] = '\0';
}

static void
get_entry_permission(char *buf, mode_t mode)
{
	size_t i;
	const char chars[] = "rwxrwxrwx";

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
}

static void
get_file_size(char *buf, off_t size)
{
	char unit;
	int counter = 0;

	while (size >= 1024) {
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

	snprintf(buf, 32, OFF_T "%c", size, unit);
}

static void
get_entry_owner(char *buf, uid_t uid)
{
	struct passwd *pw = getpwuid(uid);
	if (pw == NULL) {
		snprintf(buf, USER_MAX, "%u", uid);
	} else {

		size_t len = strlen(pw->pw_name);
		if (len >= USER_MAX) {
			len = USER_MAX - 1;
		}
		memcpy(buf, pw->pw_name, len);
		buf[len] = '\0';
	}
}

static void
get_entry_group(char *buf, gid_t gid)
{
	struct group *gr = getgrgid(gid);
	if (gr == NULL) {
		snprintf(buf, GROUP_MAX, "%u", gid);
	} else {
		strncpy(buf, gr->gr_name, GROUP_MAX - 1);
		buf[GROUP_MAX - 1] = '\0';
	}
}

static int
get_user_input(char *input, size_t size, const char *prompt, ...)
{
	va_list args;
	char msg[PROMPT_MAX];

	va_start(args, prompt);
	vsnprintf(msg, PROMPT_MAX, prompt, args);
	print_status(color_prompt, msg);
	va_end(args);

	size_t index = 0;
	int c;

	while (1) {
		c = getchar();

		switch (c) {
		case XK_ESC:
			//display_entry_details();
			return -1;
		case XK_ENTER:
			input[index] = '\0';
			//display_entry_details();
			return 0;
		case XK_BACKSPACE:
			if (index > 0) {
				index--;
				printf("\b \b");
			}
			break;
		default:
			if (index < size - 1) {
				input[index++] = c;
				putchar(c);
			}
			break;
		}
	}

	return 0;
}

static int
check_dir(char *path)
{
	DIR *dir;
	dir = opendir(path);

	if (dir == NULL) {
		if (errno == ENOTDIR) {
			return 1;
		} else {
			return -1;
		}
	}

	if (closedir(dir) < 0)
		return -1;

	return 0;
}

static int
open_file(char *file)
{
	char *ext;
	int rule_index;
	Command cmd;

	ext = get_file_extension(file);
	rule_index = -1;

	if (ext != NULL) {
		rule_index = check_rule(ext);
		free(ext);
	}

	if (rule_index < 0) {
		cmd.command = editor;
		cmd.command_count = 1;
		cmd.source_paths = NULL;
		cmd.source_count = 0;
		cmd.target = file;
		cmd.wait_for_completion = Wait;
	} else {
		cmd.command = (char **)rules[rule_index].v;
		cmd.command_count = rules[rule_index].vlen;
		cmd.source_paths = NULL;
		cmd.source_count = 0;
		cmd.target = file;
		cmd.wait_for_completion = Wait;
	}

	disable_raw_mode();
	execute_command(&cmd);
	enable_raw_mode();
	termb_resize();

	return 0;
}

static char *
get_file_extension(char *str)
{
	char *ext;
	char *dot;

	if (!str)
		return NULL;

	dot = strrchr(str, '.');
	if (!dot || dot == str)
		return NULL;

	ext = ecalloc(EXTENTION_MAX + 1, sizeof(char));
	strncpy(ext, dot + 1, EXTENTION_MAX);

	for (char *p = ext; *p; p++)
		*p = tolower((unsigned char)*p);

	return ext;
}

static int
check_rule(char *ex)
{
	size_t c, d;

	for (c = 0; c < LEN(rules); c++)
		for (d = 0; d < rules[c].exlen; d++)
			if (strncmp(rules[c].ext[d], ex, EXTENTION_MAX) == 0)
				return c;
	return -1;
}

static int
execute_command(Command *cmd)
{
	size_t argc;

	argc = cmd->command_count + cmd->source_count + 2;
	char *argv[argc];

	memcpy(argv, cmd->command,
		cmd->command_count * sizeof(char *)); /* command */
	memcpy(&argv[cmd->command_count], cmd->source_paths,
		cmd->source_count * sizeof(char *)); /* files */

	argv[argc - 2] = cmd->target;
	argv[argc - 1] = NULL;

	fork_pid = fork();
	switch (fork_pid) {
	case -1:
		return -1;
	case 0:
		execvp(argv[0], argv);
		exit(EXIT_SUCCESS);
	default:
		if (cmd->wait_for_completion == Wait) {
			int status;
			waitpid(fork_pid, &status, 0);
		}
	}
	fork_pid = 0;

	return 0;
}

static void
termb_append(const char *str, size_t len)
{
	if (len >= term.buffer_left) {
		term.buffer = erealloc(term.buffer, term.buffer_size * 2);
		term.buffer_size *= 2;
	}

	memcpy(&term.buffer[term.buffer_index], str, len);
	term.buffer_index += len;
	term.buffer_left = term.buffer_size - term.buffer_index;
}

static void
termb_write(void)
{
	if (write(STDOUT_FILENO, term.buffer, term.buffer_index - 1) < 0)
		die("write:");
	term.buffer_index = 0;
	term.buffer_left = term.buffer_size;
}

static void
termb_print_at(
	uint16_t x, uint16_t y, ColorPair col, int end, const char *fmt, ...)
{
	char buf[term.cols];
	int buf_len;
	size_t max_result_size;
	size_t result_len;
	va_list vl;

	va_start(vl, fmt);
	buf_len = vsnprintf(buf, term.cols, fmt, vl);
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
termb_resize(void)
{
	termb_append("\033[2J", 4);
	get_term_size();
	update_screen();
}

static void
cd_to_parent(const Arg *arg)
{
	char parent_path[PATH_MAX];
	char *last_slash;

	if (current_pane->path[0] == '/' && current_pane->path[1] == '\0')
		return;

	strncpy(parent_path, current_pane->path, PATH_MAX);
	last_slash = strrchr(parent_path, '/');
	if (last_slash != NULL)
		*last_slash = '\0';

	if (strnlen(parent_path, PATH_MAX) == 0) {
		strncpy(parent_path, "/", PATH_MAX);
	}

	strncpy(current_pane->path, parent_path, PATH_MAX);

	remove_watch(current_pane);
	set_pane_entries(current_pane);
	add_watch(current_pane);

	current_pane->current_index = 0;
	current_pane->start_index = 0;
	update_screen();
}

static void
create_new_file(const Arg *arg)
{
	char file_name[NAME_MAX];
	char full_path[PATH_MAX];
	int fd;

	if (get_user_input(file_name, NAME_MAX, "new file: ") != 0)
		return;

	snprintf(full_path, PATH_MAX, "%s/%s", current_pane->path, file_name);

	fd = open(full_path, O_CREAT | O_EXCL, new_file_perm);
	if (fd < 0) {
		print_status(color_err, strerror(errno));
		return;
	}

	//display_entry_details();
	close(fd);
}

static void
create_new_dir(const Arg *arg)
{
	char dir_name[NAME_MAX];
	char full_path[PATH_MAX];

	if (get_user_input(dir_name, sizeof(dir_name), "new directory: ") != 0)
		return;

	snprintf(full_path, PATH_MAX, "%s/%s", current_pane->path, dir_name);

	if (mkdir(full_path, new_dir_perm) != 0)
		print_status(color_err, strerror(errno));
}

static void
delete_entry(const Arg *arg)
{
	if (current_pane->entry_count <= 0 ||
		current_pane->current_index >= current_pane->entry_count) {
		print_status(color_err, "No entry selected or invalid index.");
		return;
	}

	char confirmation[4];
	printf("Are you sure you want to delete '%s'? (%s): ", delconf,
		current_pane->entries[current_pane->current_index].name);
	if (get_user_input(confirmation, sizeof(confirmation), "") != 0 ||
		strncmp(confirmation, delconf, delconf_len) != 0) {
		print_status(color_warn, "Deletion aborted.");
		return;
	}

	Command cmd;
	cmd.command = (char **)rm_cmd;
	cmd.command_count = rm_cmd_len;
	cmd.source_paths = NULL;
	cmd.source_count = 0;
	cmd.target =
		current_pane->entries[current_pane->current_index].fullpath;
	cmd.wait_for_completion = DontWait;

	if (execute_command(&cmd) != 0) {
		print_status(color_err, strerror(errno));
		return;
	}
}

static void
move_bottom(const Arg *arg)
{
	current_pane->current_index = current_pane->entry_count - 1;
	current_pane->start_index = current_pane->entry_count - (term.rows - 2);
	if (current_pane->start_index < 0) {
		current_pane->start_index = 0;
	}
	update_screen();
}

static void
move_cursor(const Arg *arg)
{
	if (current_pane->entry_count == 0)
		return;

	current_pane->current_index += arg->i;

	if (current_pane->current_index < 0) {
		current_pane->current_index = 0;
	} else if (current_pane->current_index >= current_pane->entry_count) {
		current_pane->current_index = current_pane->entry_count - 1;
	}

	if (current_pane->current_index < current_pane->start_index) {
		current_pane->start_index = current_pane->current_index;
	} else if (current_pane->current_index >=
		current_pane->start_index + term.rows - 2) {
		current_pane->start_index =
			current_pane->current_index - (term.rows - 3);
	}
	update_screen();
}

static void
move_top(const Arg *arg)
{
	current_pane->current_index = 0;
	current_pane->start_index = 0;
	update_screen();
}

static void
open_entry(const Arg *arg)
{
	if (current_pane->entry_count < 1)
		return;

	int s;
	Entry *current_entry =
		&current_pane->entries[current_pane->current_index];

	switch (check_dir(current_entry->fullpath)) {
	case 0: /* directory */
		strncpy(current_pane->path, current_entry->fullpath, PATH_MAX);
		remove_watch(current_pane);
		set_pane_entries(current_pane);
		add_watch(current_pane);
		current_pane->current_index = 0;
		current_pane->start_index = 0;
		update_screen();
		break;

		break;
	case 1: /* not a directory open file */
		if (S_ISREG(current_entry->st.st_mode)) {
			s = open_file(current_entry->fullpath);
			if (s < 0)
				print_status(color_err, strerror(errno));
		}
		break;
	case -1: /* failed to open directory */
		print_status(color_err, strerror(errno));
	}
}

static void
quit(const Arg *arg)
{
	cleanup_filesystem_events();
	if (term.buffer != NULL)
		free(term.buffer);
	if (panes[Left].entries != NULL)
		free(panes[Left].entries);
	if (panes[Right].entries != NULL)
		free(panes[Right].entries);
	disable_raw_mode();
	exit(EXIT_SUCCESS);
}

static void
refresh(const Arg *arg)
{
	kill(main_pid, SIGWINCH);
}

static void
switch_pane(const Arg *arg)
{
	current_pane = &panes[pane_idx ^= 1];
	update_screen();
}

static void
select_all(const Arg *arg)
{
	if (current_pane->entry_count <= 0) {
		print_status(color_warn, "No entries to select.");
		return;
	}

	for (int i = 0; i < current_pane->entry_count; i++) {
		current_pane->entries[i].selected ^= 1;
	}
	update_screen();
}

static void
select_entry(const Arg *arg)
{
	current_pane->entries[current_pane->current_index].selected ^= 1;
	update_screen();
}

static void
toggle_dotfiles(const Arg *arg)
{
	show_dotfiles ^= 1;
	set_pane_entries(&panes[Left]);
	set_pane_entries(&panes[Right]);
	update_screen();
}

static void
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

static void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;
	if ((p = calloc(nmemb, size)) == NULL)
		die("calloc:");
	return p;
}

static void *
erealloc(void *p, size_t len)
{
	if ((p = realloc(p, len)) == NULL)
		die("realloc: %s\n", strerror(errno));
	return p;
}

#if defined(__linux__)
static void *
event_handler(void *arg)
{
	Pane *pane = (Pane *)arg;
	char buffer[EV_BUF_LEN];
	int length, i;

	pane->watcher.fd = inotify_init();
	if (pane->watcher.fd < 0) {
		die("inotify_init:");
		pthread_exit(NULL);
	}

	add_watch(pane);

	while (1) {
		length = read(pane->watcher.fd, buffer, EV_BUF_LEN);
		if (length <= 0) {
			die("read:");
			break;
		}

		if (length < (int)sizeof(struct inotify_event)) {
			die("read:");
			break;
		}

		i = 0;
		while (i < length) {
			struct inotify_event *event =
				(struct inotify_event *)&buffer[i];
			if (event->mask) {
				usleep(50 * 1000); // 500 milliseconds
				kill(main_pid, pane->watcher.signal);
			}
			i += sizeof(struct inotify_event) + event->len;
		}
	}
	close(pane->watcher.fd);
	return NULL;
}

void
add_watch(Pane *pane)
{
	pane->watcher.descriptor = inotify_add_watch(pane->watcher.fd,
		pane->path, IN_MODIFY | IN_CREATE | IN_DELETE);
	if (pane->watcher.descriptor < 0)
		die("inotify_add_watch:");
}

void
remove_watch(Pane *pane)
{
	if (inotify_rm_watch(pane->watcher.fd, pane->watcher.descriptor) < 0)
		die("inotify_rm_watch:");
}

void
cleanup_filesystem_events(void)
{
	remove_watch(&panes[Left]);
	pthread_cancel(panes[Left].watcher.thread);
	pthread_join(panes[Left].watcher.thread, NULL);

	remove_watch(&panes[Right]);
	pthread_cancel(panes[Right].watcher.thread);
	pthread_join(panes[Right].watcher.thread, NULL);

	close(panes[Left].watcher.fd);
	close(panes[Right].watcher.fd);
}

void
filesystem_event_init(void)
{
	pthread_create(
		&panes[Left].watcher.thread, NULL, event_handler, &panes[Left]);
	pthread_create(&panes[Right].watcher.thread, NULL, event_handler,
		&panes[Right]);
}

#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
	defined(__APPLE__) || defined(__DragonFly__)

static void *
event_handler(void *arg)
{
	Pane *pane = (Pane *)arg;
	struct kevent event;

	log_to_file(__func__, __LINE__, "Event handler started for path: %s",
		pane->path);

	pane->watcher.kq = kqueue();
	if (pane->watcher.kq == -1) {
		log_to_file(__func__, __LINE__, "kqueue creation error: %s",
			strerror(errno));
		pthread_exit(NULL);
	}

	log_to_file(__func__, __LINE__, "Kqueue created with descriptor: %d",
		pane->watcher.kq);

	add_watch(pane);

	while (1) {
		log_to_file(__func__, __LINE__, "Waiting for events...");
		int nev = kevent(pane->watcher.kq, NULL, 0, &event, 1,
			NULL); // NULL timeout for indefinite wait
		if (nev < 0) {
			if (errno == EINTR) {
				continue; // Retry if interrupted by signal
			}
			log_to_file(__func__, __LINE__, "kevent wait error: %s",
				strerror(errno));
			pthread_exit(NULL);
		} else if (nev > 0) {
			log_to_file(__func__, __LINE__, "Event detected.");
			usleep(500 * 1000); // 500 milliseconds
			kill(main_pid, pane->watcher.signal);
			if (event.fflags & NOTE_DELETE) {
				log_to_file(__func__, __LINE__,
					"File deleted: %s", pane->path);
				break; // Exit the loop if the file is deleted
			}
			log_to_file(__func__, __LINE__,
				"Re-adding watch after event.");
			add_watch(
				pane); // Re-add the watch after handling the event
		}
	}

	log_to_file(__func__, __LINE__, "Closing file descriptor: %d",
		pane->watcher.fd);
	close(pane->watcher.fd);
	close(pane->watcher.kq);
	return NULL;
}

static void
add_watch(Pane *pane)
{
	if (pane->watcher.fd >= 0) {
		log_to_file(__func__, __LINE__,
			"Closing previous file descriptor: %d",
			pane->watcher.fd);
		close(pane->watcher.fd);
	}

	pane->watcher.fd = open(pane->path, O_RDONLY);
	if (pane->watcher.fd < 0) {
		log_to_file(
			__func__, __LINE__, "open error: %s", strerror(errno));
		return;
	}

	log_to_file(__func__, __LINE__,
		"Opened file descriptor: %d for path: %s", pane->watcher.fd,
		pane->path);

	EV_SET(&pane->watcher.change, pane->watcher.fd, EVFILT_VNODE,
		EV_ADD | EV_ENABLE | EV_ONESHOT,
		NOTE_DELETE | NOTE_WRITE | NOTE_ATTRIB | NOTE_RENAME |
			NOTE_REVOKE,
		0, (void *)pane->path);

	if (kevent(pane->watcher.kq, &pane->watcher.change, 1, NULL, 0, NULL) ==
		-1) {
		log_to_file(__func__, __LINE__, "kevent register error: %s",
			strerror(errno));
		close(pane->watcher.fd);
		pane->watcher.fd = -1;
		return;
	}

	log_to_file(__func__, __LINE__,
		"Event registered successfully for path: %s", pane->path);
}

static void
remove_watch(Pane *pane)
{
	if (pane->watcher.fd >= 0) {
		log_to_file(__func__, __LINE__,
			"Removing watch for file descriptor: %d",
			pane->watcher.fd);
		if (close(pane->watcher.fd) < 0)
			log_to_file(__func__, __LINE__,
				"kevent remove error: %s", strerror(errno));
		pane->watcher.fd = -1;
	}
}

void
cleanup_filesystem_events(void)
{
	remove_watch(&panes[Left]);
	pthread_cancel(panes[Left].watcher.thread);
	//pthread_join(panes[Left].watcher.thread, NULL);

	remove_watch(&panes[Right]);
	pthread_cancel(panes[Right].watcher.thread);
	//pthread_join(panes[Right].watcher.thread, NULL);

	close(panes[Left].watcher.kq);
	close(panes[Right].watcher.kq);
}

void
filesystem_event_init(void)
{
	pthread_create(
		&panes[Left].watcher.thread, NULL, event_handler, &panes[Left]);
	pthread_create(&panes[Right].watcher.thread, NULL, event_handler,
		&panes[Right]);
}

#endif

int
main(int argc, char *argv[])
{
	char c;

	if (remove("/tmp/sfm.log") != 0) {
		fprintf(stderr, "Error removing log file: %s\n",
			strerror(errno));
	}

	if (argc == 1) {
#if defined(__OpenBSD__)
		if (pledge("cpath exec getpw proc rpath stdio tmppath tty wpath",
			    NULL) == -1)
			die("pledge");
#endif /* __OpenBSD__ */
		init_term();
		enable_raw_mode();
		get_env();
		set_panes();
		start_signal();

		termb_append("\033[2J", 4);
		update_screen();

		filesystem_event_init();
		while (1) {
			c = getchar();
			handle_keypress(c);
		}
	} else if (argc == 2 && strncmp("-v", argv[1], 2) == 0) {
		die("sfm-" VERSION);
	} else {
		die("usage: sfm [-v]");
	}

	return 0;
}
