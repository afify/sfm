/* See LICENSE file for copyright and license details. */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/inotify.h>
#define INOTIFY
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
defined(__APPLE__)
#define KQUEUE
#include <sys/event.h>
#endif /* filesystem events */

#include "termbox.h"
#include "util.h"

/* macros */
#define MAX_P       4095
#define MAX_N       255
#define MAX_USRI    32
#define EVENTS      32
#define EV_BUF_LEN  (EVENTS * (sizeof(struct inotify_event) + MAX_N + 1))

/* enums */
enum { AskDel, DAskDel }; /* delete directory */

/* typedef */
typedef struct {
	char name[MAX_N];
	char full[MAX_P];
	off_t size;
	mode_t mode;
	time_t td;
	uid_t user;
	gid_t group;
} Entry;

typedef struct {
	char dirn[MAX_P];     // directory name
	char high_dir[MAX_P]; // highlighted_dir fullpath
	int dirx;             // position of title
	Entry *direntr;
	size_t hdir;          // highlighted_dir
	size_t dirc;          // total files in dir
	uint16_t dir_bg;
	uint16_t dir_fg;
} Pane;

typedef struct {
	char key;
	char path[MAX_P];
} Bookmark;

typedef struct {
	char *soft;
	const char **ext;
	size_t len;
} Rule;

#include "config.h"

/* function declarations */
static void print_tb(const char*, int, int, uint16_t, uint16_t);
static void printf_tb(int, int, uint16_t, uint16_t, const char*, ...);
static void print_status(uint16_t, uint16_t, const char*, ...);
static void print_xstatus(char, int);
static void print_error(char*);
static void print_prompt(char*);
static void clear(int, int, int, uint16_t);
static void clear_status(void);
static void clear_pane(int);
static char *get_extentions(char*);
static char *get_full_path(char*, char*);
static char *get_parent(char*);
static char *get_file_info(Entry*);
static char *get_file_size(off_t);
static void get_dir_size(char*, size_t*);
static char *get_file_date_time(time_t);
static char *get_file_userowner(uid_t, size_t);
static char *get_file_groupowner(gid_t, size_t);
static char *get_file_perm(mode_t);
static int create_new_dir(char*, char*);
static int create_new_file(char*, char*);
static int delete_ent(char *fullpath);
static int delete_file(char*);
static int delete_dir(char*, int);
static int check_dir(char*);
static mode_t chech_execf(mode_t mode);
static int open_files(char*);
static int sort_name(const void *const, const void *const);
static void float_to_string(float, char*);
static ssize_t findbm(char);
static int get_user_input(char*, size_t, char*);
static void print_col(Entry*, size_t, size_t, size_t, int, int);
static size_t scroll(size_t, size_t, size_t);
static void start_ev(Pane*, Pane*);
static void handel_press(struct tb_event*, Pane*);
static void outdir_press(struct tb_event*, Pane*);
static void indir_press(struct tb_event*, Pane*);
static void refresh_pane(Pane*);
static int listdir(Pane*, char*);
static void t_resize(Pane*, Pane*);
static int set_panes(Pane*, Pane*, int);
static void draw_frame(void);
static int start(void);

/* global variables */
static int parent_row = 1; // FIX
static const uint32_t INOTIFY_MASK = IN_CREATE | IN_DELETE | IN_DELETE_SELF \
	| IN_MODIFY | IN_MOVE_SELF | IN_MOVED_FROM | IN_MOVED_TO;

/* function implementations */
static void
print_tb(const char *str, int x, int y, uint16_t fg, uint16_t bg)
{
	while (*str != '\0') {
		uint32_t uni = 0;
		str += tb_utf8_char_to_unicode(&uni, str);
		tb_change_cell(x, y, uni, fg, bg);
		x++;
	}
}

static void
printf_tb(int x, int y, uint16_t fg, uint16_t bg, const char *fmt, ...)
{
	char buf[4096];
	va_list vl;
	va_start(vl, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, vl);
	va_end(vl);
	print_tb(buf, x, y, fg, bg);
}

static void
print_status(uint16_t fg, uint16_t bg, const char *fmt, ...)
{
	int height;
	height = tb_height();

	char buf[256];
	va_list vl;
	va_start(vl, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, vl);
	va_end(vl);
	clear_status();
	print_tb(buf, 2, height-2, fg, bg);

}

static void
print_xstatus(char c, int x)
{
	int height;
	uint32_t uni = 0;
	height = tb_height();
	(void)tb_utf8_char_to_unicode(&uni, &c);
	tb_change_cell(x, height-2, uni,  TB_DEFAULT, status_b);
}

static void
print_error(char *errmsg)
{
	print_status(serr_f, serr_b, errmsg);
}

static void
print_prompt(char *prompt)
{
	print_status(sprompt_f, sprompt_b, prompt);
}

static void
clear(int sx, int ex, int y, uint16_t bg)
{
	/* clear line from to */
	/* x = line number  vertical */
	/* y = column number horizontal */
	int i;
	for (i = sx; i < ex; i++) {
		tb_change_cell(i, y, 0x0000, TB_DEFAULT, bg);
	}

}

static void
clear_status(void)
{
	int width, height;
	width = tb_width();
	height = tb_height();
	clear(1, width-1, height-2, status_b);
}

static void
clear_pane(int pane)
{
	int i, x, ex, y, width, height;
	width = tb_width();
	height = tb_height();
	x = 0, y = 0, i = 0, ex = 0;

	if (pane == 2){
		x = 2;
		ex = (width/2) - 1;
	} else if (pane == (width/2) + 2) {
		x = (width/2) + 1;
		ex = width -1;
	}

	while (i < height-2) {
		clear(x, ex, y, TB_DEFAULT);
		i++;
		y++;
	}
	/* draw top line */
	for (y = x; y < ex ; ++y) {
		tb_change_cell(y, 0, u_hl, frame_f, frame_b);
	}

}

static char *
get_extentions(char *str)
{
	char *ext;
	char dot;
	size_t counter, len, i;

	dot = '.';
	counter = 0;
	len = strlen(str);

	for (i = len-1; i > 0; i--) {
		if (str[i] == dot) {
			break;
		} else {
			counter++;
		}
	}

	ext = ecalloc(counter+1, sizeof(char));
	strncpy(ext, &str[len-counter], counter);
	return ext;
}

static char *
get_full_path(char *first, char *second)
{
	char *full_path;
	size_t full_path_len;

	full_path_len = strlen(first) + strlen(second) + 2;
	full_path = ecalloc(full_path_len, sizeof(char));

	if (strcmp(first, "/") == 0) {
		(void)snprintf(full_path, full_path_len, "/%s", second);

	} else {
		(void)snprintf(
			full_path, full_path_len, "%s/%s", first, second);
	}

	return full_path;
}

static char *
get_parent(char *dir)
{
	char *parent;
	char dot;
	size_t counter, len, i;

	dot = '/';
	counter = 0;
	len = strlen(dir);

	for (i = len-1; i > 0; i--) {
		if (dir[i] == dot) {
			break;
		} else {
			counter++;
		}
	}

	i = len - counter - 1;
	if (i == 0)
		i = len - counter;
	parent = ecalloc(i+1, sizeof(char));
	strncpy(parent, dir, i);

	return parent;
}

static char *
get_file_info(Entry *cursor)
{
	char *size, *result, *ur, *gr, *td, *perm;
	size_t *lsize;
	size_t size_len = (size_t)9;
	size_t perm_len = (size_t)11;
	size_t ur_len = (size_t)32;
	size_t gr_len = (size_t)32;
	size_t td_len = (size_t)14;
	size_t result_chars = size_len + perm_len + ur_len + gr_len + td_len;
	result = ecalloc(result_chars, sizeof(char));
	lsize = ecalloc(40, sizeof(size_t));

	if (show_perm == 1)
	{
		perm = get_file_perm(cursor->mode);
		strncpy(result, perm, perm_len);
		strcat(result, " ");
		free(perm);
	}

	if (show_ug == 1) {
		ur = get_file_userowner(cursor->user, ur_len);
		gr = get_file_groupowner(cursor->group, gr_len);
		strncat(result, ur, ur_len);
		strcat(result, ":");
		strncat(result, gr, gr_len);
		strcat(result, " ");
		free(ur);
		free(gr);
	}

	if (show_dt == 1)
	{
		td = get_file_date_time(cursor->td);
		strncat(result, td, td_len);
		strcat(result, " ");
		free(td);
	}

	if (show_size == 1 && S_ISREG(cursor->mode)) {
		size = get_file_size(cursor->size);
		strncat(result, size, size_len);
		free(size);
	}

	free(lsize);
	return result;
}

static void
get_dir_size(char *fullpath, size_t *fullsize)
{
	DIR *dir;
	char *ent_full;
	mode_t mode;
	struct dirent *entry;
	struct stat status;

	dir = opendir(fullpath);
	if (dir == NULL)
	{
		return;
	}

	while ((entry = readdir(dir)) != 0)
	{
		if ((strcmp(entry->d_name, ".") == 0 ||
		strcmp(entry->d_name, "..") == 0))
			continue;

		ent_full = get_full_path(fullpath, entry->d_name);
		if (lstat(ent_full, &status) == 0) {
			mode = status.st_mode;
			if (S_ISDIR(mode)) {
				get_dir_size(ent_full, fullsize);
				free(ent_full);
			} else {

				*fullsize += status.st_size;
				free(ent_full);
			}
		}
	}

	closedir(dir);
}

static char *
get_file_size(off_t size)
{

	/* need to be freed */
	char *Rsize;
	float lsize;
	int counter;
	counter = 0;

	Rsize = ecalloc((size_t)10, sizeof(char));
	lsize = (float)size;

	while (lsize >= 1000.0)
	{
		lsize /= 1024.0;
		++counter;
	}

	float_to_string(lsize, Rsize);

	switch (counter)
	{
	case 0:
		strcat(Rsize, "B");
		break;
	case 1:
		strcat(Rsize, "K");
		break;
	case 2:
		strcat(Rsize, "M");
		break;
	case 3:
		strcat(Rsize, "G");
		break;
	case 4:
		strcat(Rsize, "T");
		break;
	}

	return Rsize;
}

static char *
get_file_date_time(time_t status)
{
	char *result;
	struct tm lt;
	size_t result_len;

	result_len = (size_t)14;
	result = ecalloc(result_len, sizeof(char));
	localtime_r(&status, &lt);

	if (strftime(result, result_len, dt_fmt, &lt) != sizeof(dt_fmt)-1) {
		free(result);
		return NULL;
	}

	return result;
}

static char *
get_file_userowner(uid_t status, size_t len)
{
	char *result;
	struct passwd *pw;

	result = ecalloc(len, sizeof(char));
	pw = getpwuid(status);
	if (pw == NULL)
		(void)snprintf(result, len-1, "%d", (int)status);
	else
		strncpy(result, pw->pw_name, len-1);

	return result;
}

static char *
get_file_groupowner(gid_t status, size_t len)
{
	char *result;
	struct group *gr;

	result = ecalloc(len, sizeof(char));
	gr = getgrgid(status);
	if (gr == NULL)
		(void)snprintf(result, len-1, "%d", (int)status);
	else
		strncpy(result, gr->gr_name, len-1);

	return result;
}

static char *
get_file_perm(mode_t mode)
{
	char *buf;
	size_t i;

	const char chars[] = "rwxrwxrwx";
	buf = ecalloc((size_t)11, sizeof(char));

	if(S_ISDIR(mode))
		buf[0] = 'd';
	else if(S_ISREG(mode))
		buf[0] = '-';
	else if(S_ISLNK(mode))
		buf[0] = 'l';
	else if(S_ISBLK(mode))
		buf[0] = 'b';
	else if(S_ISCHR(mode))
		buf[0] = 'c';
	else if(S_ISFIFO(mode))
		buf[0] = 'p';
	else if(S_ISSOCK(mode))
		buf[0] = 's';
	else
		buf[0] = '?';

	for (i = 1; i < 10; i++) {
		buf[i] = (mode & (1 << (9-i))) ? chars[i-1] : '-';
	}
	buf[10] = '\0';

	return buf;
}

static int
create_new_dir(char *cwd, char *user_input)
{
	char *path;
	path = ecalloc(strlen(cwd)+strlen(user_input)+2, sizeof(char));
	strcpy(path, cwd);
	strcat(path, "/");
	strcat(path, user_input);

	if(mkdir(path, new_dir_perm) < 0) {
		free(path);
		return -1;
	}

	free(path);
	return 0;
}

static int
create_new_file(char *cwd, char *user_input)
{
	int rf;
	char *path;
	path = ecalloc(strlen(cwd)+strlen(user_input)+2, sizeof(char));
	strcpy(path, cwd);
	strcat(path, "/");
	strcat(path, user_input);

	rf = open(path, O_CREAT|O_EXCL, S_IRUSR|S_IWUSR);
	free(path);
	if (rf < 0)
		return -1;

	if (close(rf) < 0)
		return -1;
	return 0;
}

static int
delete_ent(char *fullpath)
{
	struct stat status;
	mode_t mode;

	if (lstat(fullpath, &status) < 0)
		return -1;

	mode = status.st_mode;
	if(S_ISDIR(mode)) {
		return delete_dir(fullpath, (int)AskDel);
	} else {
		return delete_file(fullpath);
	}

}

static int
delete_dir(char *fullpath, int delchoice)
{
	if (delchoice == (int)AskDel) {
		char *confirmation;
		confirmation = ecalloc((size_t)2, sizeof(char));
		if ((get_user_input(
			confirmation, (size_t)2,"delete directory (Y) ?") < 0) ||
			(strcmp(confirmation, "Y") != 0)) {
			free(confirmation);
			return 1; /* canceled by user or wrong confirmation */
		}
		free(confirmation);
	}

	if (rmdir(fullpath) == 0)
		return 0; /* empty directory */

	DIR *dir;
	char *ent_full;
	mode_t mode;
	struct dirent *entry;
	struct stat status;

	dir = opendir(fullpath);
	if (dir == NULL) {
		return -1;
	}

	while ((entry = readdir(dir)) != 0) {
		if ((strcmp(entry->d_name, ".") == 0  ||
			strcmp(entry->d_name, "..") == 0))
			continue;

		ent_full = get_full_path(fullpath, entry->d_name);
		if (lstat(ent_full, &status) == 0) {
			mode = status.st_mode;
			if (S_ISDIR(mode)) {
				if (delete_dir(ent_full, (int)DAskDel) < 0) {
					free(ent_full);
					return -1;
				}
			} else if (S_ISREG(mode)) {
				if (unlink(ent_full) < 0) {
					free(ent_full);
					return -1;
				}
			}
		}
		free(ent_full);
	}

	print_status(status_f, status_b, "gotit");
	if (closedir(dir) < 0)
		return -1;

	return rmdir(fullpath); /* directory after delete all entries */
}

static int
delete_file(char *fullpath)
{
	char *confirmation;
	confirmation = ecalloc((size_t)2, sizeof(char));

	if ((get_user_input(confirmation, (size_t)2, "delete file (Y) ?") < 0) ||
		(strcmp(confirmation, "Y") != 0)) {
		free(confirmation);
		return 1; /* canceled by user or wrong confirmation */
	}

	free(confirmation);
	return unlink(fullpath);
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

static mode_t
chech_execf(mode_t mode)
{
	if (S_ISREG(mode))
		return (((S_IXUSR | S_IXGRP | S_IXOTH) & mode));
	return 0;
}

static int
open_files(char *filename)
{
	// TODO
	/* open editor in other window */
	/* wait option */
	char *editor, *file_ex, *software, *term;
	int status;
	size_t d, c;
	pid_t pid, r;

	editor = getenv("EDITOR");
	term = getenv("TERM");
	file_ex = get_extentions(filename);
	software = NULL;

	/* find software in rules */
	for (c = 0; c < LEN(rules); c++) {
		for (d = 0; d < rules[c].len; d++){
			if (strcmp(rules[c].ext[d], file_ex) == 0) {
				software = rules[c].soft;
			}
		}
	}

	/* default softwares */
	if (term == NULL)
		term = "xterm-256color";
	if (editor == NULL)
		editor = "vi";
	if (software == NULL)
		software = editor;

	free(file_ex);
	char *filex[] = {software, filename, NULL};
	tb_shutdown();
	pid = fork();

	switch (pid) {
	case -1:
		return -1;
	case 0:
		(void)execvp(filex[0], filex);
		exit(EXIT_SUCCESS);
	default:
		while ((r = waitpid(pid, &status, 0)) == (pid_t)-1 && errno == EINTR)
			continue;
		if (r == (pid_t)-1)
			return -1;
		if ((WIFEXITED(status) != 0) && (WEXITSTATUS(status) != 0))
			return -1;
	}

	return 0;
}

static int
sort_name(const void *const A, const void *const B)
{
	int result;
	mode_t data1 = (*(Entry*) A).mode;
	mode_t data2 = (*(Entry*) B).mode;

	if (data1 < data2) {
		return -1;
	} else if (data1 == data2) {
		result = strcmp((*(Entry*) A).name,(*(Entry*) B).name);
		return result;
	} else {
		return 1;
	}
}

static void
float_to_string(float f, char *r)
{
	int length, length2, i, number, position, tenth; /* length is size of decimal part, length2 is size of tenth part */
	float number2;

	f = (float)(int)(f * 10) / 10;

	number2 = f;
	number = (int)f;
	length2 = 0;
	tenth = 1;

	/* Calculate length2 tenth part */
	while ((number2 - (float)number) != 0.0 && !((number2 - (float)number) < 0.0))
	{
		tenth *= 10.0;
		number2 = f * (float)tenth;
		number = (int)number2;

		length2++;
	}

	/* Calculate length decimal part */
	for (length = (f > 1.0) ? 0 : 1; f > 1.0; length++)
		f /= 10.0;

	position = length;
	length = length + 1 + length2;
	number = (int)number2;

	if (length2 > 0)
	{
		for (i = length; i >= 0; i--)
		{
			if (i == (length))
				r[i] = '\0';
			else if (i == (position))
				r[i] = '.';
			else
			{
				r[i] = (char)(number % 10) + '0';
				number /= 10;
			}
		}
	}
	else
	{
		length--;
		for (i = length; i >= 0; i--)
		{
			if (i == (length))
				r[i] = '\0';
			else
			{
				r[i] = (char)(number % 10) + '0';
				number /= 10;
			}
		}
	}
}

static ssize_t
findbm(char event)
{
	ssize_t i;

	for (i = 0; i < (ssize_t)LEN(bmarks); i++) {
		if (event == bmarks[i].key) {
			if (check_dir(bmarks[i].path) != 0) {
				print_error(strerror(errno));
				return -1;
			}
			return i;
		}
	}
	return -1;
}

static int
get_user_input(char *out, size_t sout, char *prompt)
{
	int height = tb_height();
	size_t startat;
	struct tb_event fev;
	size_t counter = (size_t)1;
	char empty = ' ';
	int x = 0;

	clear_status();
	startat = strlen(prompt) + 3;
	print_prompt(prompt);
	tb_set_cursor((int)(startat + 1), height-2);
	tb_present();

	while (tb_poll_event(&fev) != 0) {
		switch (fev.type) {
		case TB_EVENT_KEY:
			if (fev.key == (uint16_t)TB_KEY_ESC) {
				tb_set_cursor(-1, -1);
				clear_status();
				return -1;
			}

			if (fev.key == (uint16_t)TB_KEY_BACKSPACE ||
				fev.key == (uint16_t)TB_KEY_BACKSPACE2) {
				if (BETWEEN(counter, (size_t)2, sout)) {
					out[x-1] = '\0';
					counter--;
					x--;
					print_xstatus(empty, startat + counter);
					tb_set_cursor(
						(int)startat + counter, height - 2);
				}

			} else if (fev.key == (uint16_t)TB_KEY_ENTER) {
				tb_set_cursor(-1, -1);
				out[counter-1] = '\0';
				return 0;

			} else {
				if (counter < sout) {
					print_xstatus((char)fev.ch, (int)(startat+counter));
					out[x] = (char)fev.ch;
					tb_set_cursor((int)(startat + counter + 1),height-2);
					counter++;
					x++;
				}
			}

			tb_present();
			break;

		default:
			return -1;
		}
	}

	return -1;

}

static void
print_col(Entry *entry, size_t hdir, size_t x, size_t y, int dyn_y, int width)
{
	uint16_t bg, fg;
	char buf[MAX_P];
	char lnk_full[MAX_P];
	char *result;

	bg = file_b;
	fg = file_f;

	result = entry->name;
	if (S_ISDIR(entry->mode)) {
		bg = dir_b;
		fg = dir_f;
	} else if (S_ISLNK(entry->mode) && realpath(entry->full, buf) != NULL) {
		strncpy(lnk_full, entry->name, MAX_N);
		strcat(lnk_full, " -> ");
		strncat(lnk_full, buf, MAX_N);
		bg = other_b;
		fg = other_f;
		result = lnk_full;
	}

	/* highlight executable files */
	if (chech_execf(entry->mode) > 0)
		fg = exec_f;

	/* highlighted (cursor) */
	if (y + dyn_y == hdir) {
		bg = bg | TB_REVERSE;
		fg = fg | TB_REVERSE | TB_BOLD;
	}

	/* print each element in directory */
	printf_tb((int)x, (int)y, fg, bg, "%*.*s", ~width, width, result);

}

static size_t
scroll(size_t height, size_t dirc, size_t hdir)
{
	size_t result, limit;

	result = 0;
	limit = (height -1) / 2;
	if (dirc > height - 1) {
		if (hdir < limit){
			result = 0;
		} else if (hdir > dirc - limit) {
			result = dirc - height + 1;
		} else {
			result = hdir - limit;
		}
	}
	return result;
}

static void
start_ev(Pane *pane_l, Pane *pane_r)
{
	struct tb_event ev;
	int current_pane = 0;

	while (tb_poll_event(&ev) != 0) {
		switch (ev.type) {
		case TB_EVENT_KEY:
			if (ev.ch == 'q') {
				free(pane_l->direntr);
				free(pane_r->direntr);
				tb_shutdown();
				exit(EXIT_SUCCESS);
			}

			if (ev.key == TB_KEY_SPACE)
				current_pane = !current_pane;

			if (current_pane == 0)
				handel_press(&ev, pane_l);
			else if (current_pane == 1)
				handel_press(&ev, pane_r);

			tb_present();
			break;

		case TB_EVENT_RESIZE:
			t_resize(pane_l, pane_r);
			break;
		default:
			break;
		}
	}
	tb_shutdown();
}

static void
handel_press(struct tb_event *ev, Pane *cpane)
{
	/* key require change directory or relist*/
	char listkeys[] = "hlnND/";

	if (strchr(listkeys, ev->ch) != NULL || !(findbm(ev->ch) < 0))
		outdir_press(ev, cpane);
	else /* stay in same directory */
		indir_press(ev, cpane);
}

static void
outdir_press(struct tb_event *ev, Pane *cpane)
{
	char *parent;
	int b;

	if (ev->ch == 'h') {
		parent = get_parent(cpane->dirn);
		if (check_dir(parent) < 0) { /* failed to open directory */
			print_error(strerror(errno));
		} else {
			strcpy(cpane->dirn, parent);
			clear_pane(cpane->dirx);
			cpane->hdir = (size_t)parent_row;
			free(cpane->direntr);
			(void)listdir(cpane, NULL);
			parent_row = 1;
		}
		free(parent);
	} else if (ev->ch == 'l') {
		switch (check_dir(cpane->high_dir)) {
		case 0:
			strcpy(cpane->dirn, cpane->high_dir);
			clear_pane(cpane->dirx);
			parent_row = (int)cpane->hdir;
			cpane->hdir = 1;
			free(cpane->direntr);
			(void)listdir(cpane, NULL);
			break;
		case 1:
			/* is not a directory open file */
			if (open_files(cpane->high_dir) < 0) {
				print_error("procces failed");
				return;
			}
			if (tb_init() != 0)
				die("tb_init");
// 			if (cpane->dirx == 2) /* if current left pane */
// 				t_resize(cpane, opane);
// 			else
// 				t_resize(opane, cpane);
			break;
		case -1:
			/* failed to open directory */
			print_error(strerror(errno));
		}
	} else if (ev->ch == '/') {
		char *user_input;
		user_input = ecalloc(MAX_USRI, sizeof(char));
		if (get_user_input(user_input, MAX_USRI, "filter") < 0) {
			free(user_input);
			return;
		}
		if (listdir(cpane, user_input) < 0) {
			indir_press(ev, cpane);
			print_error("no match");
		}
		free(user_input);
	} else if (ev->ch == 'n') {
		char *user_input;
		user_input = ecalloc(MAX_USRI, sizeof(char));
		if (get_user_input(user_input, MAX_USRI, "new file") < 0) {
			free(user_input);
			return;
		}
		if (create_new_file(cpane->dirn, user_input) < 0) {
			print_error(strerror(errno));
		} else {
			free(cpane->direntr);
			listdir(cpane, NULL);
		}
		free(user_input);
	} else if (ev->ch == 'N') {
		char *user_input;
		user_input = ecalloc(MAX_USRI, sizeof(char));
		if (get_user_input(user_input, MAX_USRI, "new directory") < 0) {
			free(user_input);
			return;
		}
		if (create_new_dir(cpane->dirn, user_input) < 0) {
			print_error(strerror(errno));
		} else {
			free(cpane->direntr);
			listdir(cpane, NULL);
		}
		free(user_input);

	} else if (ev->ch == 'D') {
		switch (delete_ent(cpane->high_dir)) {
		case -1:
			print_error(strerror(errno));
			break;
		case 0:
			clear_pane(cpane->dirx);
			if (cpane->hdir == cpane->dirc) /* last entry */
				cpane->hdir--;
			free(cpane->direntr);
			listdir(cpane, NULL);
			break;
		}
	} else {
		/* bookmarks */
		b = (int)findbm((char)ev->ch);
		if (b < 0)
			return;
		strcpy(cpane->dirn, bmarks[b].path);
		clear_pane(cpane->dirx);
		cpane->hdir = 1;
		free(cpane->direntr);
		listdir(cpane, NULL);
	}
}

static void
indir_press(struct tb_event *ev, Pane *cpane)
{
	if (ev->ch == 'j') {
		if (cpane->hdir < cpane->dirc) {
			cpane->hdir++;
			refresh_pane(cpane);
		}
	} else if (ev->ch == 'k') {
		if (cpane->hdir > 1) {
			cpane->hdir--;
			refresh_pane(cpane);
		}
	} else if (ev->ch == 'g') {
		cpane->hdir = 1;
		refresh_pane(cpane);
	} else if (ev->ch == 'G') {
		cpane->hdir = cpane->dirc;
		refresh_pane(cpane);
	} else if (ev->ch == 'M') {
		cpane->hdir = (cpane->dirc/2);
		refresh_pane(cpane);
	} else if (ev->key == TB_KEY_CTRL_U) {
		if (cpane->hdir > move_ud)
			cpane->hdir = cpane->hdir - move_ud;
		else
			cpane->hdir = 1;
		refresh_pane(cpane);
	} else if (ev->key == TB_KEY_CTRL_D) {
		if (cpane->hdir < cpane->dirc - move_ud)
			cpane->hdir = cpane->hdir + move_ud;
		else
			cpane->hdir = cpane->dirc;
		refresh_pane(cpane);
	}

}

static void
refresh_pane(Pane *cpane)
{

	char *fileinfo;
	char *fullpath;
	size_t y, dyn_y, dyn_max, start_from;
	int width;
	width = (tb_width() / 2) - 4;
	size_t height = (size_t)tb_height() - 2;

	/* scroll */
	y = 1;
	dyn_y = 0;
	start_from = scroll(height, cpane->dirc, cpane->hdir);
	dyn_y = start_from;
	dyn_max = MIN(cpane->dirc, (height - 1) + start_from);

	/* get full path of cursor */
	fullpath = get_full_path(cpane->dirn,
			cpane->direntr[cpane->hdir-1].name);
	strncpy(cpane->high_dir, fullpath, (size_t)MAX_P);
	free(fullpath);

	/* print each entry in directory */
	while (start_from < dyn_max) {
		print_col(&cpane->direntr[start_from], cpane->hdir,
			(size_t)cpane->dirx, y, (int)dyn_y, width);
			start_from++;
			y++;
	}

	fileinfo = get_file_info(&cpane->direntr[cpane->hdir-1]);

	/* print info in statusbar */
	print_status(status_f, status_b, "%lu/%lu %s",
		cpane->hdir,
		cpane->dirc,
		fileinfo);

	free(fileinfo);
}

static int
listdir(Pane *cpane, char *filter)
{
	DIR *dir;
	struct dirent *entry;
	struct stat status;
	char *fullpath;
	int width;
	size_t i, height;
	int filtercount = 0;
	int oldc = cpane->dirc;

	height = (size_t)tb_height() - 2;
	width = (tb_width() / 2) - 4;
	cpane->dirc = 0;
	i = 0;

	dir = opendir(cpane->dirn);
	if (dir == NULL)
		return -1;

	/* get content and filter sum */
	while ((entry = readdir(dir)) != 0) {
		if (filter != NULL) {
			if (strstr(entry->d_name, filter) != NULL)
				filtercount++;
		} else { /* no filter */
			cpane->dirc++;
		}
	}

	if (filter == NULL)
		cpane->dirc -=2;

	if (filter != NULL) {
		if (filtercount > 0) {
			cpane->dirc = filtercount;
			free(cpane->direntr);
			clear_pane(cpane->dirx);
			cpane->hdir = 1;
		} else if (filtercount == 0) {
			if (closedir(dir) < 0)
				return -1;
			cpane->dirc = oldc;
			return -1;
		}
	}

	/* print current directory title */
	printf_tb(cpane->dirx, 0, cpane->dir_fg | TB_BOLD, cpane->dir_bg,
		" %.*s ", width, cpane->dirn);

	/* empty directory */
	if (cpane->dirc == 0) {
		if (closedir(dir) < 0)
			return -1;
		return 0;
	}

	rewinddir(dir); /* reset position */

	/* create array of entries */
	i = 0;
	cpane->direntr = ecalloc(cpane->dirc, sizeof(Entry));
	while ((entry = readdir(dir)) != 0) {
		if ((strcmp(entry->d_name, ".") == 0  ||
			strcmp(entry->d_name, "..") == 0))
			continue;

		/* list found filter */
		if (filter != NULL) {
			if (strstr(entry->d_name, filter) != NULL) {
				strcpy(cpane->direntr[i].name, entry->d_name);
				fullpath = get_full_path(cpane->dirn, entry->d_name);
				strcpy(cpane->direntr[i].full, fullpath);

				if (lstat(fullpath, &status) == 0) {
					cpane->direntr[i].size = status.st_size;
					cpane->direntr[i].mode = status.st_mode;
					cpane->direntr[i].group = status.st_gid;
					cpane->direntr[i].user = status.st_uid;
					cpane->direntr[i].td = status.st_mtime;
				}
				free(fullpath);
				i++;
			}

		} else {

		strcpy(cpane->direntr[i].name, entry->d_name);
		fullpath = get_full_path(cpane->dirn, entry->d_name);
		strcpy(cpane->direntr[i].full, fullpath);

		if (lstat(fullpath, &status) == 0) {
			cpane->direntr[i].size = status.st_size;
			cpane->direntr[i].mode = status.st_mode;
			cpane->direntr[i].group = status.st_gid;
			cpane->direntr[i].user = status.st_uid;
			cpane->direntr[i].td = status.st_mtime;
		}
		free(fullpath);
		i++;
		}
	}

	cpane->dirc = i;
	qsort(cpane->direntr, cpane->dirc, sizeof(Entry), sort_name);
	refresh_pane(cpane);

	if (closedir(dir) < 0)
		return -1;
	return 0;
}

static void
t_resize(Pane *cpane, Pane *opane)
{
	tb_clear();
	draw_frame();
	(void)set_panes(cpane, opane, 1);
	(void)listdir(cpane, NULL);
	(void)listdir(opane, NULL);
	tb_present();

}

static int
set_panes(Pane *pane_l, Pane *pane_r, int resize)
{
	int width;
	char *home;
	char cwd[MAX_P];

	if ((getcwd(cwd, sizeof(cwd)) == NULL))
		return -1;

	home = getenv("HOME");
	width = tb_width();

	if (home == NULL)
		home = "/";

	pane_l->dirx = 2;
	pane_l->dir_fg = pane_l_f;
	pane_l->dir_bg = pane_l_b;
	if (resize == 0) {
		strcpy(pane_l->dirn, cwd);
		pane_l->hdir = 1;
	}

	pane_r->dirx = (width / 2) + 2;
	pane_r->dir_fg = pane_r_f;
	pane_r->dir_bg = pane_r_b;

	if (resize == 0) {
		strcpy(pane_r->dirn, home);
		pane_r->hdir = 1;
	}

	return 0;
}

static void
draw_frame(void)
{
	int height, width, i;

	width = tb_width();
	height = tb_height();

	/* 2 horizontal lines */
	for (i = 1; i < width-1 ; ++i) {
		tb_change_cell(i, 0,        u_hl, frame_f, frame_b);
		tb_change_cell(i, height-1, u_hl, frame_f, frame_b);
	}

	/* 3 vertical lines */
	for (i = 1; i < height-1 ; ++i) {
		tb_change_cell(0,           i, u_vl, frame_f, frame_b);
		tb_change_cell((width-1)/2, i-1, u_vl, frame_f, frame_b);
		tb_change_cell(width-1,     i, u_vl, frame_f, frame_b);
	}

	/* 4 corners */
	tb_change_cell(0,       0,        u_cnw, frame_f, frame_b);
	tb_change_cell(width-1, 0,        u_cne, frame_f, frame_b);
	tb_change_cell(0,       height-1, u_csw, frame_f, frame_b);
	tb_change_cell(width-1, height-1, u_cse, frame_f, frame_b);

	/* 2 middel top and bottom */
	tb_change_cell((width-1)/2, 0,        u_mn, frame_f, frame_b);

}

static int
start(void)
{
	Pane pane_r, pane_l;

	if (tb_init()!= 0)
		die("tb_init");
	if (tb_select_output_mode(TB_OUTPUT_256) != TB_OUTPUT_256)
		if(tb_select_output_mode(TB_OUTPUT_NORMAL) != TB_OUTPUT_NORMAL)
			die("output error");

	tb_clear();
	draw_frame();
	set_panes(&pane_l, &pane_r, 0);
	listdir(&pane_r, NULL);
	listdir(&pane_l, NULL);
	tb_present();
	start_ev(&pane_l, &pane_r);
	return 0;
}

int
main(int argc, char *argv[])
{
#ifdef __OpenBSD__
	if (pledge("cpath exec getpw proc rpath stdio tmppath tty wpath", NULL) == -1)
		die("pledge");
#endif /* __OpenBSD__ */
	if (argc == 1) {
		if (start() != 0)
			die("start failed");
	} else if (
		argc == 2 && strlen(argv[1]) == (size_t)2 &&
		strcmp("-v", argv[1]) == 0) {
			die("sfm-"VERSION);
	} else {
		die("usage: sfm [-v]");
	}
	return 0;
}
