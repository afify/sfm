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
	gid_t group;
	mode_t mode;
	off_t size;
	time_t td;
	uid_t user;
} Entry;

typedef struct {
	char dirn[MAX_P];     // dir name cwd
	Entry *direntr;       // dir entries
	int dirx;             // pane cwd x pos
	size_t dirc;          // dir entries sum
	size_t hdir;          // highlighted dir
	size_t firstrow;
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

typedef union {
	uint16_t key; /* one of the TB_KEY_* constants */
	uint32_t ch;  /* unicode character */
} Evkey;

typedef struct {
	const Evkey evkey;
	void (*func)(void);
} Key;

/* function declarations */
static void print_tb(const char*, int, int, uint16_t, uint16_t);
static void printf_tb(int, int, uint16_t, uint16_t, const char*, ...);
static void print_status(uint16_t, uint16_t, const char*, ...);
static void print_xstatus(char, int);
static void print_error(char*);
static void print_prompt(char*);
static void print_info(void);
static void print_row(Pane*, size_t, uint16_t, uint16_t);
static void clear(int, int, int, uint16_t);
static void clear_status(void);
static void clear_pane(int);
static void add_hi(Pane*, size_t);
static void rm_hi(Pane*, size_t);
static void float_to_string(float, char*);
static int check_dir(char*);
static mode_t chech_execf(mode_t);
static int sort_name(const void *const, const void *const);
static char *get_ext(char*);
static int get_fdt(char*, size_t, time_t);
static char *get_fgrp(gid_t, size_t);
static char *get_finfo(Entry*);
static char *get_fperm(mode_t);
static char *get_fsize(off_t);
static char *get_fullpath(char*, char*);
static char *get_fusr(uid_t, size_t);
static void get_dirsize(char*, off_t*);
static void get_hicol(uint16_t*, uint16_t*, mode_t);
static int deldir(char*, int);
static int delent(char *);
static int delf(char*);
static void calcdir(void);
static void crnd(void);
static void crnf(void);
static void delfd(void);
static void mvbk(void);
static void mvbtm(void);
static void mvdwn(void);
static void mvdwns(void);
static void mvfor(void);
static void mvmid(void);
static void mvtop(void);
static void mvup(void);
static void mvups(void);
static void scrdwn(void);
static void scrdwns(void);
static void scrup(void);
static void scrups(void);
static int get_usrinput(char*, size_t, char*);
static int open_files(char*);
static ssize_t findbm(char);
static void filter(void);
static void switch_pane(void);
static void quit(void);
static void grabkeys(struct tb_event*);
static void start_ev(void);
static void refresh_pane(void);
static int listdir(char*);
static void t_resize(void);
static int set_panes(int);
static void draw_frame(void);
static int start(void);

/* global variables */
static Pane pane_r, pane_l, *cpane;
static int parent_row = 1; // FIX
static size_t scrheight;

static const uint32_t INOTIFY_MASK = IN_CREATE | IN_DELETE | IN_DELETE_SELF \
	| IN_MODIFY | IN_MOVE_SELF | IN_MOVED_FROM | IN_MOVED_TO;

/* configuration, allows nested code to access above variables */
#include "config.h"

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
	print_tb(buf, 1, height-1, fg, bg);

}

static void
print_xstatus(char c, int x)
{
	int height;
	uint32_t uni = 0;
	height = tb_height();
	(void)tb_utf8_char_to_unicode(&uni, &c);
	tb_change_cell(x, height-1, uni,  TB_DEFAULT, status_b);
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
print_info(void)
{
	char *fileinfo;
	fileinfo = get_finfo(&cpane->direntr[cpane->hdir-1]);
	print_status(status_f, status_b, "%lu/%lu %s",
		cpane->hdir, cpane->dirc, fileinfo);
	free(fileinfo);
}

static void
print_row(Pane *pane, size_t entpos, uint16_t fg, uint16_t bg)
{
	int x, y;
	char *result;
	char buf[MAX_P];
	char lnk_full[MAX_P];
	int width;

	width = (tb_width() / 2) - 4;
	result = pane->direntr[entpos].name;
	x = pane->dirx;
	y = entpos - cpane->firstrow + 1;

	if (S_ISLNK(pane->direntr[entpos].mode) &&
			realpath(pane->direntr[entpos].name, buf) != NULL) {
		strncpy(lnk_full, pane->direntr[entpos].name, MAX_N);
		strcat(lnk_full, " -> ");
		strncat(lnk_full, buf, MAX_N);
		result = lnk_full;
	}

	printf_tb(x, y, fg, bg, "%*.*s", ~width, width, result);
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
	clear(1, width-1, height-1, status_b);
}

static void
clear_pane(int pane)
{
	int i, x, ex, y, width, height;
	width = tb_width();
	height = tb_height();
	x = 0, y = 0, i = 0, ex = 0;

	if (pane == 2) {
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

static void
add_hi(Pane *pane, size_t entpos)
{
	uint16_t fg, bg;
	get_hicol(&fg, &bg, pane->direntr[entpos].mode);
	print_row(pane, entpos, fg|TB_REVERSE|TB_BOLD, bg|TB_REVERSE);
}

static void
rm_hi(Pane *pane, size_t entpos)
{
	uint16_t fg, bg;
	get_hicol(&fg, &bg, pane->direntr[entpos].mode);
	print_row(pane, entpos, fg, bg);
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

static char *
get_ext(char *str)
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

static int
get_fdt(char *result, size_t reslen, time_t status)
{
	struct tm lt;
	localtime_r(&status, &lt);
	return strftime(result, reslen, dtfmt, &lt);
}

static char *
get_fgrp(gid_t status, size_t len)
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
get_finfo(Entry *cursor)
{
	char *sz, *rst, *ur, *gr, *td, *prm;
	size_t szlen, prmlen, urlen, grlen, tdlen, rstlen;

	szlen = 9;
	prmlen = 11;
	urlen = grlen = tdlen = 32;
	rstlen = szlen + prmlen + urlen + grlen + tdlen;
	rst = ecalloc(rstlen, sizeof(char));

	if (show_perm == 1) {
		prm = get_fperm(cursor->mode);
		strncpy(rst, prm, prmlen);
		strcat(rst, " ");
		free(prm);
	}

	if (show_ug == 1) {
		ur = get_fusr(cursor->user, urlen);
		gr = get_fgrp(cursor->group, grlen);
		strncat(rst, ur, urlen);
		strcat(rst, ":");
		strncat(rst, gr, grlen);
		strcat(rst, " ");
		free(ur);
		free(gr);
	}

	if (show_dt == 1) {
		td = ecalloc(tdlen, sizeof(char));
		if (get_fdt(td, tdlen, cursor->td) > 0) {
			strncat(rst, td, tdlen);
			strcat(rst, " ");
		}
		free(td);
	}

	if (show_size == 1 && S_ISREG(cursor->mode)) {
		sz = get_fsize(cursor->size);
		strncat(rst, sz, szlen);
		free(sz);
	}

	return rst;
}

static char *
get_fperm(mode_t mode)
{
	char *buf;
	size_t i;

	const char chars[] = "rwxrwxrwx";
	buf = ecalloc((size_t)11, sizeof(char));

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
		buf[i] = (mode & (1 << (9-i))) ? chars[i-1] : '-';
	}
	buf[10] = '\0';

	return buf;
}

static char *
get_fsize(off_t size)
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
get_fullpath(char *first, char *second)
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
get_fusr(uid_t status, size_t len)
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

static void
get_dirsize(char *fullpath, off_t *fullsize)
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

		ent_full = get_fullpath(fullpath, entry->d_name);
		if (lstat(ent_full, &status) == 0) {
			mode = status.st_mode;
			if (S_ISDIR(mode)) {
				get_dirsize(ent_full, fullsize);
				free(ent_full);
			} else {

				*fullsize += status.st_size;
				free(ent_full);
			}
		}
	}

	closedir(dir);
	clear_status();
}

static void
get_hicol(uint16_t *fg, uint16_t *bg, mode_t mode)
{
	*bg = file_b;
	*fg = file_f;

	if (S_ISDIR(mode)) {
		*bg = dir_b;
		*fg = dir_f;
	} else if (S_ISLNK(mode)) {
		*bg = other_b;
		*fg = other_f;
	} else if (chech_execf(mode) > 0) {
		*fg = exec_f;
	}
}

static int
deldir(char *fullpath, int delchoice)
{
	if (delchoice == (int)AskDel) {
		char *confirmation;
		confirmation = ecalloc((size_t)2, sizeof(char));
		if ((get_usrinput(
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

		ent_full = get_fullpath(fullpath, entry->d_name);
		if (lstat(ent_full, &status) == 0) {
			mode = status.st_mode;
			if (S_ISDIR(mode)) {
				if (deldir(ent_full, (int)DAskDel) < 0) {
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
delent(char *fullpath)
{
	struct stat status;
	mode_t mode;

	if (lstat(fullpath, &status) < 0)
		return -1;

	mode = status.st_mode;
	if (S_ISDIR(mode)) {
		return deldir(fullpath, (int)AskDel);
	} else {
		return delf(fullpath);
	}

}

static int
delf(char *fullpath)
{
	char *confirmation;
	confirmation = ecalloc((size_t)2, sizeof(char));

	if ((get_usrinput(confirmation, (size_t)2, "delete file (Y) ?") < 0) ||
		(strcmp(confirmation, "Y") != 0)) {
		free(confirmation);
		return 1; /* canceled by user or wrong confirmation */
	}

	free(confirmation);
	return unlink(fullpath);
}

static void
calcdir(void)
{
	off_t *fullsize;
	char *csize;
	char *result;

	if (S_ISDIR(cpane->direntr[cpane->hdir-1].mode)) {
		fullsize = ecalloc(50, sizeof(off_t));
		get_dirsize(cpane->direntr[cpane->hdir-1].name, fullsize);
		csize = get_fsize(*fullsize);
		result = get_finfo(&cpane->direntr[cpane->hdir-1]);

		clear_status();
		print_status(status_f, status_b, "%lu/%lu %s%s",
			cpane->hdir, cpane->dirc, result, csize);
		free(csize);
		free(fullsize);
		free(result);
	}
}

static void
crnd(void)
{
	char *user_input, *path;
	size_t pathlen;

	user_input = ecalloc(MAX_USRI, sizeof(char));
	if (get_usrinput(user_input, MAX_USRI, "new file") < 0) {
		free(user_input);
		return;
	}

	pathlen = strlen(cpane->dirn) + 1 + MAX_USRI + 1;
	path = ecalloc(pathlen, sizeof(char));
	if (snprintf(path, pathlen, "%s/%s", cpane->dirn, user_input) < 0) {
		free(user_input);
		free(path);
		return;
	}

	if (mkdir(path, new_dir_perm) < 0)
		print_error(strerror(errno));
	else
		listdir(NULL);

	free(user_input);
	free(path);
}

static void
crnf(void)
{
	char *user_input, *path;
	size_t pathlen;
	int rf;

	user_input = ecalloc(MAX_USRI, sizeof(char));
	if (get_usrinput(user_input, MAX_USRI, "new file") < 0) {
		free(user_input);
		return;
	}

	pathlen = strlen(cpane->dirn) + 1 + MAX_USRI + 1;
	path = ecalloc(pathlen, sizeof(char));
	if (snprintf(path, pathlen, "%s/%s", cpane->dirn, user_input) < 0) {
		free(user_input);
		free(path);
		return;
	}

	rf = open(path, O_CREAT|O_EXCL, S_IRUSR|S_IWUSR);

	if (rf < 0) {
		print_error(strerror(errno));
	} else {
		if (close(rf) < 0)
			print_error(strerror(errno));
		else
			listdir(NULL);
	}

	free(user_input);
	free(path);
}

static void
delfd(void)
{
	switch (delent(cpane->direntr[cpane->hdir-1].name)) {
	case -1:
		print_error(strerror(errno));
		break;
	case 0:
		if (cpane->hdir == cpane->dirc) /* last entry */
			cpane->hdir--;
		listdir(NULL);
		break;
	}
}

static void
mvbk(void)
{
	chdir("..");
	getcwd(cpane->dirn, MAX_P);
	cpane->firstrow = 0;
	cpane->hdir = parent_row;
	listdir(NULL);
	parent_row = 1;
	add_hi(cpane, cpane->hdir-1);
}

static void
mvbtm(void)
{
	if (cpane->dirc > scrheight) {
		clear_pane(cpane->dirx);
		rm_hi(cpane, cpane->hdir-1);
		cpane->hdir = cpane->dirc;
		cpane->firstrow = cpane->dirc - scrheight + 1;
		refresh_pane();
		add_hi(cpane, cpane->hdir-1);
	} else {
		rm_hi(cpane, cpane->hdir-1);
		cpane->hdir = cpane->dirc;
		add_hi(cpane, cpane->hdir-1);
	}
	print_info();
}

static void
mvdwn(void)
{
	if (cpane->dirc < scrheight && cpane->hdir < cpane->dirc) {
		rm_hi(cpane, cpane->hdir-1);
		cpane->hdir++;
		add_hi(cpane, cpane->hdir-1);
	} else {
		mvdwns(); /* scroll */
	}
	print_info();
}

static void
mvdwns(void)
{
	size_t real;
	real= cpane->hdir - 1 - cpane->firstrow;

	if (real > scrheight - 3 - scrsp && cpane->hdir + scrsp < cpane->dirc) {
		cpane->firstrow++;
		clear_pane(cpane->dirx);
		rm_hi(cpane, cpane->hdir-1);
		cpane->hdir++;
		refresh_pane();
		add_hi(cpane, cpane->hdir-1);
	} else if (cpane->hdir < cpane->dirc) {
		rm_hi(cpane, cpane->hdir-1);
		cpane->hdir++;
		add_hi(cpane, cpane->hdir-1);
	}
}

static void
mvfor(void)
{
	switch (check_dir(cpane->direntr[cpane->hdir-1].name)) {
	case 0:
		chdir(cpane->direntr[cpane->hdir-1].name);
		getcwd(cpane->dirn, MAX_P);
		parent_row = (int)cpane->hdir;
		cpane->hdir = 1;
		cpane->firstrow = 0;
		(void)listdir(NULL);
		add_hi(cpane, cpane->hdir-1);
		break;
	case 1:
		/* is not a directory open file */
		if (open_files(cpane->direntr[cpane->hdir-1].name) < 0) {
			print_error("procces failed");
			return;
		}
		if (tb_init() != 0)
			die("tb_init");
		t_resize();
		break;
	case -1:
		/* failed to open directory */
		print_error(strerror(errno));
	}
}

static void
mvmid(void)
{
	rm_hi(cpane, cpane->hdir - 1);
	cpane->hdir = (scrheight / 2) + cpane->firstrow;
	add_hi(cpane, cpane->hdir - 1);
	print_info();
}

static void
mvtop(void)
{
	if (cpane->dirc > scrheight) {
		clear_pane(cpane->dirx);
		rm_hi(cpane, cpane->hdir-1);
		cpane->hdir = 1;
		cpane->firstrow = 0;
		refresh_pane();
		add_hi(cpane, cpane->hdir-1);
	} else {
		rm_hi(cpane, cpane->hdir-1);
		cpane->hdir = 1;
		add_hi(cpane, cpane->hdir-1);
		print_info();
	}
}

static void
mvup(void)
{
	if (cpane->dirc < scrheight && cpane->hdir > 1) {
		rm_hi(cpane, cpane->hdir-1);
		cpane->hdir--;
		add_hi(cpane, cpane->hdir-1);
	} else {
		mvups(); /* scroll */
	}
	print_info();
}

static void
mvups(void)
{
	size_t real;
	real= cpane->hdir - 1 -  cpane->firstrow;

	if (cpane->firstrow > 0 && real < 1 + scrsp) {
		cpane->firstrow--;
		clear_pane(cpane->dirx);
		rm_hi(cpane, cpane->hdir-1);
		cpane->hdir--;
		refresh_pane();
		add_hi(cpane, cpane->hdir-1);
	} else if (cpane->hdir > 1) {
		rm_hi(cpane, cpane->hdir-1);
		cpane->hdir--;
		add_hi(cpane, cpane->hdir-1);
	}
}

static void
scrdwn(void)
{
	if (cpane->dirc < scrheight && cpane->hdir < cpane->dirc) {
		if (cpane->hdir < cpane->dirc - scrmv) {
			rm_hi(cpane, cpane->hdir-1);
			cpane->hdir += scrmv;
			add_hi(cpane, cpane->hdir-1);
		} else {
			mvbtm();
		}
	} else {
		scrdwns();
	}
	print_info();
}

static void
scrdwns(void)
{
	size_t real;
	int dynmv;
	real = cpane->hdir - cpane->firstrow;
	dynmv = MIN(cpane->dirc - cpane->hdir - cpane->firstrow , scrmv);

	if (real + scrmv + 1 > scrheight &&
		cpane->hdir + scrsp + scrmv < cpane->dirc) { /* scroll */
		cpane->firstrow += dynmv;
		clear_pane(cpane->dirx);
		rm_hi(cpane, cpane->hdir-1);
		cpane->hdir += scrmv;
		refresh_pane();
		add_hi(cpane, cpane->hdir-1);
	} else {
		if (cpane->hdir < cpane->dirc - scrmv) {
			rm_hi(cpane, cpane->hdir-1);
			cpane->hdir += scrmv;
			add_hi(cpane, cpane->hdir-1);
		} else {
			mvbtm();
		}
	}
}

static void
scrup(void)
{
	if (cpane->dirc < scrheight && cpane->hdir > 1) {
		if (cpane->hdir > scrmv) {
			rm_hi(cpane, cpane->hdir-1);
			cpane->hdir = cpane->hdir - scrmv;
			add_hi(cpane, cpane->hdir-1);
			print_info();
		} else {
			mvtop();
		}
	} else {
		scrups();
	}
}

static void
scrups(void)
{
	size_t real;
	int dynmv;
	real = cpane->hdir - cpane->firstrow;
	dynmv = MIN(cpane->firstrow , scrmv);

	if (cpane->firstrow > 0  && real < scrmv + scrsp) {
		cpane->firstrow -= dynmv;
		clear_pane(cpane->dirx);
		rm_hi(cpane, cpane->hdir-1);
		cpane->hdir -= scrmv;
		refresh_pane();
		add_hi(cpane, cpane->hdir-1);
	} else {
		if (cpane->hdir > scrmv + 1) {
			rm_hi(cpane, cpane->hdir-1);
			cpane->hdir -= scrmv;
			add_hi(cpane, cpane->hdir-1);
		} else {
			mvtop();
		}
	}
}

static int
get_usrinput(char *out, size_t sout, char *prompt)
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
	tb_set_cursor((int)(startat + 1), height-1);
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
						(int)startat + counter, height - 1);
				}

			} else if (fev.key == (uint16_t)TB_KEY_ENTER) {
				tb_set_cursor(-1, -1);
				out[counter-1] = '\0';
				return 0;

			} else {
				if (counter < sout) {
					print_xstatus((char)fev.ch, (int)(startat+counter));
					out[x] = (char)fev.ch;
					tb_set_cursor((int)(startat + counter + 1),height-1);
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
	file_ex = get_ext(filename);
	software = NULL;

	/* find software in rules */
	for (c = 0; c < LEN(rules); c++) {
		for (d = 0; d < rules[c].len; d++) {
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

static void
filter(void)
{
	char *user_input;
	user_input = ecalloc(MAX_USRI, sizeof(char));
	if (get_usrinput(user_input, MAX_USRI, "filter") < 0) {
		free(user_input);
		return;
	}
	if (listdir(user_input) < 0) {
		print_error("no match");
	}
	free(user_input);
}

static void
switch_pane(void)
{
	if (cpane == &pane_l) {
		rm_hi(&pane_l, pane_l.hdir-1);
		add_hi(&pane_r, pane_r.hdir-1);
		chdir(pane_r.dirn);
		cpane = &pane_r;
	} else if (cpane == &pane_r) {
		rm_hi(&pane_r, pane_r.hdir-1);
		add_hi(&pane_l, pane_l.hdir-1);
		chdir(pane_l.dirn);
		cpane = &pane_l;
	}
	print_info();
}

static void
quit(void)
{
	free(pane_l.direntr);
	free(pane_r.direntr);
	tb_shutdown();
	exit(EXIT_SUCCESS);
}

static void
grabkeys(struct tb_event *event)
{
	size_t i;

	for (i = 0; i < LEN(keys); i++) {
		if (event->ch != 0) {
			if (event->ch == keys[i].evkey.ch) {
				keys[i].func();
				return;
			}
		} else if (event->key != 0) {
			if (event->key == keys[i].evkey.key) {
				keys[i].func();
				return;
			}
		}
	}
}

static void
start_ev(void)
{
	struct tb_event ev;

	while (tb_poll_event(&ev) != 0) {
		switch (ev.type) {
		case TB_EVENT_KEY:
			grabkeys(&ev);
			tb_present();
			break;
		case TB_EVENT_RESIZE:
			t_resize();
			break;
		default:
			break;
		}
	}
	tb_shutdown();
}

static void
refresh_pane(void)
{
	size_t y, dyn_max, start_from;
	int width;
	width = (tb_width() / 2) - 4;
	uint16_t fg, bg;

	y = 1;
	start_from = cpane->firstrow;
	dyn_max = MIN(cpane->dirc, (scrheight - 1) + cpane->firstrow);

	/* print each entry in directory */
	while (start_from < dyn_max) {
		get_hicol(&fg, &bg, cpane->direntr[start_from].mode);
		print_row(cpane, start_from, fg ,bg);
		start_from++;
		y++;
	}

	print_info();

	/* print current directory title */
	printf_tb(cpane->dirx, 0, cpane->dir_fg | TB_BOLD, cpane->dir_bg,
		" %.*s ", width, cpane->dirn);
}

static int
listdir(char *filter)
{
	DIR *dir;
	struct dirent *entry;
	struct stat status;
	int width;
	size_t i;
	int filtercount = 0;
	size_t oldc = cpane->dirc;

	width = (tb_width() / 2) - 4;
	cpane->dirc = 0;
	i = 0;

	if (chdir(cpane->dirn) < 0)
		return -1;

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

	if (filter == NULL) {
		clear_pane(cpane->dirx);
		cpane->dirc -=2;
	}

	if (filter != NULL) {
		if (filtercount > 0) {
			cpane->dirc -=2;
			cpane->dirc = filtercount;
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
		clear_status();
		if (closedir(dir) < 0)
			return -1;
		return 0;
	}

	rewinddir(dir); /* reset position */

	/* create array of entries */
	i = 0;
	cpane->direntr = erealloc(cpane->direntr, cpane->dirc * sizeof(Entry));
	while ((entry = readdir(dir)) != 0) {
		if ((strcmp(entry->d_name, ".") == 0  ||
			strcmp(entry->d_name, "..") == 0))
			continue;

		/* list found filter */
		if (filter != NULL) {
			if (strstr(entry->d_name, filter) != NULL) {
				strcpy(cpane->direntr[i].name, entry->d_name);
				if (lstat(entry->d_name, &status) == 0) {
					cpane->direntr[i].size = status.st_size;
					cpane->direntr[i].mode = status.st_mode;
					cpane->direntr[i].group = status.st_gid;
					cpane->direntr[i].user = status.st_uid;
					cpane->direntr[i].td = status.st_mtime;
				}
				i++;
			}

		} else {
		strcpy(cpane->direntr[i].name, entry->d_name);
		if (lstat(entry->d_name, &status) == 0) {
			cpane->direntr[i].size = status.st_size;
			cpane->direntr[i].mode = status.st_mode;
			cpane->direntr[i].group = status.st_gid;
			cpane->direntr[i].user = status.st_uid;
			cpane->direntr[i].td = status.st_mtime;
		}
		i++;
		}
	}

	cpane->dirc = i;
	qsort(cpane->direntr, cpane->dirc, sizeof(Entry), sort_name);
	refresh_pane();

	if (closedir(dir) < 0)
		return -1;
	return 0;
}

static void
t_resize(void)
{
	/* TODO need refactoring */
	tb_clear();
	draw_frame();
	(void)set_panes(1);

	if (cpane == &pane_l) {
		chdir(pane_r.dirn);
		cpane = &pane_r;
		refresh_pane();
		chdir(pane_l.dirn);
		cpane = &pane_l;
		refresh_pane();
	} else if (cpane == &pane_r) {
		chdir(pane_l.dirn);
		cpane = &pane_l;
		refresh_pane();
		chdir(pane_r.dirn);
		cpane = &pane_r;
		refresh_pane();
	}

	tb_present();
}

static int
set_panes(int resize)
{
	int width;
	char *home;
	char cwd[MAX_P];
	scrheight = tb_height() - 2;

	home = getenv("HOME");
	width = tb_width();
	if ((getcwd(cwd, sizeof(cwd)) == NULL))
		return -1;
	if (home == NULL)
		home = "/";

	pane_l.dirx = 2;
	pane_l.dir_fg = pane_l_f;
	pane_l.dir_bg = pane_l_b;
	pane_l.firstrow = 0;
	if (resize == 0) {
		pane_l.direntr = ecalloc(0, sizeof(Entry));
		strcpy(pane_l.dirn, cwd);
		pane_l.hdir = 1;
	}

	pane_r.dirx = (width / 2) + 2;
	pane_r.dir_fg = pane_r_f;
	pane_r.dir_bg = pane_r_b;
	pane_r.firstrow = 0;
	if (resize == 0) {
		pane_r.direntr = ecalloc(0, sizeof(Entry));
		strcpy(pane_r.dirn, home);
		pane_r.hdir = 1;
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
		tb_change_cell(i, height-2, u_hl, frame_f, frame_b);
	}

	/* 3 vertical lines */
	for (i = 1; i < height-1 ; ++i) {
		tb_change_cell(0,           i,   u_vl, frame_f, frame_b);
		tb_change_cell((width-1)/2, i-1, u_vl, frame_f, frame_b);
		tb_change_cell(width-1,     i,   u_vl, frame_f, frame_b);
	}

	/* 4 corners */
	tb_change_cell(0,       0,        u_cnw, frame_f, frame_b);
	tb_change_cell(width-1, 0,        u_cne, frame_f, frame_b);
	tb_change_cell(0,       height-2, u_csw, frame_f, frame_b);
	tb_change_cell(width-1, height-2, u_cse, frame_f, frame_b);

	/* 2 middel top and bottom */
	tb_change_cell((width-1)/2, 0,        u_mn, frame_f, frame_b);
	tb_change_cell((width-1)/2, height-2, u_ms, frame_f, frame_b);
}

static int
start(void)
{
	if (tb_init()!= 0)
		die("tb_init");
	if (tb_select_output_mode(TB_OUTPUT_256) != TB_OUTPUT_256)
		if (tb_select_output_mode(TB_OUTPUT_NORMAL) != TB_OUTPUT_NORMAL)
			die("output error");

	draw_frame();
	set_panes(0);
	cpane = &pane_r;
	listdir(NULL);
	cpane = &pane_l;
	listdir(NULL);
	add_hi(&pane_l, pane_l.hdir-1);
	tb_present();
	start_ev();
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
