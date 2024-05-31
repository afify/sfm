#if defined(__linux__)
#define _GNU_SOURCE
#elif defined(__APPLE__)
#define _DARWIN_C_SOURCE
#elif defined(__FreeBSD__)
#define __BSD_VISIBLE 1
#endif
#if defined(__linux__)
#include <sys/inotify.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
	defined(__APPLE__)
#include <sys/types.h>
#include <sys/time.h>
#include <sys/event.h>

#include <fcntl.h>
#endif
#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__)
#define OFF_T "%ld"
#elif defined(__OpenBSD__) || defined(__APPLE__)
#define OFF_T "%lld"
#endif
#if defined(__APPLE__)
#define M_TIME st_mtimespec
#else
#define M_TIME st_mtim
#endif

#include <sys/types.h>
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

#define LOG(format, ...)                                                \
	{                                                               \
		log_to_file(__func__, __LINE__, format, ##__VA_ARGS__); \
	}

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
start(void)
{
	init_term();
	enable_raw_mode();
	get_env();
	set_panes();
	start_signal();
	filesystem_event_init();
	start_watcher_threads();

	termb_append("\033[2J", 4);
	update_screen();

	while (1) {
		LOG("----GETCHAR");
		char c = getchar();
		handle_keypress(c);
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
	//LOG("term size = %d", term.buffer_size);
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
	switch (signo) {
	case SIGWINCH:
		//LOG("----SIGWINCH");
		termb_resize();
		break;
	case SIGUSR1:
		LOG("----SIGUSR1");
		set_pane_entries(&panes[Left]);
		// update_screen();
		break;
	case SIGUSR2:
		LOG("----SIGUSR2");
		set_pane_entries(&panes[Right]);
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

	strncpy(panes[Right].path, home, PATH_MAX - 1);
	panes[Right].entries = NULL;
	panes[Right].entry_count = 0;
	panes[Right].start_index = 0;
	panes[Right].current_index = 0;
	//LOG("home=%s", home);
	//LOG("cwd=%s", cwd);

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
		die("open:");
	}

	dir = fdopendir(fd);
	if (!dir) {
		close(fd);
		die("fdopendir:");
	}

	pane->entry_count = count_entries(pane->path);
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

	closedir(dir); // Ensure the directory stream is closed
	close(fd);
	qsort(pane->entries, pane->entry_count, sizeof(Entry), entry_compare);
}

static int
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

	// if (show_dotfiles != 1 && entry->d_name[0] == '.')
	// 	return 1;

	// if (entry->d_name[0] == '.' &&
	// 	(entry->d_name[1] == '\0' ||
	// 		(entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
	// 	return 1;

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

	// Ensure both pointers are valid
	if (!entryA || !entryB) {
		return 0;
	}

	int result;
	mode_t modeA = entryA->st.st_mode;
	mode_t modeB = entryB->st.st_mode;

	if (modeA < modeB) {
		return -1;
	} else if (modeA == modeB) {
		result = strncmp(entryA->name, entryB->name, NAME_MAX);
		return result;
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

	display_entry_details();
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
	char *buffer =
		ecalloc(term.buffer_size, sizeof(char)); // Allocate a buffer
	size_t index = 0;

	termb_append("\x1b[2;1f", 6); // move to top left
	for (i = 0;
		i < term.rows - 2 && pane->start_index + i < pane->entry_count;
		i++) {
		get_entry_color(
			&col, pane->entries[pane->start_index + i].st.st_mode);

		if (pane == current_pane &&
			pane->start_index + i == pane->current_index) {
			col.attr = col.attr | RVS;
		}

		// Truncate string based on byte length
		size_t max_len = (term.cols / 2);
		char truncated_name[max_len + 1];
		strncpy(truncated_name,
			pane->entries[pane->start_index + i].name, max_len);
		truncated_name[max_len - 1] = '.';
		truncated_name[max_len] = '\0';

		index += snprintf(buffer + index, term.buffer_size - index,
			"\x1b[%dG\x1b[%d;38;5;%dm%s\x1b[0m\r\n", col_offset,
			col.attr, col.fg, truncated_name);
	}
	termb_append(buffer, index);
	free(buffer);
}

static void
handle_keypress(char c)
{
	grabkeys(c, nkeys, nkeyslen);
}

static void
grabkeys(uint32_t k, Key *key, size_t max_keys)
{
	//LOG("key (%c) is pressed", k);
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
	char *sz, *ur, *dt, *prm;
	struct stat st;

	if (current_pane->entry_count < 1) {
		print_status(color_err, "Empty directory.");
		return;
	}

	st = current_pane->entries[current_pane->current_index].st;
	prm = get_entry_permission(st.st_mode);
	ur = get_entry_owner(st.st_uid);
	//gr = get_entry_group(st.st_gid);
	dt = get_entry_datetime(st.M_TIME.tv_sec);
	sz = get_file_size(st.st_size);

	print_status(color_status, "%02d/%02d %s %s: %s %s",
		current_pane->current_index + 1, current_pane->entry_count, prm, ur, dt,
		sz);

	free(prm);
	free(ur);
	//free(gr);
	free(dt);
	free(sz);
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

static char *
get_entry_datetime(time_t status)
{
	char *result;
	struct tm lt;

	result = ecalloc(DATETIME_MAX, sizeof(char));
	localtime_r(&status, &lt);
	strftime(result, DATETIME_MAX, "%Y-%m-%d %H:%M", &lt);
	result[DATETIME_MAX - 1] = '\0';
	return result;
}

static char *
get_entry_permission(mode_t mode)
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
get_file_size(off_t size)
{
	char *result;
	char unit;
	int result_len;
	int counter;

	counter = 0;
	result_len = 6;
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

	if (snprintf(result, result_len, OFF_T "%c", size, unit) < 0)
		strncat(result, "???", result_len);

	return result;
}

static char *
get_entry_owner(uid_t status)
{
	char *result;
	struct passwd *pw;

	result = ecalloc(USER_MAX, sizeof(char));
	pw = getpwuid(status);
	if (pw == NULL)
		(void)snprintf(result, USER_MAX, "%u", status);
	else
		strncpy(result, pw->pw_name, USER_MAX);

	result[USER_MAX - 1] = '\0';
	return result;
}

static char *
get_entry_group(gid_t status)
{
	char *result;
	struct group *gr;

	result = ecalloc(GROUP_MAX, sizeof(char));
	gr = getgrgid(status);
	if (gr == NULL)
		(void)snprintf(result, GROUP_MAX, "%u", status);
	else
		strncpy(result, gr->gr_name, GROUP_MAX);

	result[GROUP_MAX - 1] = '\0';
	return result;
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

	//LOG("ext = %s", ext);
	if (ext != NULL) {
		rule_index = check_rule(ext);
		//LOG("rule_index = %d", rule_index);
		//LOG("ex = %s", ext);
		free(ext);
	}

	if (rule_index < 0) {
		cmd.source_paths = NULL;
		cmd.source_count = 0;
		cmd.command = editor;
		cmd.command_count = 1;
		cmd.target = file;
		cmd.wait_for_completion = Wait;
	} else {
		cmd.source_paths = NULL;
		cmd.source_count = 0;
		cmd.command = (char **)rules[rule_index].v;
		cmd.command_count = rules[rule_index].vlen;
		cmd.target = file;
		cmd.wait_for_completion = Wait;
	}

	execute_command(&cmd);
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

	// //Log the complete command to be executed
	char command_log[1024] = { 0 }; // Adjust the size as needed
	for (size_t i = 0; argv[i] != NULL; i++) {
		strcat(command_log, argv[i]);
		strcat(command_log, " ");
	}
	//LOG("argv[0]= %s", argv[0]);
	//LOG("argv[1]= %s", argv[1]);
	//LOG("Executing command: %s", command_log);

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
		// if (waiting == Wait) {
		// 	while ((r = waitpid(fork_pid, &ws, 0)) == -1 &&
		// 		errno == EINTR)
		// 		continue;
		// 	if (r == -1)
		// 		return -1;
		// 	if ((WIFEXITED(ws) != 0) && (WEXITSTATUS(ws) != 0))
		// 		return -1;
		// }
	}
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
	//LOG("APPEND->(%d) term size = %d left(%d)", len, term.buffer_size,
	//	term.buffer_left);
}

static void
termb_write(void)
{
	//LOG("WRITE -> (%d) left (%d)", term.buffer_index, term.buffer_left);
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
	//LOG("INDEX = %d", current_pane->current_index);

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
	Entry *current_entry = &current_pane->entries[current_pane->current_index];

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
			disable_raw_mode();
			s = open_file(current_entry->fullpath);
			enable_raw_mode();
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
	filesystem_event_quit();
	stop_watcher_threads();
	free(term.buffer);
	free(panes[Left].entries);
	free(panes[Right].entries);
	disable_raw_mode();
	exit(arg->i);
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
	//LOG("### REALLOC-> = %d", len);
	if ((p = realloc(p, len)) == NULL)
		die("realloc: %s\n", strerror(errno));
	return p;
}

static void
log_to_file(const char *function, int line, const char *format, ...)
{
	static int file_initialized = 0;
	pid_t pid = getpid();           // Get the current process ID
	pthread_t tid = pthread_self(); // Get the current thread ID

	if (!file_initialized) {
		remove("/tmp/sfm.log");
		file_initialized = 1;
	}

	FILE *file = fopen("/tmp/sfm.log", "a");
	if (file == NULL) {
		fprintf(stderr, "Failed to open log file");
		exit(EXIT_FAILURE);
	}

	time_t now;
	time(&now);
	char time_str[20];
	strftime(time_str, sizeof(time_str), "%F %R:%S", localtime(&now));

	// Write the log message with the specified format
	fprintf(file, "[%s] [PID: %d] [TID: %lu] %s() %d: ", time_str, pid, tid,
		function, line);

	va_list args;
	va_start(args, format);
	vfprintf(file, format, args);
	va_end(args);

	fprintf(file, "\n");

	fclose(file);
}

void
filesystem_event_init(void)
{
	panes[Left].watcher.inotify_fd = inotify_init();
	if (panes[Left].watcher.inotify_fd < 0) {
		LOG("inotify_init error for left pane");
		exit(EXIT_FAILURE);
	}

	panes[Right].watcher.inotify_fd = inotify_init();
	if (panes[Right].watcher.inotify_fd < 0) {
		LOG("inotify_init error for right pane");
		exit(EXIT_FAILURE);
	}

	add_watch(&panes[Left]);
	add_watch(&panes[Right]);
}

void
filesystem_event_quit(void)
{
	remove_watch(&panes[Left]);
	remove_watch(&panes[Right]);

	close(panes[Left].watcher.inotify_fd);
	panes[Left].watcher.inotify_fd = -1;

	close(panes[Right].watcher.inotify_fd);
	panes[Right].watcher.inotify_fd = -1;
}

void
start_watcher_threads(void)
{
	if (pthread_create(&panes[Left].watcher.watcher_thread, NULL,
		    watch_directory, (void *)&panes[Left].watcher) != 0) {
		LOG("pthread_create for left pane");
		exit(EXIT_FAILURE);
	}

	if (pthread_create(&panes[Right].watcher.watcher_thread, NULL,
		    watch_directory, (void *)&panes[Right].watcher) != 0) {
		LOG("pthread_create for right pane");
		exit(EXIT_FAILURE);
	}
}

void
stop_watcher_threads(void)
{
	pthread_cancel(panes[Left].watcher.watcher_thread);
	pthread_cancel(panes[Right].watcher.watcher_thread);

	pthread_join(panes[Left].watcher.watcher_thread, NULL);
	pthread_join(panes[Right].watcher.watcher_thread, NULL);
}

#if defined(__linux__)
void
add_watch(Pane *pane)
{
	pane->watcher.watch_descriptor =
		inotify_add_watch(pane->watcher.inotify_fd, pane->path,
			IN_MODIFY | IN_CREATE | IN_DELETE);
	if (pane->watcher.watch_descriptor < 0) {
		LOG("inotify_add_watch error");
		exit(EXIT_FAILURE);
	}
}

void
remove_watch(Pane *pane)
{
	if (inotify_rm_watch(pane->watcher.inotify_fd,
		    pane->watcher.watch_descriptor) < 0) {
		LOG("inotify_rm_watch error");
	}
}

void *
watch_directory(void *arg)
{
	Watcher *watcher = (Watcher *)arg;
	char buffer[EVENT_BUFFER_LENGTH];

	while (1) {
		int length =
			read(watcher->inotify_fd, buffer, EVENT_BUFFER_LENGTH);
		if (length < 0) {
			LOG("read error");
			break;
		}

		int i = 0;
		while (i < length) {
			struct inotify_event *event =
				(struct inotify_event *)&buffer[i];
			if (event->mask & (IN_MODIFY | IN_CREATE | IN_DELETE)) {
				if (event->wd ==
					panes[Left].watcher.watch_descriptor) {
					kill(main_pid, SIGUSR1);
				} else if (event->wd ==
					panes[Right].watcher.watch_descriptor) {
					kill(main_pid, SIGUSR2);
				}
			}
			i += EVENT_SIZE + event->len;
		}
	}

	pthread_exit(NULL);
}

#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
	defined(__APPLE__)

void
add_watch(Pane *pane)
{
	pane->watcher.directory_fd = open(pane->path, O_RDONLY);
	if (pane->watcher.directory_fd < 0) {
		LOG("open error");
		exit(EXIT_FAILURE);
	}

	pane->watcher.kq = kqueue();
	if (pane->watcher.kq == -1) {
		LOG("kqueue error");
		close(pane->watcher.directory_fd);
		exit(EXIT_FAILURE);
	}

	struct kevent change;
	EV_SET(&change, pane->watcher.directory_fd, EVFILT_VNODE,
		EV_ADD | EV_CLEAR,
		NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_LINK |
			NOTE_RENAME | NOTE_REVOKE,
		0, 0);

	if (kevent(pane->watcher.kq, &change, 1, NULL, 0, NULL) == -1) {
		LOG("kevent error");
		close(pane->watcher.directory_fd);
		exit(EXIT_FAILURE);
	}
}

void
remove_watch(Pane *pane)
{
	close(pane->watcher.directory_fd);
	pane->watcher.directory_fd = -1;
	close(pane->watcher.kq);
	pane->watcher.kq = -1;
}

void *
watch_directory(void *arg)
{
	Watcher *watcher = (Watcher *)arg;
	struct kevent event;

	while (1) {
		int nev = kevent(watcher->kq, NULL, 0, &event, 1, NULL);
		if (nev == -1) {
			LOG("kevent error");
			break;
		} else if (nev > 0) {
			if (event.filter == EVFILT_VNODE) {
				if (event.ident ==
					panes[Left].watcher.directory_fd) {
					kill(main_pid, SIGUSR1);
				} else if (event.ident ==
					panes[Right].watcher.directory_fd) {
					kill(main_pid, SIGUSR2);
				}
			}
		}
	}

	pthread_exit(NULL);
}

#endif

int
main(int argc, char *argv[])
{
#if defined(__OpenBSD__)
	if (pledge("cpath exec getpw proc rpath stdio tmppath tty wpath",
		    NULL) == -1)
		die("pledge");
#endif /* __OpenBSD__ */
	if (argc == 1)
		start();
	else if (argc == 2 && strncmp("-v", argv[1], 2) == 0)
		die("sfm-" VERSION);
	else
		die("usage: sfm [-v]");
	return 0;
}
