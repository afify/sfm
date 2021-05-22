/* See LICENSE file for copyright and license details. */

#if defined(__linux__)
#define _GNU_SOURCE
#elif defined(__APPLE__)
#define _DARWIN_C_SOURCE
#elif defined(__FreeBSD__)
#define __BSD_VISIBLE 1
#endif
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#if defined(__linux__)
#include <sys/inotify.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
	defined(__APPLE__)
#include <sys/event.h>
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <libgen.h>
#include <pthread.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "termbox.h"
#include "util.h"

/* macros */
#define MAX_P 4096
#define MAX_N 255
#define MAX_USRI 32
#define MAX_EXT 4
#define MAX_STATUS 255
#define MAX_LINE 4096
#define MAX_USRN 32
#define MAX_GRPN 32
#define MAX_DTF 32
#define CURSOR cpane->direntr[cpane->hdir - 1]

/* enums */
enum { AddHi, NoHi }; /* add highlight in listdir */

/* typedef */
typedef struct {
	char name[MAX_N];
	gid_t group;
	mode_t mode;
	off_t size;
	time_t dt;
	uid_t user;
} Entry;

typedef struct {
	uint16_t fg;
	uint16_t bg;
} Cpair;

typedef struct {
	int pane_id;
	char dirn[MAX_P]; // dir name cwd
	char *filter;
	Entry *direntr; // dir entries
	int dirx; // pane cwd x pos
	int dirc; // dir entries sum
	int hdir; // highlighted dir
	int firstrow;
	int parent_firstrow;
	int parent_row; // FIX
	Cpair dircol;
	int inotify_wd;
	int event_fd;
} Pane;

typedef struct {
	uint32_t ch;
	char path[MAX_P];
} Bookmark;

typedef struct {
	const char **ext;
	size_t exlen;
	const void *v;
} Rule;

typedef union {
	uint16_t key; /* one of the TB_KEY_* constants */
	uint32_t ch; /* unicode character */
} Evkey;

typedef struct {
	const Evkey evkey;
	void (*func)(void);
} Key;

/* function declarations */
static void print_tb(const char *, int, int, uint16_t, uint16_t);
static void printf_tb(int, int, Cpair, const char *, ...);
static void print_status(Cpair, const char *, ...);
static void print_xstatus(char, int);
static void print_error(char *);
static void print_prompt(char *);
static void print_info(char *);
static void print_row(Pane *, size_t, Cpair);
static void clear(int, int, int, uint16_t);
static void clear_status(void);
static void clear_pane(void);
static void add_hi(Pane *, size_t);
static void rm_hi(Pane *, size_t);
static int check_dir(char *);
static mode_t chech_execf(mode_t);
static int sort_name(const void *const, const void *const);
static void get_dirp(char *);
static char *get_ext(char *);
static int get_fdt(char *, time_t);
static char *get_fgrp(gid_t);
static char *get_fperm(mode_t);
static char *get_fsize(off_t);
static char *get_fullpath(char *, char *);
static char *get_fusr(uid_t);
static void get_dirsize(char *, off_t *);
static void get_hicol(Cpair *, mode_t);
static int delent(char *);
static void calcdir(void);
static void crnd(void);
static void crnf(void);
static void delfd(void);
static void mvbk(void);
static void mvbtm(void);
static void mvdwn(void);
static void mvdwns(void);
static void mvfwd(void);
static void mvmid(void);
static void mvtop(void);
static void mvup(void);
static void mvups(void);
static void scrdwn(void);
static void scrdwns(void);
static void scrup(void);
static void scrups(void);
static int get_usrinput(char *, size_t, const char *, ...);
static int frules(char *);
static int spawn(const void *, char *);
static int opnf(char *);
static int fsev_init(void);
static int addwatch(void);
static int read_events(void);
static void rmwatch(Pane *);
static void fsev_shdn(void);
static ssize_t findbm(uint32_t);
static void start_filter(void);
static void start_vmode(void);
static void exit_vmode(void);
static void selup(void);
static void seldwn(void);
static void selall(void);
static void selref(void);
static void selynk(void);
static void selcalc(void);
static void paste(void);
static void selmv(void);
static void seldel(void);
static void init_files(void);
static void free_files(void);
static void yank(void);
static void rname(void);
static void switch_pane(void);
static void quit(void);
static void grabkeys(struct tb_event *, Key *, size_t);
static void *read_th(void *arg);
static void start_ev(void);
static void refresh_pane(void);
static void set_direntr(struct dirent *, DIR *, char *);
static int listdir(int);
static void t_resize(void);
static void set_panes(void);
static void draw_frame(void);
static void start(void);

/* global variables */
static pthread_t fsev_thread;
static Pane pane_r, pane_l, *cpane;
static char *editor[2];
static char fed[] = "vi";
static int theight, twidth, scrheight;
static size_t selection_size = 0;
static char yank_file[MAX_P];
static int *selection;
static int cont_vmode = 0;
static char **selected_files;
#if defined _SYS_INOTIFY_H
#define READEVSZ 16
static int inotify_fd;
#elif defined _SYS_EVENT_H_
#define READEVSZ 0
static int kq;
struct kevent evlist[2]; /* events we want to monitor */
struct kevent chlist[2]; /* events that were triggered */
static struct timespec gtimeout;
#endif
#if defined(__linux__) || defined (__FreeBSD__)
#define OFF_T "%ld"
#elif defined(__NetBSD__) || defined(__OpenBSD__) || defined(__APPLE__)
#define OFF_T "%lld"
#endif

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
printf_tb(int x, int y, Cpair col, const char *fmt, ...)
{
	char buf[MAX_LINE];
	va_list vl;
	va_start(vl, fmt);
	(void)vsnprintf(buf, MAX_LINE, fmt, vl);
	va_end(vl);
	print_tb(buf, x, y, col.fg, col.bg);
}

static void
print_status(Cpair col, const char *fmt, ...)
{
	char buf[MAX_STATUS];
	va_list vl;
	va_start(vl, fmt);
	(void)vsnprintf(buf, MAX_STATUS, fmt, vl);
	va_end(vl);
	clear_status();
	print_tb(buf, 1, theight - 1, col.fg, col.bg);
}

static void
print_xstatus(char c, int x)
{
	uint32_t uni = 0;
	(void)tb_utf8_char_to_unicode(&uni, &c);
	tb_change_cell(x, theight - 1, uni, cstatus.fg, cstatus.bg);
}

static void
print_error(char *errmsg)
{
	print_status(cerr, errmsg);
}

static void
print_prompt(char *prompt)
{
	print_status(cprompt, prompt);
}

static void
print_info(char *dirsize)
{
	char *sz, *ur, *gr, *dt, *prm;

	dt = ecalloc(MAX_DTF, sizeof(char));

	prm = get_fperm(CURSOR.mode);
	ur = get_fusr(CURSOR.user);
	gr = get_fgrp(CURSOR.group);

	if (get_fdt(dt, CURSOR.dt) < 0)
		*dt = '\0';

	if (S_ISREG(CURSOR.mode)) {
		sz = get_fsize(CURSOR.size);
	} else {
		if (dirsize == NULL) {
			sz = ecalloc(1, sizeof(char));
			*sz = '\0';
		} else {
			sz = dirsize;
		}
	}

	print_status(cstatus, "%02d/%02d %s %s:%s %s %s", cpane->hdir,
		cpane->dirc, prm, ur, gr, dt, sz);

	free(prm);
	free(ur);
	free(gr);
	free(dt);
	free(sz);
}

static void
print_row(Pane *pane, size_t entpos, Cpair col)
{
	int x, y;
	char *result;
	char buf[MAX_P];
	char lnk_full[MAX_P];
	int width;

	width = (twidth / 2) - 4;
	result = basename(pane->direntr[entpos].name);
	x = pane->dirx;
	y = entpos - cpane->firstrow + 1;

	if (S_ISLNK(pane->direntr[entpos].mode) != 0) {
		if (realpath(pane->direntr[entpos].name, buf) != NULL) {
			(void)snprintf(
				lnk_full, MAX_N, "%s -> %s", result, buf);
			result = lnk_full;
		}
	}

	printf_tb(x, y, col, "%*.*s", ~width, width, result);
}

static void
clear(int sx, int ex, int y, uint16_t bg)
{
	/* clear line from to */
	/* x = line number vertical */
	/* y = column number horizontal */
	int i;
	for (i = sx; i < ex; i++) {
		tb_change_cell(i, y, 0x0000, TB_DEFAULT, bg);
	}
}

static void
clear_status(void)
{
	clear(1, twidth - 1, theight - 1, cstatus.bg);
}

static void
clear_pane(void)
{
	int i, ex, y;
	y = 0, i = 0, ex = 0;

	if (cpane->pane_id == pane_l.pane_id)
		ex = (twidth / 2) - 1;
	else if (cpane->pane_id == pane_r.pane_id)
		ex = twidth - 1;

	while (i < scrheight) {
		clear(cpane->dirx, ex, y, TB_DEFAULT);
		i++;
		y++;
	}

	/* draw top line */
	for (y = cpane->dirx; y < ex; ++y) {
		tb_change_cell(y, 0, u_hl, cframe.fg, cframe.bg);
	}
}

static void
add_hi(Pane *pane, size_t entpos)
{
	Cpair col;
	get_hicol(&col, pane->direntr[entpos].mode);
	col.fg |= TB_REVERSE | TB_BOLD;
	col.bg |= TB_REVERSE;
	print_row(pane, entpos, col);
}

static void
rm_hi(Pane *pane, size_t entpos)
{
	Cpair col;
	get_hicol(&col, pane->direntr[entpos].mode);
	print_row(pane, entpos, col);
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
	mode_t data1 = (*(Entry *)A).mode;
	mode_t data2 = (*(Entry *)B).mode;

	if (data1 < data2) {
		return -1;
	} else if (data1 == data2) {
		result = strncmp((*(Entry *)A).name, (*(Entry *)B).name, MAX_N);
		return result;
	} else {
		return 1;
	}
}

static void
get_dirp(char *cdir)
{
	int counter, len, i;

	counter = 0;
	len = strnlen(cdir, MAX_P);
	if (len == 1)
		return;

	for (i = len - 1; i > 1; i--) {
		if (cdir[i] == '/')
			break;
		else
			counter++;
	}

	cdir[len - counter - 1] = '\0';
}

static char *
get_ext(char *str)
{
	char *ext;
	char dot;
	size_t counter, len, i;

	dot = '.';
	counter = 0;
	len = strnlen(str, MAX_N);

	for (i = len - 1; i > 0; i--) {
		if (str[i] == dot) {
			break;
		} else {
			counter++;
		}
	}

	ext = ecalloc(MAX_EXT + 1, sizeof(char));
	strncpy(ext, &str[len - counter], MAX_EXT);
	ext[MAX_EXT] = '\0';
	return ext;
}

static int
get_fdt(char *result, time_t status)
{
	struct tm lt;
	localtime_r(&status, &lt);
	return strftime(result, MAX_DTF, dtfmt, &lt);
}

static char *
get_fgrp(gid_t status)
{
	char *result;
	struct group *gr;

	result = ecalloc(MAX_GRPN, sizeof(char));
	gr = getgrgid(status);
	if (gr == NULL)
		(void)snprintf(result, MAX_GRPN, "%u", status);
	else
		strncpy(result, gr->gr_name, MAX_GRPN);

	result[MAX_GRPN - 1] = '\0';
	return result;
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

	if (snprintf(result, result_len, OFF_T "%c", size, unit) < 0)
		strncat(result, "???", result_len);

	return result;
}

static char *
get_fullpath(char *first, char *second)
{
	char *full_path;

	full_path = ecalloc(MAX_P, sizeof(char));

	if (strncmp(first, "/", MAX_P) == 0)
		(void)snprintf(full_path, MAX_P, "/%s", second);
	else
		(void)snprintf(full_path, MAX_P, "%s/%s", first, second);

	return full_path;
}

static char *
get_fusr(uid_t status)
{
	char *result;
	struct passwd *pw;

	result = ecalloc(MAX_USRN, sizeof(char));
	pw = getpwuid(status);
	if (pw == NULL)
		(void)snprintf(result, MAX_USRN, "%u", status);
	else
		strncpy(result, pw->pw_name, MAX_USRN);

	result[MAX_USRN - 1] = '\0';
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
	if (dir == NULL) {
		return;
	}

	while ((entry = readdir(dir)) != 0) {
		if ((strncmp(entry->d_name, ".", 2) == 0 ||
			    strncmp(entry->d_name, "..", 3) == 0))
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
get_hicol(Cpair *col, mode_t mode)
{
	*col = cfile;
	if (S_ISDIR(mode))
		*col = cdir;
	else if (S_ISLNK(mode))
		*col = cother;
	else if (chech_execf(mode) > 0)
		*col = cexec;
}

static int
delent(char *fullpath)
{
	char *inp_conf;
	int conf_len = 4;
	char conf[] = "yes";

	inp_conf = ecalloc(conf_len, sizeof(char));
	if ((get_usrinput(inp_conf, conf_len, "delete file (yes) ?") < 0) ||
		(strncmp(inp_conf, conf, conf_len) != 0)) {
		free(inp_conf);
		return 1; /* canceled by user or wrong inp_conf */
	}
	free(inp_conf);

	return spawn(rm_cmd, fullpath);
}

static void
calcdir(void)
{
	if (!S_ISDIR(CURSOR.mode))
		return;

	off_t *fullsize;
	char *csize;

	fullsize = ecalloc(1, sizeof(off_t));
	get_dirsize(CURSOR.name, fullsize);
	csize = get_fsize(*fullsize);

	CURSOR.size = *fullsize;
	print_info(csize);
	free(fullsize);
}

static void
crnd(void)
{
	char *user_input, *path;

	user_input = ecalloc(MAX_USRI, sizeof(char));
	if (get_usrinput(user_input, MAX_USRI, "new dir") < 0) {
		free(user_input);
		return;
	}

	path = ecalloc(MAX_P, sizeof(char));
	if (snprintf(path, MAX_P, "%s/%s", cpane->dirn, user_input) < 0) {
		free(user_input);
		free(path);
		return;
	}

	if (mkdir(path, ndir_perm) < 0)
		print_error(strerror(errno));

	free(user_input);
	free(path);
}

static void
crnf(void)
{
	char *user_input, *path;
	int rf;

	user_input = ecalloc(MAX_USRI, sizeof(char));
	if (get_usrinput(user_input, MAX_USRI, "new file") < 0) {
		free(user_input);
		return;
	}

	path = ecalloc(MAX_P, sizeof(char));
	if (snprintf(path, MAX_P, "%s/%s", cpane->dirn, user_input) < 0) {
		free(user_input);
		free(path);
		return;
	}

	rf = open(path, O_CREAT | O_EXCL, nf_perm);

	if (rf < 0)
		print_error(strerror(errno));
	else if (close(rf) < 0)
		print_error(strerror(errno));

	free(user_input);
	free(path);
}

static void
delfd(void)
{
	switch (delent(CURSOR.name)) {
	case -1:
		print_error(strerror(errno));
		break;
	case 0:
		if (BETWEEN(cpane->hdir - 1, 1, cpane->dirc)) /* last entry */
			cpane->hdir--;
		break;
	}
}

static void
mvbk(void)
{
	if (cpane->dirn[0] == '/' && cpane->dirn[1] == '\0') { /* cwd = / */
		return;
	}

	get_dirp(cpane->dirn);
	if (check_dir(cpane->dirn) < 0) {
		print_error(strerror(errno));
		return;
	}

	rmwatch(cpane);
	cpane->firstrow = cpane->parent_firstrow;
	cpane->hdir = cpane->parent_row;
	if (listdir(AddHi) < 0)
		print_error(strerror(errno));
	cpane->parent_firstrow = 0;
	cpane->parent_row = 1;
}

static void
mvbtm(void)
{
	if (cpane->dirc < 1)
		return;
	if (cpane->dirc > scrheight) {
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir = cpane->dirc;
		cpane->firstrow = cpane->dirc - scrheight + 1;
		refresh_pane();
		add_hi(cpane, cpane->hdir - 1);
	} else {
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir = cpane->dirc;
		add_hi(cpane, cpane->hdir - 1);
	}
	print_info(NULL);
}

static void
mvdwn(void)
{
	if (cpane->dirc < 1)
		return;
	if (cpane->dirc < scrheight && cpane->hdir < cpane->dirc) {
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir++;
		add_hi(cpane, cpane->hdir - 1);
	} else {
		mvdwns(); /* scroll */
	}
	print_info(NULL);
}

static void
mvdwns(void)
{
	int real;
	real = cpane->hdir - 1 - cpane->firstrow;

	if (real > scrheight - 3 - scrsp && cpane->hdir + scrsp < cpane->dirc) {
		cpane->firstrow++;
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir++;
		refresh_pane();
		add_hi(cpane, cpane->hdir - 1);
	} else if (cpane->hdir < cpane->dirc) {
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir++;
		add_hi(cpane, cpane->hdir - 1);
	}
}

static void
mvfwd(void)
{
	rmwatch(cpane);
	if (cpane->dirc < 1)
		return;
	int s;

	switch (check_dir(CURSOR.name)) {
	case 0:
		strncpy(cpane->dirn, CURSOR.name, MAX_P);
		cpane->parent_row = cpane->hdir;
		cpane->parent_firstrow = cpane->firstrow;
		cpane->hdir = 1;
		cpane->firstrow = 0;
		if (listdir(AddHi) < 0)
			print_error(strerror(errno));
		break;
	case 1: /* not a directory open file */
		tb_shutdown();
		s = opnf(CURSOR.name);
		if (tb_init() != 0)
			die("tb_init");
		t_resize();
		if (s < 0)
			print_error("process failed non-zero exit");
		break;
	case -1: /* failed to open directory */
		print_error(strerror(errno));
	}
}

static void
mvmid(void)
{
	if (cpane->dirc < 1)
		return;
	rm_hi(cpane, cpane->hdir - 1);
	if (cpane->dirc < scrheight / 2)
		cpane->hdir = (cpane->dirc + 1) / 2;
	else
		cpane->hdir = (scrheight / 2) + cpane->firstrow;
	add_hi(cpane, cpane->hdir - 1);
	print_info(NULL);
}

static void
mvtop(void)
{
	if (cpane->dirc < 1)
		return;
	if (cpane->dirc > scrheight) {
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir = 1;
		cpane->firstrow = 0;
		refresh_pane();
		add_hi(cpane, cpane->hdir - 1);
	} else {
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir = 1;
		add_hi(cpane, cpane->hdir - 1);
		print_info(NULL);
	}
}

static void
mvup(void)
{
	if (cpane->dirc < 1)
		return;
	if (cpane->dirc < scrheight && cpane->hdir > 1) {
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir--;
		add_hi(cpane, cpane->hdir - 1);
	} else {
		mvups(); /* scroll */
	}
	print_info(NULL);
}

static void
mvups(void)
{
	size_t real;
	real = cpane->hdir - 1 - cpane->firstrow;

	if (cpane->firstrow > 0 && real < 1 + scrsp) {
		cpane->firstrow--;
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir--;
		refresh_pane();
		add_hi(cpane, cpane->hdir - 1);
	} else if (cpane->hdir > 1) {
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir--;
		add_hi(cpane, cpane->hdir - 1);
	}
}

static void
scrdwn(void)
{
	if (cpane->dirc < 1)
		return;
	if (cpane->dirc < scrheight && cpane->hdir < cpane->dirc) {
		if (cpane->hdir < cpane->dirc - scrmv) {
			rm_hi(cpane, cpane->hdir - 1);
			cpane->hdir += scrmv;
			add_hi(cpane, cpane->hdir - 1);
		} else {
			mvbtm();
		}
	} else {
		scrdwns();
	}
	print_info(NULL);
}

static void
scrdwns(void)
{
	int real, dynmv;

	real = cpane->hdir - cpane->firstrow;
	dynmv = MIN(cpane->dirc - cpane->hdir - cpane->firstrow, scrmv);

	if (real + scrmv + 1 > scrheight &&
		cpane->hdir + scrsp + scrmv < cpane->dirc) { /* scroll */
		cpane->firstrow += dynmv;
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir += scrmv;
		refresh_pane();
		add_hi(cpane, cpane->hdir - 1);
	} else {
		if (cpane->hdir < cpane->dirc - scrmv - 1) {
			rm_hi(cpane, cpane->hdir - 1);
			cpane->hdir += scrmv;
			add_hi(cpane, cpane->hdir - 1);
		} else {
			mvbtm();
		}
	}
}

static void
scrup(void)
{
	if (cpane->dirc < 1)
		return;
	if (cpane->dirc < scrheight && cpane->hdir > 1) {
		if (cpane->hdir > scrmv) {
			rm_hi(cpane, cpane->hdir - 1);
			cpane->hdir = cpane->hdir - scrmv;
			add_hi(cpane, cpane->hdir - 1);
			print_info(NULL);
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
	int real, dynmv;
	real = cpane->hdir - cpane->firstrow;
	dynmv = MIN(cpane->firstrow, scrmv);

	if (cpane->firstrow > 0 && real < scrmv + scrsp) {
		cpane->firstrow -= dynmv;
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir -= scrmv;
		refresh_pane();
		add_hi(cpane, cpane->hdir - 1);
	} else {
		if (cpane->hdir > scrmv + 1) {
			rm_hi(cpane, cpane->hdir - 1);
			cpane->hdir -= scrmv;
			add_hi(cpane, cpane->hdir - 1);
		} else {
			mvtop();
		}
	}
}

static int
get_usrinput(char *out, size_t sout, const char *fmt, ...)
{
	int height = tb_height();
	size_t startat;
	struct tb_event fev;
	size_t counter = (size_t)1;
	char empty = ' ';
	int x = 0;
	int name_size = 0;
	char buf[256];

	clear_status();

	va_list vl;
	Cpair col;
	col = cprompt;
	va_start(vl, fmt);
	name_size = vsnprintf(buf, sizeof(buf), fmt, vl);
	va_end(vl);
	print_tb(buf, 1, height - 1, col.fg, col.bg);
	startat = name_size + 1;
	tb_set_cursor((int)(startat + 1), height - 1);
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
					out[x - 1] = '\0';
					counter--;
					x--;
					print_xstatus(empty, startat + counter);
					tb_set_cursor(
						startat + counter, theight - 1);
				}

			} else if (fev.key == TB_KEY_ENTER) {
				tb_set_cursor(-1, -1);
				out[counter - 1] = '\0';
				return 0;

			} else {
				if (counter < sout) {
					print_xstatus((char)fev.ch,
						(startat + counter));
					out[x] = (char)fev.ch;
					tb_set_cursor((startat + counter + 1),
						theight - 1);
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
frules(char *ex)
{
	size_t c, d;

	for (c = 0; c < LEN(rules); c++)
		for (d = 0; d < rules[c].exlen; d++)
			if (strncmp(rules[c].ext[d], ex, MAX_EXT) == 0)
				return c;
	return -1;
}

static int
spawn(const void *v, char *fn)
{
	int ws, x, argc;
	pid_t pid, r;

	x = 0;
	argc = 0;

	/* count args */
	while (((char **)v)[x++] != NULL)
		argc++;

	char *argv[argc + 2];
	for (x = 0; x < argc; x++)
		argv[x] = ((char **)v)[x];

	argv[argc] = fn;
	argv[argc + 1] = NULL;

	pid = fork();
	switch (pid) {
	case -1:
		return -1;
	case 0:
		execvp(argv[0], argv);
		exit(EXIT_SUCCESS);
	default:
		while ((r = waitpid(pid, &ws, 0)) == -1 && errno == EINTR)
			continue;
		if (r == -1)
			return -1;
		if ((WIFEXITED(ws) != 0) && (WEXITSTATUS(ws) != 0))
			return -1;
	}
	return 0;
}

static int
opnf(char *fn)
{
	char *ex;
	int c;

	ex = get_ext(fn);
	c = frules(ex);
	free(ex);

	if (c < 0) /* extension not found open in editor */
		return spawn(editor, fn);
	else
		return spawn((char **)rules[c].v, fn);
}

static int
fsev_init(void)
{
#if defined _SYS_INOTIFY_H
	inotify_fd = inotify_init();
	if (inotify_fd < 0)
		return -1;
#elif defined _SYS_EVENT_H_
	gtimeout.tv_sec = 1;
	kq = kqueue();
	if (kq < 0)
		return -1;
#endif
	return 0;
}

static int
addwatch(void)
{
#if defined _SYS_INOTIFY_H
	return cpane->inotify_wd = inotify_add_watch(inotify_fd, cpane->dirn,
		       IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_CREATE |
			       IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF);
#elif defined _SYS_EVENT_H_
	cpane->event_fd = open(cpane->dirn, O_RDONLY);
	if (cpane->event_fd < 0)
		return cpane->event_fd;
	EV_SET(&evlist[cpane->pane_id], cpane->event_fd, EVFILT_VNODE,
		EV_ADD | EV_CLEAR,
		NOTE_DELETE | NOTE_EXTEND | NOTE_LINK | NOTE_RENAME |
			NOTE_REVOKE | NOTE_WRITE,
		0, NULL);
	return 0;
#endif
}

static int
read_events(void)
{
#if defined _SYS_INOTIFY_H
	char *p;
	ssize_t r;
	struct inotify_event *event;
	const size_t events = 32;
	const size_t evbuflen =
		events * (sizeof(struct inotify_event) + MAX_N + 1);
	char buf[evbuflen];

	if (cpane->inotify_wd < 0)
		return -1;
	r = read(inotify_fd, buf, evbuflen);
	if (r <= 0)
		return r;

	for (p = buf; p < buf + r;) {
		event = (struct inotify_event *)p;
		if (!event->wd)
			break;
		if (event->mask) {
			return r;
		}

		p += sizeof(struct inotify_event) + event->len;
	}
#elif defined _SYS_EVENT_H_
	return kevent(kq, evlist, 2, chlist, 2, &gtimeout);
#endif
	return -1;
}

static void
rmwatch(Pane *pane)
{
// 	if (pane->inotify_wd >= 0)
// 		inotify_rm_watch(inotify_fd, pane->inotify_wd);
#if defined _SYS_EVENT_H_
	close(pane->event_fd);
#endif
}

static void
fsev_shdn(void)
{
	pthread_cancel(fsev_thread);
#ifndef __APPLE__
	pthread_join(fsev_thread, NULL);
#endif
	rmwatch(&pane_l);
	rmwatch(&pane_r);
#if defined _SYS_INOTIFY_H
	close(inotify_fd);
#elif defined _SYS_EVENT_H_
	close(kq);
#endif
}

static ssize_t
findbm(uint32_t event)
{
	ssize_t i;

	for (i = 0; i < (ssize_t)LEN(bmarks); i++) {
		if (event == bmarks[i].ch) {
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
start_filter(void)
{
	if (cpane->dirc < 1)
		return;
	char *user_input;
	user_input = ecalloc(MAX_USRI, sizeof(char));
	if (get_usrinput(user_input, MAX_USRI, "filter") < 0) {
		free(user_input);
		return;
	}
	cpane->filter = user_input;
	if (listdir(AddHi) < 0)
		print_error("no match");
	cpane->filter = NULL;
	free(user_input);
}

static void
start_vmode(void)
{
	struct tb_event fev;
	if (selection != NULL) {
		free(selection);
		selection = NULL;
	}

	selection = ecalloc(cpane->dirc, sizeof(size_t));
	selection[0] = cpane->hdir;
	cont_vmode = 0;
	print_prompt("-- VISUAL --");
	tb_present();
	while (tb_poll_event(&fev) != 0) {
		switch (fev.type) {
		case TB_EVENT_KEY:
			grabkeys(&fev, vkeys, vkeyslen);
			if (cont_vmode == -1)
				return;
			tb_present();
			break;
		}
	}
}

static void
exit_vmode(void)
{
	refresh_pane();
	add_hi(cpane, cpane->hdir - 1);
	cont_vmode = -1;
}

static void
selup(void)
{
	mvup();
	print_prompt("-- VISUAL --");
	int index = abs(cpane->hdir - selection[0]);

	if (cpane->hdir < selection[0]) {
		selection[index] = cpane->hdir;
		add_hi(cpane, selection[index]);
	} else if (index < cpane->dirc) {
		selection[index + 1] = 0;
	}
	if (cpane->dirc >= scrheight ||
		cpane->hdir <= 1) { /* rehighlight all if scrolling */
		selref();
	}
}

static void
seldwn(void)
{
	mvdwn();
	print_prompt("-- VISUAL --");
	int index = abs(cpane->hdir - selection[0]);

	if (cpane->hdir > selection[0]) {
		selection[index] = cpane->hdir;
		add_hi(cpane, selection[index] - 2);
	} else {
		selection[index + 1] = 0;
	}
	if (cpane->dirc >= scrheight ||
		cpane->hdir >= cpane->dirc) { /* rehighlight all if scrolling */
		selref();
	}
}

static void
selall(void)
{
	int i;
	for (i = 0; i < cpane->dirc; i++) {
		selection[i] = i + 1;
	}
	selref();
}

static void
selref(void)
{
	int i;
	for (i = 0; i < cpane->dirc; i++) {
		if (selection[i] < (scrheight + cpane->firstrow) &&
			selection[i] >
				cpane->firstrow) { /* checks if in the frame of the directories */
			add_hi(cpane, selection[i] - 1);
		}
	}
}

static void
selcalc(void)
{
	int j;
	selection_size = 0;

	for (j = 0; j < cpane->dirc; j++) { /* calculate used selection size */
		if (selection[j] != 0)
			selection_size++;
		else
			break;
	}
}

static void
free_files(void)
{
	size_t i;

	if (selected_files != NULL) {
		for (i = 0; i < selection_size; i++) {
			free(selected_files[i]);
			selected_files[i] = NULL;
		}
		free(selected_files);
		selected_files = NULL;
	}
}

static void
init_files(void)
{
	size_t i;
	free_files();

	selcalc();
	selected_files = ecalloc(selection_size, sizeof(char *));

	for (i = 0; i < selection_size; i++) {
		selected_files[i] = ecalloc(MAX_P, sizeof(char));
		strncpy(selected_files[i],
			cpane->direntr[selection[i] - 1].name,
			MAX_P); /* TODO use pointer */
	}
}

static void
selynk(void)
{
	init_files();
	refresh_pane();
	add_hi(cpane, cpane->hdir - 1);
	print_status(cprompt, "%zu files are yanked", selection_size);
	cont_vmode = -1;
}

static void
seldel(void)
{
	char *inp_conf;
	int conf_len = 4;
	char conf[] = "yes";
	size_t i;

	inp_conf = ecalloc(conf_len, sizeof(char));
	if ((get_usrinput(inp_conf, conf_len, "delete file (yes) ?") < 0) ||
		(strncmp(inp_conf, conf, conf_len) != 0)) {
		free(inp_conf);
		return; /* canceled by user or wrong inp_conf */
	}
	free(inp_conf);

	init_files();
	for (i = 0; i < selection_size; i++) {
		spawn(rm_cmd, selected_files[i]);
	}

	cpane->hdir = cpane->dirc - selection_size;
	print_status(cprompt, "%zu files are deleted", selection_size);
	free_files();
	cont_vmode = -1;
}

static void
paste(void)
{
	size_t i;
	if (strnlen(yank_file, MAX_P) != 0) {
		print_status(cprompt, "coping");
		if (spawn(cp_cmd, cpane->dirn) != 0)
			print_error("coping failed");
		else
			print_status(cprompt, "file copied");
		yank_file[0] = '\0'; /* set yank_file len 0 */
		return;
	}

	print_error("nothing to paste");

	if (selected_files == NULL)
		return;

	for (i = 0; i < selection_size; i++) {
		char *selcp_cmd[] = { "cp", "-r", selected_files[i],
			cpane->dirn, NULL };
		spawn(selcp_cmd, NULL);
	}
	print_status(cprompt, "%zu files are copied", selection_size);
	free_files();
}

static void
selmv(void)
{
	size_t i;

	if (strnlen(yank_file, MAX_P) != 0) {
		print_status(cprompt, "moving");
		if (spawn(mv_cmd, cpane->dirn) != 0)
			print_error("moving failed");
		else
			print_status(cprompt, "file moved");
		yank_file[0] = '\0'; /* set yank_file len 0 */
		return;
	}

	print_error("nothing to move");

	if (selected_files == NULL)
		return;

	for (i = 0; i < selection_size; i++) {
		char *selmv_cmd[] = { "mv", selected_files[i], cpane->dirn,
			NULL };
		spawn(selmv_cmd, NULL);
	}
	print_status(cprompt, "%zu files are moved", selection_size);
	free_files();
}

static void
rname(void)
{
	char new_name[MAX_P];
	char *input_name;

	input_name = ecalloc(MAX_N, sizeof(char));

	if (get_usrinput(input_name, MAX_N, "rename: %s",
		    basename(CURSOR.name)) < 0) {
		free(input_name);
		return;
	}

	if (snprintf(new_name, MAX_P, "%s/%s", cpane->dirn, input_name) < 0) {
		free(input_name);
		print_error(strerror(errno));
		return;
	}

	char *rename_cmd[] = { "mv", CURSOR.name, new_name, NULL };
	if (spawn(rename_cmd, NULL) < 0)
		print_error(strerror(errno));

	free(input_name);
}

static void
yank(void)
{
	strncpy(yank_file, CURSOR.name, MAX_P);
	print_status(cprompt, "1 file is yanked", selection_size);
}

static void
switch_pane(void)
{
	if (cpane->dirc > 0)
		rm_hi(cpane, cpane->hdir - 1);
	if (cpane == &pane_l)
		cpane = &pane_r;
	else if (cpane == &pane_r)
		cpane = &pane_l;
	if (cpane->dirc > 0) {
		add_hi(cpane, cpane->hdir - 1);
		print_info(NULL);
	} else {
		clear_status();
	}
}

static void
quit(void)
{
	if (cont_vmode == -1) { /* check if selection was allocated */
		free(selection);
		if (selected_files != NULL)
			free_files();
	}
	free(pane_l.direntr);
	free(pane_r.direntr);
	fsev_shdn();
	tb_shutdown();
	exit(EXIT_SUCCESS);
}

static void
grabkeys(struct tb_event *event, Key *key, size_t max_keys)
{
	size_t i;
	ssize_t b;

	for (i = 0; i < max_keys; i++) {
		if (event->ch != 0) {
			if (event->ch == key[i].evkey.ch) {
				key[i].func();
				return;
			}
		} else if (event->key != 0) {
			if (event->key == key[i].evkey.key) {
				key[i].func();
				return;
			}
		}
	}

	/* bookmarks */
	b = findbm(event->ch);
	if (b < 0)
		return;
	rmwatch(cpane);
	strncpy(cpane->dirn, bmarks[b].path, MAX_P);
	cpane->firstrow = 0;
	cpane->parent_row = 1;
	cpane->hdir = 1;
	if (listdir(AddHi) < 0)
		print_error(strerror(errno));
}

void *
read_th(void *arg)
{
	int i;
	while (1) {

		i = read_events();

		if (i > READEVSZ) {
			listdir(AddHi);
			if (cpane == &pane_l) {
				cpane = &pane_r;
				if (listdir(NoHi) < 0)
					print_error(strerror(errno));
				cpane = &pane_l;
				if (listdir(AddHi) < 0)
					print_error(strerror(errno));
			} else if (cpane == &pane_r) {
				cpane = &pane_l;
				if (listdir(NoHi) < 0)
					print_error(strerror(errno));
				cpane = &pane_r;
				if (listdir(AddHi) < 0)
					print_error(strerror(errno));
			}
		}
		tb_present();
	}
	return arg;
}

static void
start_ev(void)
{
	struct tb_event ev;

	while (tb_poll_event(&ev) != 0) {
		switch (ev.type) {
		case TB_EVENT_KEY:
			grabkeys(&ev, nkeys, nkeyslen);
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
	width = (twidth / 2) - 4;
	Cpair col;

	y = 1;
	start_from = cpane->firstrow;
	dyn_max = MIN(cpane->dirc, (scrheight - 1) + cpane->firstrow);

	/* print each entry in directory */
	while (start_from < dyn_max) {
		get_hicol(&col, cpane->direntr[start_from].mode);
		print_row(cpane, start_from, col);
		start_from++;
		y++;
	}

	if (cpane->dirc > 0)
		print_info(NULL);
	else
		clear_status();

	/* print current directory title */
	cpane->dircol.fg |= TB_BOLD;
	printf_tb(cpane->dirx, 0, cpane->dircol, " %.*s ", width, cpane->dirn);
}

static void
set_direntr(struct dirent *entry, DIR *dir, char *filter)
{
	int i;
	char *tmpfull;
	struct stat status;

#define ADD_ENTRY                                           \
	tmpfull = get_fullpath(cpane->dirn, entry->d_name); \
	strncpy(cpane->direntr[i].name, tmpfull, MAX_N);    \
	if (lstat(tmpfull, &status) == 0) {                 \
		cpane->direntr[i].size = status.st_size;    \
		cpane->direntr[i].mode = status.st_mode;    \
		cpane->direntr[i].group = status.st_gid;    \
		cpane->direntr[i].user = status.st_uid;     \
		cpane->direntr[i].dt = status.st_mtime;     \
	}                                                   \
	i++;                                                \
	free(tmpfull);

	i = 0;
	cpane->direntr = erealloc(cpane->direntr, cpane->dirc * sizeof(Entry));
	while ((entry = readdir(dir)) != 0) {
		if ((strncmp(entry->d_name, ".", 2) == 0 ||
			    strncmp(entry->d_name, "..", 3) == 0))
			continue;

		if (filter == NULL) {
			ADD_ENTRY
		} else if (filter != NULL) {
			if (strcasestr(entry->d_name, filter) != NULL) {
				ADD_ENTRY
			}
		}
	}

	cpane->dirc = i;
}

static int
listdir(int hi)
{
	DIR *dir;
	struct dirent *entry;
	int width;
	int filtercount = 0;
	size_t oldc = cpane->dirc;

	width = (twidth / 2) - 4;
	cpane->dirc = 0;

	dir = opendir(cpane->dirn);
	if (dir == NULL)
		return -1;

	/* get content and filter sum */
	while ((entry = readdir(dir)) != 0) {
		if (cpane->filter != NULL) {
			if (strcasestr(entry->d_name, cpane->filter) != NULL)
				filtercount++;
		} else { /* no filter */
			cpane->dirc++;
		}
	}

	if (cpane->filter == NULL) {
		clear_pane();
		cpane->dirc -= 2;
	}

	if (cpane->filter != NULL) {
		if (filtercount > 0) {
			cpane->dirc = filtercount;
			clear_pane();
			cpane->hdir = 1;
		} else if (filtercount == 0) {
			if (closedir(dir) < 0)
				return -1;
			cpane->dirc = oldc;
			return -1;
		}
	}

	/* print current directory title */
	cpane->dircol.fg |= TB_BOLD;
	printf_tb(cpane->dirx, 0, cpane->dircol, " %.*s ", width, cpane->dirn);

	if (cpane->filter == NULL) /* dont't watch when filtering */
		if (addwatch() < 0)
			print_error("can't add watch");

	/* empty directory */
	if (cpane->dirc == 0) {
		clear_status();
		if (closedir(dir) < 0)
			return -1;
		return 0;
	}

	rewinddir(dir); /* reset position */
	set_direntr(entry, dir, cpane->filter); /* create array of entries */
	qsort(cpane->direntr, cpane->dirc, sizeof(Entry), sort_name);
	refresh_pane();

	if (hi == AddHi)
		add_hi(cpane, cpane->hdir - 1);

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
	pane_r.dirx = (twidth / 2) + 2;

	if (cpane == &pane_l) {
		cpane = &pane_r;
		refresh_pane();
		cpane = &pane_l;
		refresh_pane();
		if (cpane->dirc > 0)
			add_hi(&pane_l, pane_l.hdir - 1);
	} else if (cpane == &pane_r) {
		cpane = &pane_l;
		refresh_pane();
		cpane = &pane_r;
		refresh_pane();
		if (cpane->dirc > 0)
			add_hi(&pane_r, pane_r.hdir - 1);
	}

	tb_present();
}

static void
get_editor(void)
{
	editor[0] = getenv("EDITOR");
	editor[1] = NULL;

	if (editor[0] == NULL)
		editor[0] = fed;
}

static void
set_panes(void)
{
	char *home;
	char cwd[MAX_P];

	home = getenv("HOME");
	if (home == NULL)
		home = "/";
	if ((getcwd(cwd, sizeof(cwd)) == NULL))
		strncpy(cwd, home, MAX_P);

	pane_l.pane_id = 0;
	pane_l.dirx = 2;
	pane_l.dircol = cpanell;
	pane_l.firstrow = 0;
	pane_l.direntr = ecalloc(0, sizeof(Entry));
	strncpy(pane_l.dirn, cwd, MAX_P);
	pane_l.hdir = 1;
	pane_l.inotify_wd = -1;
	pane_l.parent_row = 1;

	pane_r.pane_id = 1;
	pane_r.dirx = (twidth / 2) + 2;
	pane_r.dircol = cpanelr;
	pane_r.firstrow = 0;
	pane_r.direntr = ecalloc(0, sizeof(Entry));
	strncpy(pane_r.dirn, home, MAX_P);
	pane_r.hdir = 1;
	pane_r.inotify_wd = -1;
	pane_r.parent_row = 1;
}

static void
draw_frame(void)
{
	int i;
	theight = tb_height();
	twidth = tb_width();
	scrheight = theight - 2;

	/* 2 horizontal lines */
	for (i = 1; i < twidth - 1; ++i) {
		tb_change_cell(i, 0, u_hl, cframe.fg, cframe.bg);
		tb_change_cell(i, theight - 2, u_hl, cframe.fg, cframe.bg);
	}

	/* 4 vertical lines */
	for (i = 1; i < theight - 1; ++i) {
		tb_change_cell(0, i, u_vl, cframe.fg, cframe.bg);
		tb_change_cell(
			(twidth - 1) / 2, i - 1, u_vl, cframe.fg, cframe.bg);
		tb_change_cell(((twidth - 1) / 2) + 1, i - 1, u_vl, cframe.fg,
			cframe.bg);
		tb_change_cell(twidth - 1, i, u_vl, cframe.fg, cframe.bg);
	}

	/* 4 corners */
	tb_change_cell(0, 0, u_cnw, cframe.fg, cframe.bg);
	tb_change_cell(twidth - 1, 0, u_cne, cframe.fg, cframe.bg);
	tb_change_cell(0, theight - 2, u_csw, cframe.fg, cframe.bg);
	tb_change_cell(twidth - 1, theight - 2, u_cse, cframe.fg, cframe.bg);

	/* 2 middel top and bottom */
	tb_change_cell((twidth - 1) / 2, 0, u_mn, cframe.fg, cframe.bg);
	tb_change_cell(
		(twidth - 1) / 2, theight - 2, u_ms, cframe.fg, cframe.bg);
}

static void
start(void)
{
	if (tb_init() != 0)
		die("tb_init");
	if (tb_select_output_mode(TB_OUTPUT_256) != TB_OUTPUT_256)
		if (tb_select_output_mode(TB_OUTPUT_NORMAL) != TB_OUTPUT_NORMAL)
			die("output error");

	draw_frame();
	set_panes();
	get_editor();
	if (fsev_init() < 0)
		print_error(strerror(errno));
	cpane = &pane_r;
	if (listdir(NoHi) < 0)
		print_error(strerror(errno));
	cpane = &pane_l;
	if (listdir(AddHi) < 0)
		print_error(strerror(errno));
	tb_present();

	pthread_create(&fsev_thread, NULL, read_th, NULL);
	start_ev();
}

int
main(int argc, char *argv[])
{
#ifdef __OpenBSD__
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
