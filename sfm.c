/* See LICENSE file for copyright and license details. */

#if defined(__linux__)
	#define _GNU_SOURCE
	#include <sys/inotify.h>
	#include <sys/types.h>
	#define EV_BUF_LEN (1024 * (sizeof(struct inotify_event) + 16))
	#define OFF_T      "%ld"
	#define M_TIME     st_mtim

#elif defined(__APPLE__)
	#define _DARWIN_C_SOURCE
	#include <sys/types.h>
	#include <sys/time.h>
	#include <sys/event.h>

	#include <fcntl.h>
	#include <limits.h>
	#define OFF_T  "%lld"
	#define M_TIME st_mtimespec

#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
	#define __BSD_VISIBLE 1
	#include <sys/types.h>
	#include <sys/time.h>
	#include <sys/event.h>

	#include <fcntl.h>
	#include <limits.h>
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
char *editor[2] = { "vi", NULL };
char *shell[2] = { "/bin/sh", NULL };
char *home = "/";
static pid_t fork_pid, main_pid;
static char **selected_entries = NULL;
static int selected_count = 0;
static int mode;

static void
log_to_file(const char *func, int line, const char *format, ...)
{
	pid_t pid = getpid();
	FILE *logfile = fopen("/tmp/sfm.log", "a");
	if (logfile) {
		va_list args;
		va_start(args, format);
		fprintf(logfile, "%d-- [%s:%d] ", pid, func, line);
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
	term.buffer_size = (unsigned long)term.rows * term.cols * 4;
	term.buffer = ecalloc(term.buffer_size, sizeof(char));
	term.buffer_left = term.buffer_size;
	term.buffer_index = 0;
}

static void
enable_raw_mode(void)
{
	tcgetattr(STDIN_FILENO, &term.orig);
	term.newterm = term.orig;
	term.newterm.c_lflag &= ~(ECHO | ICANON | ISIG);
	term.newterm.c_iflag &= ~(IXON | ICRNL);
	term.newterm.c_oflag &= ~(OPOST);
	term.newterm.c_cflag |= (CS8);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &term.newterm);
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
	char *env_editor = NULL;
	char *env_shell = NULL;
	char *env_home = NULL;

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
		log_to_file(__func__, __LINE__, "SIGWINCH");
		termb_resize();
		break;
	case SIGUSR1:
		log_to_file(__func__, __LINE__, "SIGUSR1");
		set_pane_entries(&panes[Left]);
		update_screen();
		break;
	case SIGUSR2:
		log_to_file(__func__, __LINE__, "SIGUSR2");
		set_pane_entries(&panes[Right]);
		update_screen();
		break;
	default:
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
	panes[Left].offset = 0;

	strncpy(panes[Right].path, home, PATH_MAX - 1);
	panes[Right].entries = NULL;
	panes[Right].entry_count = 0;
	panes[Right].start_index = 0;
	panes[Right].current_index = 0;
	panes[Right].watcher.fd = -1;
	panes[Right].watcher.signal = SIGUSR2;
	panes[Right].offset = term.cols / 2;

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
	const struct dirent *entry;
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
	if (dir == NULL) {
		close(fd);
		print_status(color_err, strerror(errno));
		return;
	}

	pane->entry_count = 0;
	while ((entry = readdir(dir)) != NULL) {
		if (should_skip_entry(entry) == 0) {
			pane->entry_count++;
		}
	}

	pane->entries = ecalloc(pane->entry_count, sizeof(Entry));

	rewinddir(dir);

	i = 0;
	while ((entry = readdir(dir)) != NULL) {
		if (should_skip_entry(entry) == 1) {
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

		set_entry_color(&pane->entries[pane->start_index + i]);

		i++;
	}

	pane->entry_count = i;

	if (closedir(dir) < 0)
		die("closedir:");
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
get_fullpath(char *full_path, const char *first, const char *second)
{
	int ret;

	if (first[0] == '/' && first[1] == '\0')
		(void)snprintf(full_path, PATH_MAX, "/%s", second);

	ret = snprintf(full_path, PATH_MAX, "%s/%s", first, second);
	if (ret < 0)
		die(strerror(errno));
	if (ret >= PATH_MAX)
		die("Path exceeded maximum length");
}

static int
get_selected_paths(Pane *pane, char **result)
{
	int count = 0;

	for (int i = 0; i < pane->entry_count; i++) {
		if (pane->entries[i].selected) {
			result[count] = pane->entries[i].fullpath;
			count++;
		}
	}

	return count;
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
	// clear all except last line
	write(STDOUT_FILENO, "\x1b[F\x1b[A\x1b[999C\x1b[1J", 16);
	append_entries(&panes[Left]);
	append_entries(&panes[Right]);
	termb_write();
	write_entries_name();

	log_to_file(__func__, __LINE__, "err: (%d)", errno);
	if (mode == NormalMode && errno == 0)
		display_entry_details();
	else
		print_status(color_err, strerror(errno));
}

static void
disable_raw_mode(void)
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &term.orig);
	if (write(STDOUT_FILENO, "\x1b[?1049l", 8) < 0)
		die("write:");
}

static void
append_entries(Pane *pane)
{
	int i;
	int n = 0;
	size_t index = 0;
	size_t max_len = term.cols / 2;
	Entry entry;
	char *buffer;

	if (pane->entries == NULL) {
		return;
	}
	buffer = ecalloc(term.buffer_size, sizeof(char));
	termb_append("\x1b[2;1f", 6); // move to top left

	for (i = 0;
		i < term.rows - 2 && pane->start_index + i < pane->entry_count;
		i++) {

		if (pane->start_index + i >= pane->entry_count ||
			pane->entries == NULL) {
			continue;
		}

		entry = pane->entries[pane->start_index + i];

		/* selected entry */
		if (entry.selected == 1)
			entry.color = color_selected;

		/* current entry */
		if (pane == current_pane &&
			pane->start_index + i == pane->current_index) {
			entry.color.attr |= RVS;
		}

		// Format the entry with truncation and padding
		n = snprintf(buffer + index, term.buffer_size - index,
			"\x1b[%dG"
			"\x1b[%d;38;5;%d;48;5;%dm%-*.*s\x1b[0m\r\n",
			pane->offset, entry.color.attr, entry.color.fg,
			entry.color.bg, (int)max_len, (int)max_len, entry.name);
		if (n < 0)
			break;

		index += n;
	}

	termb_append(buffer, index);
	free(buffer);
	buffer = NULL;
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
		"\x1b[%d;38;5;%d;48;5;%dm" // set string colors
		"%s"
		"\x1b[0;0m", // reset colors
		term.rows, color.attr, color.fg, color.bg, buf);

	if (write(STDOUT_FILENO, result, result_len) < 0)
		die("write:");
}

static void
display_entry_details(void)
{
	char sz[FSIZE_MAX];
	char ur[USER_MAX];
	char gr[GROUP_MAX];
	char dt[DATETIME_MAX];
	char prm[PERMISSION_MAX];
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
set_entry_color(Entry *ent)
{
	if (ent->selected) {
		ent->color = color_selected;
		return;
	} else if (ent->matched) {
		ent->color = color_search;
		return;
	}

	switch (ent->st.st_mode & S_IFMT) {
	case S_IFREG:
		ent->color = color_file;
		if ((S_IXUSR | S_IXGRP | S_IXOTH) & ent->st.st_mode)
			ent->color = color_exec;
		break;
	case S_IFDIR:
		ent->color = color_dir;
		break;
	case S_IFLNK:
		ent->color = color_lnk;
		break;
	case S_IFBLK:
		ent->color = color_blk;
		break;
	case S_IFCHR:
		ent->color = color_chr;
		break;
	case S_IFIFO:
		ent->color = color_ifo;
		break;
	case S_IFSOCK:
		ent->color = color_sock;
		break;
	default:
		ent->color = color_other;
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
	size_t i = 0;
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

	for (i = 1; i < PERMISSION_MAX; i++) {
		buf[i] = (mode & (1 << (9 - i))) ? chars[i - 1] : '-';
	}
	buf[PERMISSION_MAX - 1] = '\0';
}

static void
get_file_size(char *buf, off_t size)
{
	char unit;
	int counter = 0;

	while (size >= 1024) {
		size >>= 10;
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

	if (snprintf(buf, FSIZE_MAX, OFF_T "%c", size, unit) < 0)
		print_status(color_err, strerror(errno));
}

static void
get_entry_owner(char *buf, const uid_t uid)
{
	const struct passwd *pw;

	pw = getpwuid(uid);
	if (pw == NULL) {
		snprintf(buf, USER_MAX, "%u", uid);
	} else {
		strncpy(buf, pw->pw_name, USER_MAX - 1);
		buf[GROUP_MAX - 1] = '\0';
	}
}

static void
get_entry_group(char *buf, const gid_t gid)
{
	const struct group *gr;

	gr = getgrgid(gid);
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
	int c;
	size_t index = 0;

	va_start(args, prompt);
	vsnprintf(msg, PROMPT_MAX, prompt, args);
	print_status(color_normal, msg);
	va_end(args);

	while (1) {
		c = getchar();

		switch (c) {
		case XK_ESC:
			display_entry_details();
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

static void
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
		ext = NULL;
	}

	if (rule_index < 0) {
		cmd.cmdv = editor;
		cmd.cmdc = 1;
		cmd.argv = &file;
		cmd.argc = 1;
		cmd.wait_exec = Wait;
	} else {
		cmd.cmdv = (char **)rules[rule_index].v;
		cmd.cmdc = rules[rule_index].vlen;
		cmd.argv = &file;
		cmd.argc = 1;
		cmd.wait_exec = rules[rule_index].wait_exec;
	}

	spawn(&cmd);
}

static char *
get_file_extension(const char *str)
{
	char *ext;
	const char *dot;

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
check_rule(const char *ex)
{
	size_t c, d;

	for (c = 0; c < LEN(rules); c++)
		for (d = 0; d < rules[c].exlen; d++)
			if (strncmp(rules[c].ext[d], ex, EXTENTION_MAX) == 0)
				return c;
	return -1;
}

static void
spawn(Command *cmd)
{
	int execvp_errno;

	if (cmd->wait_exec == Wait)
		disable_raw_mode();

	execvp_errno = execute_command(cmd);

	if (cmd->wait_exec == Wait) {
		enable_raw_mode();
		termb_resize();
	}

	switch (execvp_errno) {
	case 0:
		break;
	case ENOENT:
		print_status(color_err, "Command not found: %s", cmd->cmdv[0]);
		break;
	case EACCES:
		print_status(color_err, "Permission denied: %s", cmd->argv[0]);
		break;
	case E2BIG:
		print_status(
			color_err, "Argument list too long: %s", cmd->argv[0]);
		break;
	case EFAULT:
		print_status(color_err, "Bad address: %s", cmd->argv[0]);
		break;
	case EIO:
		print_status(color_err, "I/O error: %s", cmd->argv[0]);
		break;
	case ENOEXEC:
		print_status(color_err, "Exec format error: %s", cmd->argv[0]);
		break;
	case ENOMEM:
		print_status(color_err, "Out of memory: %s", cmd->argv[0]);
		break;
	case ENOTDIR:
		print_status(color_err, "Not a directory: %s", cmd->argv[0]);
		break;
	case ETXTBSY:
		print_status(color_err, "Text file busy: %s", cmd->argv[0]);
		break;
	case EPERM:
		print_status(
			color_err, "Operation not permitted: %s", cmd->argv[0]);
		break;
	case ELOOP:
		print_status(color_err,
			"Too many symbolic links encountered: %s",
			cmd->argv[0]);
		break;
	case ENAMETOOLONG:
		print_status(color_err, "File name too long: %s", cmd->argv[0]);
		break;
	case ENFILE:
		print_status(
			color_err, "File table overflow: %s", cmd->argv[0]);
		break;
	case ENODEV:
		print_status(color_err, "No such device: %s", cmd->argv[0]);
		break;
	case ENOLCK:
		print_status(color_err, "No locks available: %s", cmd->argv[0]);
		break;
	case ENOSYS:
		print_status(color_err, "Function not implemented: %s",
			cmd->argv[0]);
		break;
	case ENOTBLK:
		print_status(
			color_err, "Block device required: %s", cmd->argv[0]);
		break;
	case EISDIR:
		print_status(color_err, "Is a directory: %s", cmd->argv[0]);
		break;
	case EROFS:
		print_status(
			color_err, "Read-only file system: %s", cmd->argv[0]);
		break;
	case EMFILE:
		print_status(
			color_err, "Too many open files: %s", cmd->argv[0]);
		break;
	default:
		print_status(color_err, "execvp failed with errno: %d",
			execvp_errno);
		break;
	}

	errno = 0;
}

static int
execute_command(Command *cmd)
{
	size_t argc;
	char **argv;
	char log_command[99024];
	size_t pos;
	int wait_status;
	int exit_status = 0;
	int exit_errno = 0;

	argc = cmd->cmdc + cmd->argc + 2;
	argv = ecalloc(argc, sizeof(char *));

	memcpy(argv, cmd->cmdv, cmd->cmdc * sizeof(char *));
	memcpy(&argv[cmd->cmdc], cmd->argv, cmd->argc * sizeof(char *));

	argv[argc - 1] = NULL;

	// Construct the command string for logging
	log_command[0] = '\0'; // Initialize the string with null terminator
	pos = 0;
	for (size_t i = 0; i < argc - 1; ++i) {
		if (argv[i] != NULL) {
			int len = snprintf(log_command + pos,
				sizeof(log_command) - pos, "%s ", argv[i]);
			if (len < 0 || pos + len >= sizeof(log_command)) {
				break; // Avoid buffer overflow
			}
			pos += len;
		}
	}
	log_command[sizeof(log_command) - 1] = '\0'; // Ensure null-termination
	log_to_file(__func__, __LINE__, "exec = %s", log_command);

	fork_pid = fork();
	switch (fork_pid) {
	case -1:
		free(argv);
		argv = NULL;
		return -1;
	case 0:
		exit_status = execvp(argv[0], argv);
		if (exit_status < 0) {
			free(argv);
			argv = NULL;
		}
		exit(errno);
	default:
		waitpid(fork_pid, &wait_status, cmd->wait_exec);
		if (WIFEXITED(wait_status) && WEXITSTATUS(wait_status)) {
			exit_errno = WEXITSTATUS(wait_status);
		}
	}
	free(argv);
	argv = NULL;
	fork_pid = 0;
	return exit_errno;
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
write_entries_name(void)
{
	int half_cols = term.cols / 2;
	char result[term.cols + 100];

	int result_len = snprintf(result, sizeof(result),
		"\x1b[1;1H"                // Move cursor to top-left corner
		"\x1b[%d;38;5;%d;48;5;%dm" // Set colors for left pane
		"%-*.*s"                   // Left string with padding
		"\x1b[%d;38;5;%d;48;5;%dm" // Set colors for right pane
		"%-*.*s"                   // Right string with padding
		"\x1b[0m",                 // Reset colors
		color_panell.attr, color_panell.fg, color_panell.bg, half_cols,
		half_cols, panes[Left].path, color_panelr.attr, color_panelr.fg,
		color_panelr.bg, half_cols, half_cols, panes[Right].path);

	write(STDOUT_FILENO, result, result_len);
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

	get_fullpath(full_path, current_pane->path, file_name);

	fd = open(full_path, O_CREAT | O_EXCL, new_file_perm);
	if (fd < 0) {
		print_status(color_err, strerror(errno));
		return;
	}

	display_entry_details();
	close(fd);
}

static void
create_new_dir(const Arg *arg)
{
	char dir_name[NAME_MAX];
	char full_path[PATH_MAX];

	if (get_user_input(dir_name, sizeof(dir_name), "new directory: ") != 0)
		return;

	get_fullpath(full_path, current_pane->path, dir_name);

	if (mkdir(full_path, new_dir_perm) != 0)
		print_status(color_err, strerror(errno));
}

static void
copy_entries(const Arg *arg)
{
	selected_entries = ecalloc(current_pane->entry_count, sizeof(char *));
	selected_count = get_selected_paths(current_pane, selected_entries);

	if (selected_count < 1) {
		selected_entries[0] =
			current_pane->entries[current_pane->current_index]
				.fullpath;
		selected_count = 1;
	}

	if (selected_count < 1) {
		print_status(color_warn, "No entries selected.");
	} else {
		print_status(color_normal, "Entries copied.");
	}

	mode = NormalMode;
}

static void
delete_entry(const Arg *arg)
{
	Command cmd;
	char confirmation[4];

	if (current_pane->entry_count <= 0 ||
		current_pane->current_index >= current_pane->entry_count) {
		print_status(color_err, "No entry selected or invalid index.");
		return;
	}

	selected_entries = ecalloc(current_pane->entry_count, sizeof(char *));
	selected_count = get_selected_paths(current_pane, selected_entries);

	if (selected_count < 1) {
		selected_entries[0] =
			current_pane->entries[current_pane->current_index]
				.fullpath;
		selected_count = 1;
	}

	log_to_file(__func__, __LINE__, "SELECTED COUNT = %d", selected_count);
	log_to_file(__func__, __LINE__, "SELECTED = %s", selected_entries[0]);

	/* confirmation */
	if (get_user_input(confirmation, sizeof(confirmation), "Delete (%s)?",
		    delconf) < 0) {
		free(selected_entries);
		selected_entries = NULL;
		selected_count = 0;
		return;
	}
	if (strncmp(confirmation, delconf, delconf_len) != 0) {
		print_status(color_warn, "Deletion aborted.");
		free(selected_entries);
		selected_entries = NULL;
		selected_count = 0;
		return;
	}

	cmd.cmdv = (char **)rm_cmd;
	cmd.cmdc = rm_cmd_len;
	cmd.argv = selected_entries;
	cmd.argc = selected_count;
	cmd.wait_exec = DontWait;

	spawn(&cmd);

	free(selected_entries);
	selected_entries = NULL;
	selected_count = 0;
	mode = NormalMode;
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
update_entry(Pane *pane, int index)
{
	int err;
	size_t max_len;
	Entry entry;
	char buffer[PATH_MAX];
	int pos;

	if (index < 0 || index >= pane->entry_count)
		return;

	max_len = term.cols / 2;
	entry = pane->entries[index];
	pos = index - pane->start_index;

	if (pane->entries[index].selected == 1)
		entry.color = color_selected;

	if (pane == current_pane && index == current_pane->current_index) {
		entry.color.attr |= RVS;
	}

	err = snprintf(buffer, sizeof(buffer),
		"\x1b[%d;%dH" // Move cursor to the entry position
		"\x1b[%d;38;5;%d;48;5;%dm%-*.*s\x1b[0m",
		pos + 2, pane->offset, entry.color.attr, entry.color.fg,
		entry.color.bg, (int)max_len - 1, (int)max_len, entry.name);

	if (err < 0)
		print_status(color_err, strerror(errno));

	write(STDOUT_FILENO, buffer, strlen(buffer));
}

static void
move_cursor(const Arg *arg)
{
	int new_start_index;
	int old_index;

	if (current_pane->entry_count == 0)
		return;

	old_index = current_pane->current_index;
	current_pane->current_index += arg->i;

	if (current_pane->current_index < 0) {
		current_pane->current_index = 0;
	} else if (current_pane->current_index >= current_pane->entry_count) {
		current_pane->current_index = current_pane->entry_count - 1;
	}

	new_start_index = current_pane->start_index;
	if (current_pane->current_index < current_pane->start_index) {
		current_pane->start_index = current_pane->current_index;
	} else if (current_pane->current_index >=
		current_pane->start_index + term.rows - 2) {
		current_pane->start_index =
			current_pane->current_index - (term.rows - 3);
	}

	if (new_start_index != current_pane->start_index) {
		update_screen();
	} else {
		// Update only the necessary entries
		if (old_index != current_pane->current_index) {
			update_entry(current_pane, old_index);
			update_entry(current_pane, current_pane->current_index);
		}
	}

	if (mode == VisualMode)
		select_cur_entry(&(Arg) { .i = Select });
}

static void
move_top(const Arg *arg)
{
	current_pane->current_index = 0;
	current_pane->start_index = 0;
	update_screen();
}

static void
move_entries(const Arg *arg)
{
	if (selected_count <= 0) {
		print_status(color_warn, "No entries copied.");
		log_to_file(__func__, __LINE__, "No entries copied.");
		return;
	}

	char **argv = ecalloc(selected_count + 2, PATH_MAX);
	for (int i = 0; i < selected_count; i++) {
		argv[i] = selected_entries[i];
	}
	argv[selected_count] = current_pane->path; // Destination path
	argv[selected_count + 1] = NULL;

	Command cmd;
	cmd.cmdv = (char **)mv_cmd;
	cmd.cmdc = mv_cmd_len;
	cmd.argv = argv;
	cmd.argc = selected_count + 1;
	cmd.wait_exec = DontWait;

	print_status(color_normal, "Moving...");
	spawn(&cmd);
	print_status(color_normal, "Moved...");

	free(selected_entries);
	selected_entries = NULL;
	selected_count = 0;
	free(argv);
	argv = NULL;
}

static void
open_entry(const Arg *arg)
{
	if (current_pane->entry_count < 1)
		return;

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
	case 1: /* not a directory open file */
		if (S_ISREG(current_entry->st.st_mode)) {
			errno = 0; /* check_dir errno */
			open_file(current_entry->fullpath);
		}
		break;
	case -1: /* failed to open directory */
		print_status(color_err, strerror(errno));
	}
}

static void
paste_entries(const Arg *arg)
{
	if (selected_count <= 0) {
		print_status(color_warn, "No entries copied");
		log_to_file(__func__, __LINE__, "No entries copied.");
		return;
	}

	char **argv = ecalloc(selected_count + 2, sizeof(char *));
	for (int i = 0; i < selected_count; i++) {
		argv[i] = selected_entries[i];
	}
	argv[selected_count] = current_pane->path; // Destination path
	argv[selected_count + 1] = NULL;

	Command cmd;
	cmd.cmdv = (char **)cp_cmd;
	cmd.cmdc = cp_cmd_len;
	cmd.argv = argv;
	cmd.argc = selected_count + 1;
	cmd.wait_exec = DontWait;

	print_status(color_normal, "Pasting...");
	spawn(&cmd);
	print_status(color_normal, "Pasted...");

	free(selected_entries);
	selected_entries = NULL;
	selected_count = 0;
	free(argv);
	argv = NULL;
}

static void
quit(const Arg *arg)
{
	cancel_search_highlight();
	cleanup_filesystem_events();
	if (selected_entries != NULL)
		free(selected_entries);
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
select_entry(Entry *entry, int s)
{
	entry->selected =
		(s == InvertSelection) ? !entry->selected : (s == Select);
}

static void
select_cur_entry(const Arg *arg)
{
	select_entry(
		&current_pane->entries[current_pane->current_index], arg->i);
	update_entry(current_pane, current_pane->current_index);
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

	log_to_file(__func__, __LINE__, "Event handler started for path: %s",
		pane->path);

	pane->watcher.fd = inotify_init();
	if (pane->watcher.fd < 0) {
		log_to_file(__func__, __LINE__,
			"Error initializing inotify: %s", strerror(errno));
		die("inotify_init:");
		pthread_exit(NULL);
	}

	add_watch(pane);

	while (1) {
		length = read(pane->watcher.fd, buffer, EV_BUF_LEN);
		if (length <= 0) {
			log_to_file(__func__, __LINE__,
				"Error reading inotify event: %s",
				strerror(errno));
			die("read:");
			break;
		}

		if (length < (int)sizeof(struct inotify_event)) {
			log_to_file(__func__, __LINE__,
				"Incomplete inotify event read");
			die("read:");
			break;
		}

		i = 0;
		while (i < length) {
			struct inotify_event *event =
				(struct inotify_event *)&buffer[i];
			if (event->mask) {
				log_to_file(__func__, __LINE__,
					"Inotify event detected: mask=%u, len=%u, name=%s",
					event->mask, event->len,
					event->len ? event->name : "");
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
	pane->watcher.descriptor = inotify_add_watch(
		pane->watcher.fd, pane->path, IN_CREATE | IN_DELETE);
	if (pane->watcher.descriptor < 0) {
		log_to_file(__func__, __LINE__,
			"Error adding inotify watch: %s", strerror(errno));
		die("inotify_add_watch:");
	}
	log_to_file(__func__, __LINE__, "Added inotify watch for path: %s",
		pane->path);
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

static void
visual_mode(const Arg *arg)
{
	if (current_pane->entry_count <= 0) {
		print_status(color_warn, "No entries to select.");
		return;
	}

	if (mode == VisualMode) {
		normal_mode(&(Arg) { 0 });
	} else {
		mode = VisualMode;
		select_cur_entry(&(Arg) { .i = Select });
		print_status(color_normal, " --VISUAL-- ");
	}

	update_screen();
}

static void
normal_mode(const Arg *arg)
{
	if (mode == SearchMode)
		cancel_search_highlight();
	mode = NormalMode;
	display_entry_details();
}

void
select_all(const Arg *arg)
{
	if (current_pane->entry_count <= 0) {
		print_status(color_warn, "No entries to select.");
		return;
	}

	for (int i = 0; i < current_pane->entry_count; i++) {
		select_entry(&current_pane->entries[i], arg->i);
	}

	update_screen();
}

static void
start_search(const Arg *arg)
{
	if (mode == SearchMode) {
		cancel_search_highlight();
		mode = NormalMode;
		update_screen();
		return;
	}

	mode = SearchMode;
	memset(current_pane->search_term, 0, NAME_MAX);

	if (get_user_input(current_pane->search_term, NAME_MAX, "Search: ") !=
		0) {
		cancel_search_highlight();
		mode = NormalMode;
		update_screen();
		return;
	}

	update_search_highlight(current_pane->search_term);
	mode = NormalMode;
	current_pane->current_match = -1;
	update_screen();
}

static void
update_search_highlight(const char *search_term)
{
	if (current_pane->matched_indices != NULL) {
		free(current_pane->matched_indices);
		current_pane->matched_indices = NULL;
		current_pane->matched_count = 0;
	}

	current_pane->matched_indices =
		(int *)ecalloc(current_pane->entry_count, sizeof(int));

	for (int i = 0; i < current_pane->entry_count; i++) {
		if (strcasestr(current_pane->entries[i].name, search_term) !=
			NULL) {
			current_pane->entries[i].matched = 1;
			current_pane->matched_indices
				[current_pane->matched_count++] = i;
		} else {
			current_pane->entries[i].matched = 0;
		}
		set_entry_color(&current_pane->entries[i]);
	}
	update_screen();
}

static void
cancel_search_highlight(void)
{
	if (current_pane->matched_indices == NULL)
		return;

	for (int i = 0; i < current_pane->entry_count; i++) {
		set_entry_color(&current_pane->entries[i]);
	}
	if (current_pane->matched_indices != NULL) {
		free(current_pane->matched_indices);
		current_pane->matched_indices = NULL;
	}
	current_pane->matched_count = 0;
	current_pane->current_match = -1;
}

static void
move_to_match(const Arg *arg)
{
	if (current_pane->matched_count == 0) {
		print_status(color_warn, "No matches found.");
		return;
	}

	if (arg->i == NextMatch) {
		current_pane->current_match =
			(current_pane->current_match + 1) %
			current_pane->matched_count;
	} else if (arg->i == PrevMatch) {
		current_pane->current_match =
			(current_pane->current_match - 1 +
				current_pane->matched_count) %
			current_pane->matched_count;
	}

	current_pane->current_index =
		current_pane->matched_indices[current_pane->current_match];

	if (current_pane->current_index < current_pane->start_index ||
		current_pane->current_index >=
			current_pane->start_index + term.rows - 2) {
		current_pane->start_index =
			current_pane->current_index - (term.rows - 2) / 2;
		if (current_pane->start_index < 0) {
			current_pane->start_index = 0;
		}
	}

	update_screen();
}

int
main(int argc, const char *argv[])
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
		mode = NormalMode;
		init_term();
		enable_raw_mode();
		get_env();
		set_panes();
		start_signal();
		log_to_file(__func__, __LINE__, "start");

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
