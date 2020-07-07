/* See LICENSE file for copyright and license details. */

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>

#include "termbox.h"
#include "util.h"

/* macros */
#define MAX_P 4095
#define MAX_N 255
#define MAX_USRI 32

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
static void print_status(const char*, ...);
static void print_xstatus(char, int);
static void print_error(const char*, ...);
static void clear(int, int, int, uint16_t);
static void clear_status(void);
static void clear_error(void);
static void clear_pane(int);
static char *get_extentions(char*);
static char *get_full_path(char*, char*);
static char *get_parent(char*);
static char *get_file_info(Entry*);
static char *get_file_size(off_t);
static char *get_file_date_time(time_t);
static char *get_file_userowner(uid_t);
static char *get_file_groupowner(gid_t);
static char *get_file_perm(mode_t);
static int create_new_dir(char*, char*);
static int create_new_file(char*, char*);
static int delete_ent(char *fullpath);
static int delete_file(char*);
static int delete_dir(char*, int);
static int check_dir(char*);
static int open_files(char*);
static int sort_name(const void *const, const void *const);
static void float_to_string(float, char*);
static size_t findbm(char);
static int get_user_input(char*, size_t, char*);
static void print_col(Entry*, size_t, size_t, size_t, int, int, char*);
static size_t scroll(size_t, size_t, size_t);
static int listdir(Pane*, char*);
static void press(struct tb_event*, Pane*, Pane*);
static void t_resize(Pane*, Pane*);
static int set_panes(Pane*, Pane*, int);
static void draw_frame(void);
static int start(void);

/* global variables */
static int parent_row = 1; // FIX

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
print_status(const char *fmt, ...)
{
	int height;
	height = tb_height();

	char buf[256];
	va_list vl;
	va_start(vl, fmt);
	vsnprintf(buf, sizeof(buf), fmt, vl);
	va_end(vl);
	clear_status();
	print_tb(buf, 2, height-2, TB_DEFAULT, status_b);

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
print_error(const char *fmt, ...)
{
	int height, width;
	width = tb_width();
	height = tb_height();

	char buf[256];
	va_list vl;
	va_start(vl, fmt);
	vsnprintf(buf, sizeof(buf), fmt, vl);
	va_end(vl);
	clear_error();
	print_tb(buf, width-20, height-2, 124, status_b);

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
	clear(1, width-20, height-2, status_b);
}

static void
clear_error(void)
{
	int width, height;
	width = tb_width();
	height = tb_height();
	clear(width-30, width-1, height-2, status_b);
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
	size_t size_len = (size_t)9;
	size_t perm_len = (size_t)11;
	size_t ur_len = (size_t)32;
	size_t gr_len = (size_t)32;
	size_t td_len = (size_t)14;
	size_t result_chars = size_len + perm_len + ur_len + gr_len + td_len;
	result = ecalloc(result_chars, sizeof(char));

	size = get_file_size(cursor->size);
	td = get_file_date_time(cursor->td);
	ur = get_file_userowner(cursor->user);
	gr = get_file_groupowner(cursor->group);
	perm = get_file_perm(cursor->mode);

	strncpy(result, perm, perm_len);
	strcat(result, " ");
	strncat(result, size, size_len);
	strcat(result, " ");
	strncat(result, ur, ur_len);
	strcat(result, " ");
	strncat(result, gr, gr_len);
	strcat(result, " ");
	strncat(result, td, td_len);

	free(size);
	free(td);
	free(perm);

	return result;
}

static char *
get_file_size(off_t size)
{
	/* need to be freed */
	char *Rsize;
	double lsize;
	int counter;
	counter = 0;

	Rsize = ecalloc(10, sizeof(char));
	lsize = size;

	while (lsize >= 1000)
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
		strcat(Rsize, "KB");
		break;
	case 2:
		strcat(Rsize, "MB");
		break;
	case 3:
		strcat(Rsize, "GB");
		break;
	case 4:
		strcat(Rsize, "TB");
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
get_file_userowner(uid_t status)
{
	char *user_owner;
	struct passwd *pw;

	pw = getpwuid(status);
	user_owner = pw->pw_name;

	return user_owner;
}

static char *
get_file_groupowner(gid_t status)
{
	char *group_owner;
	struct group *gr;

	gr = getgrgid(status);
	group_owner = gr->gr_name;

	return group_owner;
}

static char *
get_file_perm(mode_t mode)
{
	char *buf;
	size_t i;

	const char chars[] = "rwxrwxrwx";
	buf = ecalloc(11, sizeof(char));

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
	char *path;
	path = ecalloc(strlen(cwd)+strlen(user_input)+2, sizeof(char));
	strcpy(path, cwd);
	strcat(path, "/");
	strcat(path, user_input);

	FILE* file_ptr;
	file_ptr = fopen(path, "w");
	free(path);

	if (file_ptr != NULL) {
		(void)fclose(file_ptr);
		return 0;
	}

	return -1;

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
		return delete_dir(fullpath, AskDel);
	} else {
		return delete_file(fullpath);
	}

}

static int
delete_dir(char *fullpath, int delchoice)
{
	if (delchoice == AskDel) {
		char *confirmation;
		confirmation = ecalloc((size_t)2, sizeof(char));
		if ((get_user_input(
			confirmation, 2,"delete directory (Y) ?") < 0) ||
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
				if (delete_dir(ent_full, DAskDel) < 0) {
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

	print_status("gotit");
	if (closedir(dir) < 0)
		return -1;

	return rmdir(fullpath); /* directory after delete all entries */
}

static int
delete_file(char *fullpath)
{
	char *confirmation;
	confirmation = ecalloc((size_t)2, sizeof(char));

	if ((get_user_input(confirmation, 2, "delete file (Y) ?") < 0) ||
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
		while ((r = waitpid(pid, &status, 0)) == -1 && errno == EINTR)
			continue;
		if (r == -1)
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

static size_t
findbm(char event)
{
	size_t i;

	for (i = 0; i < LEN(bmarks); i++) {
		if (event == bmarks[i].key) {
			if (check_dir(bmarks[i].path) != 0) {
				print_error("%s", strerror(errno));
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
	size_t counter = 1;
	char empty = ' ';
	int x = 0;

	clear_status();
	startat = strlen(prompt) + 3;
	print_status("%s:", prompt);
	tb_set_cursor(startat + 1, height-2);
	tb_present();

	while (tb_poll_event(&fev) != 0) {
		switch (fev.type) {
		case TB_EVENT_KEY:
			if (fev.key == TB_KEY_ESC) {
				tb_set_cursor(-1, -1);
				clear_status();
				return -1;
			}

			if (fev.key == TB_KEY_BACKSPACE ||
				fev.key == TB_KEY_BACKSPACE2) {
				if (BETWEEN(counter, 2, sout)) {
					out[x-1] = '\0';
					counter--;
					x--;
					print_xstatus(empty, startat + counter);
					tb_set_cursor(
						startat + counter, height - 2);
				}

			} else if (fev.key == TB_KEY_ENTER) {
				tb_set_cursor(-1, -1);
				out[counter-1] = '\0';
				return 0;

			} else {
				if (counter < sout) {
					print_xstatus((char)fev.ch, startat+counter);
					out[x] = (char)fev.ch;
					tb_set_cursor(startat + counter + 1,height-2);
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
print_col(Entry *entry, size_t hdir, size_t x, size_t y, int dyn_y, int width, char *filter)
{
	uint16_t bg, fg;
	char buf[MAX_P];
	bg = file_b;
	fg = file_f;

	if (S_ISDIR(entry->mode)) {
		bg = dir_b;
		fg = dir_f;
	} else if (S_ISLNK(entry->mode)) {
		realpath(entry->full, buf);
		strcat(entry->name, " -> ");
		strncat(entry->name, buf, MAX_N - strlen(entry->name)-1);
		bg = other_b;
		fg = other_f;
	}

	/* highlighted (cursor) */
	if (y + dyn_y == hdir) {
		bg = bg | TB_REVERSE;
		fg = fg | TB_REVERSE | TB_BOLD;
	}

	/* highlight found filter */
	if (filter != NULL) {
		if (strstr(entry->name, filter) != NULL) {
			bg = search_b;
			fg = search_f | TB_BOLD;
		}
	}

	/* print each element in directory */
	printf_tb((int)x, (int)y, fg, bg, "%*.*s", ~width, width, entry->name);

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

static int
listdir(Pane *cpane, char *filter)
{
	DIR *dir;
	struct dirent *entry;
	struct stat status;
	Entry *list;
	char *fullpath;
	char *fileinfo;
	int width;
	size_t y, i, height, dyn_max, dyn_y, start_from;

	height = (size_t)tb_height() - 2;
	width = (tb_width() / 2) - 4;
	cpane->dirc = - 2; /* dont't count . .. */
	i = 0;

	dir = opendir(cpane->dirn);
	if (dir == NULL)
		return -1;

	/* get content sum */
	while ((entry = readdir(dir)) != 0) {
		cpane->dirc++;
	}

	/* empty directory */
	if (cpane->dirc == 0) {
		if (closedir(dir) < 0)
			return -1;

		/* print current directory title */
		printf_tb(cpane->dirx, 0, cpane->dir_fg | TB_BOLD,
			cpane->dir_bg, " %.*s ", width, cpane->dirn);
		return 0;
	}

	rewinddir(dir); /* reset position */

	/* create array of entries */
	i = 0;
	list = ecalloc(cpane->dirc, sizeof(Entry));
	while ((entry = readdir(dir)) != 0) {
		if ((strcmp(entry->d_name, ".") == 0  ||
			strcmp(entry->d_name, "..") == 0))
			continue;

		strcpy(list[i].name, entry->d_name);
		fullpath = get_full_path(cpane->dirn, entry->d_name);
		strcpy(list[i].full, fullpath);

		if (lstat(fullpath, &status) == 0) {
			list[i].size = status.st_size;
			list[i].mode = status.st_mode;
			list[i].group = status.st_gid;
			list[i].user = status.st_uid;
			list[i].td = status.st_mtime;
		}

		free(fullpath);

		i++;
	}

	qsort(list, cpane->dirc, sizeof(Entry), sort_name);

	/* scroll */
	y = 1;
	dyn_y = 0;
	start_from = scroll(height, cpane->dirc, cpane->hdir);
	dyn_y = start_from;
	dyn_max = MIN(cpane->dirc, (height - 1) + start_from);

	/* get full path of cursor */
	fullpath = get_full_path(cpane->dirn,
			list[cpane->hdir-1].name);
	strncpy(cpane->high_dir, fullpath, (size_t)MAX_P);
	free(fullpath);

	/* print each entry in directory */
	while (start_from < dyn_max) {
		print_col(&list[start_from], cpane->hdir,
			(size_t)cpane->dirx, y, (int)dyn_y, width, filter);
			start_from++;
			y++;
	}

	fileinfo = get_file_info(&list[cpane->hdir-1]);

	/* print info in statusbar */
	print_status("%lu/%lu %s",
		cpane->hdir,
		cpane->dirc,
		fileinfo);

	/* print current directory title */
	printf_tb(cpane->dirx, 0, cpane->dir_fg | TB_BOLD, cpane->dir_bg,
		" %.*s ", width, cpane->dirn);

	free(fileinfo);
	free(list);

	if (closedir(dir) < 0)
		return -1;

// 	print_error("mem (%d)", get_memory_usage());

	return 0;
}

static void
press(struct tb_event *ev, Pane *cpane, Pane *opane)
{
	char *parent;
	int b;
// 	clear_error();

	if (ev->ch == 'j') {
		if (cpane->hdir < cpane->dirc) {
			cpane->hdir++;
			(void)listdir(cpane, NULL);
		}
	} else if (ev->ch == 'k') {
		if (cpane->hdir > 1) {
			cpane->hdir--;
			(void)listdir(cpane, NULL);
		}
	} else if (ev->ch == 'h') {
		parent = get_parent(cpane->dirn);
		if (check_dir(parent) < 0) { /* failed to open directory */
			print_error("%s", strerror(errno));
		} else {
			strcpy(cpane->dirn, parent);
			clear_pane(cpane->dirx);
			cpane->hdir = (size_t)parent_row;
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
			if (cpane->dirx == 2) /* if current left pane */
				t_resize(cpane, opane);
			else
				t_resize(opane, cpane);
			break;
		case -1:
			/* failed to open directory */
			print_error("%s", strerror(errno));
		}
	} else if (ev->ch == 'g') {
		cpane->hdir = 1;
		(void)listdir(cpane, NULL);
	} else if (ev->ch == 'G') {
		cpane->hdir = cpane->dirc;
		(void)listdir(cpane, NULL);
	} else if (ev->ch == 'M') {
		cpane->hdir = (cpane->dirc/2);
		(void)listdir(cpane, NULL);
	} else if (ev->key == TB_KEY_CTRL_U) {
		if (cpane->hdir > move_ud)
			cpane->hdir = cpane->hdir - move_ud;
		else
			cpane->hdir = 1;
		(void)listdir(cpane, NULL);
	} else if (ev->key == TB_KEY_CTRL_D) {
		if (cpane->hdir < cpane->dirc - move_ud)
			cpane->hdir = cpane->hdir + move_ud;
		else
			cpane->hdir = cpane->dirc;
		(void)listdir(cpane, NULL);
	} else if (ev->ch == 'n') {
		char *user_input;
		user_input = ecalloc(MAX_USRI, sizeof(char));
		if (get_user_input(user_input, MAX_USRI, "new file") < 0) {
			free(user_input);
			return;
		}
		if (create_new_file(cpane->dirn, user_input) < 0)
			print_error("%s", strerror(errno));
		else
			listdir(cpane, NULL);
		free(user_input);
	} else if (ev->ch == 'N') {
		char *user_input;
		user_input = ecalloc(MAX_USRI, sizeof(char));
		if (get_user_input(user_input, MAX_USRI, "new directory") < 0) {
			free(user_input);
			return;
		}
		if (create_new_dir(cpane->dirn, user_input) < 0)
			print_error("%s", strerror(errno));
		else
			listdir(cpane, NULL);
		free(user_input);

	} else if (ev->ch == 'D') {
		switch (delete_ent(cpane->high_dir)) {
		case -1:
			print_error("%s", strerror(errno));
			break;
		case 0:
			clear_pane(cpane->dirx);
			if (cpane->hdir == cpane->dirc) /* last entry */
				cpane->hdir--;
			listdir(cpane, NULL);
			break;
		}
	} else if (ev->ch == '/') {
		char *user_input;
		user_input = ecalloc(MAX_USRI, sizeof(char));
		if (get_user_input(user_input, MAX_USRI, "filter") < 0) {
			free(user_input);
			return;
		}
		print_error("out -> %s", user_input);
		listdir(cpane, user_input);
		free(user_input);
	} else {
		/* bookmarks */
		b = (int)findbm((char)ev->ch);
		if (b < 0)
			return;

		strcpy(cpane->dirn, bmarks[b].path);
		clear_pane(cpane->dirx);
		cpane->hdir = 1;
		(void)listdir(cpane, NULL);
	}

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
	int init_height, init_width;
	struct tb_event ev;
	Pane pane_r, pane_l;
	int current_pane = 0;

	if (tb_init()!= 0)
		die("tb_init");

	init_width = tb_width();
	init_height = tb_height();

	if (tb_select_output_mode(TB_OUTPUT_256) != TB_OUTPUT_256)
		(void)tb_select_output_mode(TB_OUTPUT_NORMAL);

	tb_clear();
	draw_frame();
	(void)set_panes(&pane_l, &pane_r, 0);
	(void)listdir(&pane_r, NULL);
	(void)listdir(&pane_l, NULL);
	tb_present();

	while (tb_poll_event(&ev) != 0) {
		switch (ev.type) {
		case TB_EVENT_KEY:

			if (ev.ch == 'q') {
				tb_shutdown();
				return 0;
			} else if (ev.key == TB_KEY_SPACE) {
				current_pane = !current_pane;
			}

			if (current_pane == 0) {
				press(&ev, &pane_l, &pane_r);

			} else if (current_pane == 1) {
				press(&ev, &pane_r, &pane_l);
			}

			tb_present();
			break;

		case TB_EVENT_RESIZE:
			t_resize(&pane_l, &pane_r);
			break;

		default:
			break;
		}
	}

	tb_shutdown();
	return 0;
}

int
main(int argc, char *argv[])
{
#ifdef __OpenBSD__
	if (pledge("stdio tty rpath proc exec", NULL) == -1)
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
