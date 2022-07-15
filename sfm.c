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

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <libgen.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
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
#define CURSOR(x) (x)->direntr[(x)->hdir - 1]

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
	int dirc; // dir entries sum
	int hdir; // highlighted dir
	int x_srt;
	int x_end;
	int firstrow;
	int parent_firstrow;
	int parent_row; // FIX
	Cpair dircol;
	int inotify_wd;
	int event_fd;
} Pane;

typedef struct {
	const char **ext;
	size_t exlen;
	const void *v;
	size_t vlen;
} Rule;

typedef union {
	uint16_t key; /* one of the TB_KEY_* constants */
	uint32_t ch; /* unicode character */
} Evkey;

typedef union {
	int i;
	const void *v;
} Arg;

typedef struct {
	const Evkey evkey;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

/* function declarations */
static void print_tb(const char *, int, int, uint16_t, uint16_t);
static void printf_tb(int, int, Cpair, const char *, ...);
static void print_status(Cpair, const char *, ...);
static void print_xstatus(char, int);
static void print_error(char *);
static void print_prompt(char *);
static void print_info(Pane *, char *);
static void print_row(Pane *, size_t, Cpair);
static void clear(int, int, int, uint16_t);
static void clear_status(void);
static void clear_pane(Pane *);
static void add_hi(Pane *, size_t);
static void rm_hi(Pane *, size_t);
static int check_dir(char *);
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
static void delent(const Arg *arg);
static void calcdir(const Arg *arg);
static void crnd(const Arg *arg);
static void crnf(const Arg *arg);
static void mv_ver(const Arg *arg);
static void mvbk(const Arg *arg);
static void mvbtm(const Arg *arg);
static void mvfwd(const Arg *arg);
static void mvtop(const Arg *arg);
static void bkmrk(const Arg *arg);
static int get_usrinput(char *, size_t, const char *, ...);
static int frules(char *);
static int spawn(const void *, size_t, const void *, size_t, char *, int);
static int opnf(char *);
static int fsev_init(void);
static int addwatch(Pane *);
static int read_events(void);
static void rmwatch(Pane *);
static void fsev_shdn(void);
static void toggle_df(const Arg *arg);
static void start_filter(const Arg *arg);
static void start_vmode(const Arg *arg);
static void exit_vmode(const Arg *arg);
static void start_change(const Arg *arg);
static void exit_change(const Arg *arg);
static void selup(const Arg *arg);
static void seldwn(const Arg *arg);
static void selall(const Arg *arg);
static void selref(void);
static void selynk(const Arg *arg);
static void selcalc(void);
static void paste(const Arg *arg);
static void selmv(const Arg *arg);
static void seldel(const Arg *arg);
static void init_files(void);
static void free_files(void);
static void yank(const Arg *arg);
static void rname(const Arg *arg);
static void chngo(const Arg *arg);
static void chngm(const Arg *arg);
static void chngf(const Arg *arg);
static void dupl(const Arg *arg);
static void switch_pane(const Arg *arg);
static void quit(const Arg *arg);
static void grabkeys(struct tb_event *, Key *, size_t);
static void *read_th(void *arg);
static void start_ev(void);
static void refresh_pane(Pane *);
static void set_direntr(Pane *, struct dirent *, DIR *, char *);
static int listdir(Pane *);
static void t_resize(void);
static void get_shell(void);
static void opnsh(const Arg *arg);
static void set_panes(void);
static void draw_frame(void);
static void refresh(const Arg *arg);
static void start(void);

/* global variables */
static pthread_t fsev_thread;
static Pane panes[2];
static Pane *cpane;
static int pane_idx;
static char *editor[2];
static char fed[] = "vi";
static char *shell[2];
static char sh[] = "/bin/sh";
static int theight, twidth, hwidth, scrheight;
static int *sel_indexes;
static size_t sel_len = 0;
static char **sel_files;
static int cont_vmode = 0;
static int cont_change = 0;
static pid_t fork_pid = 0, main_pid;
#if defined(_SYS_INOTIFY_H)
#define READEVSZ 16
static int inotify_fd;
#elif defined(_SYS_EVENT_H_)
#define READEVSZ 0
static int kq;
struct kevent evlist[2]; /* events we want to monitor */
struct kevent chlist[2]; /* events that were triggered */
static struct timespec gtimeout;
#endif
#if defined(__linux__) || defined(__FreeBSD__)
#define OFF_T "%ld"
#elif defined(__NetBSD__) || defined(__OpenBSD__) || defined(__APPLE__)
#define OFF_T "%lld"
#endif
enum { Left, Right }; /* panes */
enum { Wait, DontWait }; /* spawn forks */

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
print_info(Pane *pane, char *dirsize)
{
	char *sz, *ur, *gr, *dt, *prm;

	dt = ecalloc(MAX_DTF, sizeof(char));

	prm = get_fperm(CURSOR(pane).mode);
	ur = get_fusr(CURSOR(pane).user);
	gr = get_fgrp(CURSOR(pane).group);

	if (get_fdt(dt, CURSOR(pane).dt) < 0)
		*dt = '\0';

	if (S_ISREG(CURSOR(pane).mode)) {
		sz = get_fsize(CURSOR(pane).size);
	} else {
		if (dirsize == NULL) {
			sz = ecalloc(1, sizeof(char));
			*sz = '\0';
		} else {
			sz = dirsize;
		}
	}

	print_status(cstatus, "%02d/%02d %s %s:%s %s %s", pane->hdir,
		pane->dirc, prm, ur, gr, dt, sz);

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
	char *full_str, *rez_pth;
	char lnk_full[MAX_N];

	full_str = basename(pane->direntr[entpos].name);
	x = pane->x_srt;
	y = entpos - pane->firstrow + 1;

	if (S_ISLNK(pane->direntr[entpos].mode) != 0) {
		rez_pth = ecalloc(MAX_P, sizeof(char));
		if (realpath(pane->direntr[entpos].name, rez_pth) != NULL) {
			snprintf(
				lnk_full, MAX_N, "%s -> %s", full_str, rez_pth);
			full_str = lnk_full;
		}
		free(rez_pth);
	}

	printf_tb(x, y, col, "%*.*s", ~hwidth, hwidth, full_str);
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
clear_pane(Pane *pane)
{
	int i, y;
	y = 0, i = 0;

	while (i < scrheight) {
		clear(pane->x_srt, pane->x_end, y, TB_DEFAULT);
		i++;
		y++;
	}

	/* draw top line */
	for (y = pane->x_srt; y < pane->x_end; ++y) {
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
	for(char *p=ext; *p; p++) {
		*p=tolower(*(const unsigned char *)p);
	}
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
	switch (mode & S_IFMT) {
	case S_IFREG:
		*col = cfile;
		if ((S_IXUSR | S_IXGRP | S_IXOTH) & mode)
			*col = cexec;
		break;
	case S_IFDIR:
		*col = cdir;
		break;
	case S_IFLNK:
		*col = clnk;
		break;
	case S_IFBLK:
		*col = cblk;
		break;
	case S_IFCHR:
		*col = cchr;
		break;
	case S_IFIFO:
		*col = cifo;
		break;
	case S_IFSOCK:
		*col = csock;
		break;
	default:
		*col = cother;
		break;
	}
}

static void
delent(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	char *inp_conf;

	inp_conf = ecalloc(delconf_len, sizeof(char));
	if ((get_usrinput(inp_conf, delconf_len, "delete files(s) (%s) ?",
		     delconf) < 0) ||
		(strncmp(inp_conf, delconf, delconf_len) != 0)) {
		free(inp_conf);
		return; /* canceled by user or wrong inp_conf */
	}
	free(inp_conf);

	char *tmp[1];
	tmp[0] = CURSOR(cpane).name;
	if (spawn(rm_cmd, rm_cmd_len, tmp, 1, NULL, DontWait) < 0) {
		print_error(strerror(errno));
		return;
	}
}

static void
calcdir(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	if (!S_ISDIR(CURSOR(cpane).mode))
		return;

	off_t *fullsize;
	char *csize;

	fullsize = ecalloc(1, sizeof(off_t));
	get_dirsize(CURSOR(cpane).name, fullsize);
	csize = get_fsize(*fullsize);

	CURSOR(cpane).size = *fullsize;
	print_info(cpane, csize);
	free(fullsize);
}

static void
crnd(const Arg *arg)
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

	PERROR(mkdir(path, ndir_perm) < 0);

	free(user_input);
	free(path);
}

static void
crnf(const Arg *arg)
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
mv_ver(const Arg *arg)
{

	if (cpane->dirc < 1)
		return;
	if (cpane->hdir - arg->i < 1) /* first line */
		return;

	if (cpane->hdir - arg->i > cpane->dirc) /* last line */
		return;

	if (cpane->firstrow > 0 && arg->i > 0 &&
		cpane->hdir <= (cpane->firstrow + arg->i)) { /* scroll up */
		cpane->firstrow = cpane->firstrow - arg->i;
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir = cpane->hdir - arg->i;
		refresh_pane(cpane);
		add_hi(cpane, cpane->hdir - 1);
		return;
	}

	if (cpane->hdir - cpane->firstrow >= scrheight + arg->i &&
		arg->i < 0) { /* scroll down */
		cpane->firstrow = cpane->firstrow - arg->i;
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir = cpane->hdir - arg->i;
		refresh_pane(cpane);
		add_hi(cpane, cpane->hdir - 1);
		return;
	}

	rm_hi(cpane, cpane->hdir - 1);
	cpane->hdir = cpane->hdir - arg->i;
	add_hi(cpane, cpane->hdir - 1);
	print_info(cpane, NULL);
}

static void
mvbk(const Arg *arg)
{
	if (cpane->dirn[0] == '/' && cpane->dirn[1] == '\0') { /* cwd = / */
		return;
	}

	get_dirp(cpane->dirn);
	if (check_dir(cpane->dirn) < 0) {
		print_error(strerror(errno));
		return;
	}

	cpane->firstrow = cpane->parent_firstrow;
	cpane->hdir = cpane->parent_row;
	PERROR(listdir(cpane) < 0);
	cpane->parent_firstrow = 0;
	cpane->parent_row = 1;
}

static void
mvbtm(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	if (cpane->dirc > scrheight) {
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir = cpane->dirc;
		cpane->firstrow = cpane->dirc - scrheight + 1;
		refresh_pane(cpane);
		add_hi(cpane, cpane->hdir - 1);
	} else {
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir = cpane->dirc;
		add_hi(cpane, cpane->hdir - 1);
	}
	print_info(cpane, NULL);
}

static void
mvfwd(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	int s;

	switch (check_dir(CURSOR(cpane).name)) {
	case 0:
		strncpy(cpane->dirn, CURSOR(cpane).name, MAX_P);
		cpane->parent_row = cpane->hdir;
		cpane->parent_firstrow = cpane->firstrow;
		cpane->hdir = 1;
		cpane->firstrow = 0;
		PERROR(listdir(cpane) < 0);
		break;
	case 1: /* not a directory open file */
		tb_shutdown();
		s = opnf(CURSOR(cpane).name);
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
mvtop(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	if (cpane->dirc > scrheight) {
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir = 1;
		cpane->firstrow = 0;
		refresh_pane(cpane);
		add_hi(cpane, cpane->hdir - 1);
	} else {
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir = 1;
		add_hi(cpane, cpane->hdir - 1);
		print_info(cpane, NULL);
	}
}

static void
bkmrk(const Arg *arg)
{
	if (check_dir((char *)arg->v) != 0) {
		print_error(strerror(errno));
		return;
	}

	strncpy(cpane->dirn, (char *)arg->v, MAX_P);
	cpane->firstrow = 0;
	cpane->parent_row = 1;
	cpane->hdir = 1;
	PERROR(listdir(cpane) < 0);
}

static int
get_usrinput(char *result, size_t max_chars, const char *fmt, ...)
{
	char msg[MAX_N];
	size_t i, cpos, startat;
	struct tb_event fev;
	va_list vl;

	i = 0;
	cpos = 1;

	va_start(vl, fmt);
	startat = vsnprintf(msg, MAX_N, fmt, vl) + 1;
	va_end(vl);

	clear_status();
	print_tb(msg, 1, theight - 1, cprompt.fg, cprompt.bg);
	tb_set_cursor(startat + 1, theight - 1);
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
				if (BETWEEN(cpos, 2, max_chars)) {
					result[i - 1] = '\0';
					cpos--;
					i--;
					print_xstatus(' ', startat + cpos);
					tb_set_cursor(
						startat + cpos, theight - 1);
				}

			} else if (fev.key == TB_KEY_ENTER) {
				tb_set_cursor(-1, -1);
				result[cpos - 1] = '\0';
				return 0;

			} else if (fev.key) { /* disable other TB_KEY_* */
				break;

			} else {
				if (cpos < max_chars) {
					print_xstatus(
						(char)fev.ch, (startat + cpos));
					result[i] = (char)fev.ch;
					tb_set_cursor((startat + cpos + 1),
						theight - 1);
					cpos++;
					i++;
				}
			}

			tb_present();
			break;

		case TB_EVENT_RESIZE:
			t_resize();
			clear_status();
			print_tb(msg, 1, theight - 1, cprompt.fg, cprompt.bg);
			print_tb(result, startat + 1, theight - 1, cstatus.fg,
				cstatus.bg);
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
spawn(const void *com_argv, size_t com_argc, const void *f_argv, size_t f_argc,
	char *fn, int waiting)
{
	int ws;
	size_t argc;
	pid_t r;

	argc = com_argc + f_argc + 2;
	char *argv[argc];

	memcpy(argv, com_argv, com_argc * sizeof(char *)); /* command */
	memcpy(&argv[com_argc], f_argv, f_argc * sizeof(char *)); /* files */

	argv[argc - 2] = fn;
	argv[argc - 1] = NULL;

	fork_pid = fork();
	switch (fork_pid) {
	case -1:
		return -1;
	case 0:
		execvp(argv[0], argv);
		exit(EXIT_SUCCESS);
	default:
		if (waiting == Wait) {
			while ((r = waitpid(fork_pid, &ws, 0)) == -1 &&
				errno == EINTR)
				continue;
			if (r == -1)
				return -1;
			if ((WIFEXITED(ws) != 0) && (WEXITSTATUS(ws) != 0))
				return -1;
		}
	}
	fork_pid = 0; /* enable th_handler() */
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
		return spawn(editor, 1, NULL, 0, fn, Wait);
	else
		return spawn(
			(char **)rules[c].v, rules[c].vlen, NULL, 0, fn, Wait);
}

static void
opnsh(const Arg *arg)
{
	int s;

	tb_shutdown();
	chdir(cpane->dirn);
	s = spawn(shell, 1, NULL, 0, NULL, Wait);
	if (tb_init() != 0)
		die("tb_init");
	t_resize();
	if (s < 0)
		print_error("process failed non-zero exit");
}

static int
fsev_init(void)
{
#if defined(_SYS_INOTIFY_H)
	inotify_fd = inotify_init();
	if (inotify_fd < 0)
		return -1;
#elif defined(_SYS_EVENT_H_)
	gtimeout.tv_sec = 1;
	kq = kqueue();
	if (kq < 0)
		return -1;
#endif
	return 0;
}

static int
addwatch(Pane *pane)
{
#if defined(_SYS_INOTIFY_H)
	return pane->inotify_wd = inotify_add_watch(inotify_fd, pane->dirn,
		       IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_CREATE |
			       IN_ATTRIB | IN_DELETE | IN_DELETE_SELF |
			       IN_MOVE_SELF);
#elif defined(_SYS_EVENT_H_)
	pane->event_fd = open(pane->dirn, O_RDONLY);
	if (pane->event_fd < 0)
		return pane->event_fd;
	EV_SET(&evlist[pane->pane_id], pane->event_fd, EVFILT_VNODE,
		EV_ADD | EV_CLEAR,
		NOTE_DELETE | NOTE_EXTEND | NOTE_LINK | NOTE_RENAME |
			NOTE_ATTRIB | NOTE_REVOKE | NOTE_WRITE,
		0, NULL);
	return 0;
#endif
}

static int
read_events(void)
{
#if defined(_SYS_INOTIFY_H)
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
#elif defined(_SYS_EVENT_H_)
	return kevent(kq, evlist, 2, chlist, 2, &gtimeout);
#endif
	return -1;
}

static void
rmwatch(Pane *pane)
{
#if defined(_SYS_INOTIFY_H)
	if (pane->inotify_wd >= 0)
		inotify_rm_watch(inotify_fd, pane->inotify_wd);
#elif defined(_SYS_EVENT_H_)
	close(pane->event_fd);
#endif
}

static void
fsev_shdn(void)
{
	pthread_cancel(fsev_thread);
#if defined(__linux__)
	pthread_join(fsev_thread, NULL);
#endif
	rmwatch(&panes[Left]);
	rmwatch(&panes[Right]);
#if defined(_SYS_INOTIFY_H)
	close(inotify_fd);
#elif defined(_SYS_EVENT_H_)
	close(kq);
#endif
}

static void
toggle_df(const Arg *arg)
{
	show_dotfiles = !show_dotfiles;
	PERROR(listdir(&panes[Left]));
	PERROR(listdir(&panes[Right]));
	tb_present();
}

static void
start_filter(const Arg *arg)
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
	if (listdir(cpane) < 0)
		print_error("no match");
	cpane->filter = NULL;
	free(user_input);
}

static void
start_vmode(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	struct tb_event fev;
	if (sel_indexes != NULL) {
		free(sel_indexes);
		sel_indexes = NULL;
	}

	sel_indexes = ecalloc(cpane->dirc, sizeof(size_t));
	sel_indexes[0] = cpane->hdir;
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
exit_vmode(const Arg *arg)
{
	refresh_pane(cpane);
	add_hi(cpane, cpane->hdir - 1);
	cont_vmode = -1;
}

static void
start_change(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	struct tb_event fev;

	cont_change = 0;
	print_prompt("c [womf]");
	tb_present();
	while (tb_poll_event(&fev) != 0) {
		switch (fev.type) {
		case TB_EVENT_KEY:
			grabkeys(&fev, ckeys, ckeyslen);
			if (cont_change == -1)
				return;
			tb_present();
			break;
		}
	}
}

static void
exit_change(const Arg *arg)
{
	cont_change = -1;
	print_info(cpane, NULL);
}

static void
selup(const Arg *arg)
{
	mv_ver(arg);
	print_prompt("-- VISUAL --");
	int index = abs(cpane->hdir - sel_indexes[0]);

	if (cpane->hdir < sel_indexes[0]) {
		sel_indexes[index] = cpane->hdir;
		add_hi(cpane, sel_indexes[index]);
	} else if (index < cpane->dirc) {
		sel_indexes[index + 1] = 0;
	}
	if (cpane->dirc >= scrheight ||
		cpane->hdir <= 1) { /* rehighlight all if scrolling */
		selref();
	}
}

static void
seldwn(const Arg *arg)
{
	mv_ver(arg);
	print_prompt("-- VISUAL --");
	int index = abs(cpane->hdir - sel_indexes[0]);

	if (cpane->hdir > sel_indexes[0]) {
		sel_indexes[index] = cpane->hdir;
		add_hi(cpane, sel_indexes[index] - 2);
	} else {
		sel_indexes[index + 1] = 0;
	}
	if (cpane->dirc >= scrheight ||
		cpane->hdir >= cpane->dirc) { /* rehighlight all if scrolling */
		selref();
	}
}

static void
selall(const Arg *arg)
{
	int i;
	for (i = 0; i < cpane->dirc; i++) {
		sel_indexes[i] = i + 1;
	}
	selref();
}

static void
selref(void)
{
	int i;
	for (i = 0; i < cpane->dirc; i++) {
		if (sel_indexes[i] < (scrheight + cpane->firstrow) &&
			sel_indexes[i] >
				cpane->firstrow) { /* checks if in the frame of the directories */
			add_hi(cpane, sel_indexes[i] - 1);
		}
	}
}

static void
selcalc(void)
{
	int j;
	sel_len = 0;

	for (j = 0; j < cpane->dirc; j++) { /* calculate used selection size */
		if (sel_indexes[j] != 0)
			sel_len++;
		else
			break;
	}
}

static void
free_files(void)
{
	size_t i;

	if (sel_files != NULL) {
		for (i = 0; i < sel_len; i++) {
			free(sel_files[i]);
			sel_files[i] = NULL;
		}
		free(sel_files);
		sel_files = NULL;
	}
}

static void
init_files(void)
{
	size_t i;
	free_files();

	selcalc();
	sel_files = ecalloc(sel_len, sizeof(char *));

	for (i = 0; i < sel_len; i++) {
		sel_files[i] = ecalloc(MAX_P, sizeof(char));
		strncpy(sel_files[i], cpane->direntr[sel_indexes[i] - 1].name,
			MAX_P);
	}
}

static void
selynk(const Arg *arg)
{
	init_files();
	refresh_pane(cpane);
	add_hi(cpane, cpane->hdir - 1);
	print_status(cprompt, "%zu files are yanked", sel_len);
	cont_vmode = -1;
}

static void
seldel(const Arg *arg)
{
	char *inp_conf;

	inp_conf = ecalloc(delconf_len, sizeof(char));
	if ((get_usrinput(inp_conf, delconf_len, "delete files(s) (%s) ?",
		     delconf) < 0) ||
		(strncmp(inp_conf, delconf, delconf_len) != 0)) {
		free(inp_conf);
		return; /* canceled by user or wrong inp_conf */
	}
	free(inp_conf);

	init_files();

	if (spawn(rm_cmd, rm_cmd_len, sel_files, sel_len, NULL, DontWait) < 0)
		print_error(strerror(errno));
	else
		print_status(cprompt, "%zu files are deleted", sel_len);

	free_files();
	cont_vmode = -1;
}

static void
paste(const Arg *arg)
{
	if (sel_files == NULL) {
		print_error("nothing to paste");
		return;
	}

	if (spawn(cp_cmd, cp_cmd_len, sel_files, sel_len, cpane->dirn,
		    DontWait) < 0)
		print_error(strerror(errno));
	else
		print_status(cprompt, "%zu files are copied", sel_len);

	free_files();
}

static void
selmv(const Arg *arg)
{
	if (sel_files == NULL) {
		print_error("nothing to move");
		return;
	}

	if (spawn(mv_cmd, mv_cmd_len, sel_files, sel_len, cpane->dirn,
		    DontWait) < 0)
		print_error(strerror(errno));
	else
		print_status(cprompt, "%zu files are moved", sel_len);

	free_files();
}

static void
rname(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	char new_name[MAX_P];
	char *input_name;

	input_name = ecalloc(MAX_N, sizeof(char));

	if (get_usrinput(input_name, MAX_N, "rename: %s",
		    basename(CURSOR(cpane).name)) < 0) {
		exit_change(0);
		free(input_name);
		return;
	}

	if (snprintf(new_name, MAX_P, "%s/%s", cpane->dirn, input_name) < 0) {
		free(input_name);
		print_error(strerror(errno));
		return;
	}

	char *rename_cmd[] = { "mv", CURSOR(cpane).name, new_name };
	PERROR(spawn(rename_cmd, 3, NULL, 0, NULL, DontWait) < 0);

	free(input_name);
	exit_change(0);
}

static void
chngo(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	char *input_og;
	char *tmp[1];

	input_og = ecalloc(MAX_N, sizeof(char));

	if (get_usrinput(input_og, MAX_N, "OWNER:GROUP %s",
		    basename(CURSOR(cpane).name)) < 0) {
		exit_change(0);
		free(input_og);
		return;
	}

	tmp[0] = input_og;
	if (spawn(chown_cmd, chown_cmd_len, tmp, 1, CURSOR(cpane).name,
		    DontWait) < 0) {
		print_error(strerror(errno));
		return;
	}

	free(input_og);
	exit_change(0);
}

static void
chngm(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	char *input_og;
	char *tmp[1];

	input_og = ecalloc(MAX_N, sizeof(char));

	if (get_usrinput(input_og, MAX_N, "chmod %s",
		    basename(CURSOR(cpane).name)) < 0) {
		exit_change(0);
		free(input_og);
		return;
	}

	tmp[0] = input_og;
	if (spawn(chmod_cmd, chmod_cmd_len, tmp, 1, CURSOR(cpane).name,
		    DontWait) < 0) {
		print_error(strerror(errno));
		return;
	}

	free(input_og);
	exit_change(0);
}

static void
chngf(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	char *input_og;
	char *tmp[1];

	input_og = ecalloc(MAX_N, sizeof(char));

	if (get_usrinput(input_og, MAX_N, CHFLAG " %s",
		    basename(CURSOR(cpane).name)) < 0) {
		exit_change(0);
		free(input_og);
		return;
	}

	tmp[0] = input_og;
	if (spawn(chflags_cmd, chflags_cmd_len, tmp, 1, CURSOR(cpane).name,
		    DontWait) < 0) {
		print_error(strerror(errno));
		return;
	}

	free(input_og);
	exit_change(0);
}

static void
dupl(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	char new_name[MAX_P];
	char *input_name;

	input_name = ecalloc(MAX_N, sizeof(char));

	if (get_usrinput(input_name, MAX_N, "new name: %s",
		    basename(CURSOR(cpane).name)) < 0) {
		free(input_name);
		return;
	}

	if (snprintf(new_name, MAX_P, "%s/%s", cpane->dirn, input_name) < 0) {
		free(input_name);
		print_error(strerror(errno));
		return;
	}

	char *tmp[1];
	tmp[0] = CURSOR(cpane).name;
	if (spawn(cp_cmd, cp_cmd_len, tmp, 1, new_name, DontWait) < 0) {
		print_error(strerror(errno));
		return;
	}

	free(input_name);
}

static void
yank(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;

	free_files();
	sel_len = 1;
	sel_files = ecalloc(sel_len, sizeof(char *));
	sel_files[0] = ecalloc(MAX_P, sizeof(char));
	strncpy(sel_files[0], CURSOR(cpane).name, MAX_P);
	print_status(cprompt, "1 file is yanked", sel_len);
}

static void
switch_pane(const Arg *arg)
{
	if (cpane->dirc > 0)
		rm_hi(cpane, cpane->hdir - 1);
	cpane = &panes[pane_idx ^= 1];
	if (cpane->dirc > 0) {
		add_hi(cpane, cpane->hdir - 1);
		print_info(cpane, NULL);
	} else {
		clear_status();
	}
}

static void
quit(const Arg *arg)
{
	if (cont_vmode == -1) { /* check if selection was allocated */
		free(sel_indexes);
		if (sel_files != NULL)
			free_files();
	}
	free(panes[Left].direntr);
	free(panes[Right].direntr);
	fsev_shdn();
	tb_shutdown();
	exit(EXIT_SUCCESS);
}

static void
grabkeys(struct tb_event *event, Key *key, size_t max_keys)
{
	size_t i;

	for (i = 0; i < max_keys; i++) {
		if (event->ch != 0) {
			if (event->ch == key[i].evkey.ch) {
				key[i].func(&key[i].arg);
				return;
			}
		} else if (event->key != 0) {
			if (event->key == key[i].evkey.key) {
				key[i].func(&key[i].arg);
				return;
			}
		}
	}
}

void *
read_th(void *arg)
{
	struct timespec tim;
	tim.tv_sec = 0;
	tim.tv_nsec = 5000000L; /* 0.005 sec */

	while (1)
		if (read_events() > READEVSZ) {
			kill(main_pid, SIGUSR1);
			nanosleep(&tim, NULL);
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
refresh_pane(Pane *pane)
{
	size_t y, dyn_max, start_from;
	hwidth = (twidth / 2) - 4;
	Cpair col;

	y = 1;
	start_from = pane->firstrow;
	dyn_max = MIN(pane->dirc, (scrheight - 1) + pane->firstrow);

	/* print each entry in directory */
	while (start_from < dyn_max) {
		get_hicol(&col, pane->direntr[start_from].mode);
		print_row(pane, start_from, col);
		start_from++;
		y++;
	}

	if (pane->dirc > 0)
		print_info(pane, NULL);
	else
		clear_status();

	/* print current directory title */
	pane->dircol.fg |= TB_BOLD;
	printf_tb(pane->x_srt, 0, pane->dircol, " %.*s", hwidth, pane->dirn);
}

static void
set_direntr(Pane *pane, struct dirent *entry, DIR *dir, char *filter)
{
	int i;
	char *tmpfull;
	struct stat status;

#define ADD_ENTRY                                          \
	tmpfull = get_fullpath(pane->dirn, entry->d_name); \
	strncpy(pane->direntr[i].name, tmpfull, MAX_N);    \
	if (lstat(tmpfull, &status) == 0) {                \
		pane->direntr[i].size = status.st_size;    \
		pane->direntr[i].mode = status.st_mode;    \
		pane->direntr[i].group = status.st_gid;    \
		pane->direntr[i].user = status.st_uid;     \
		pane->direntr[i].dt = status.st_mtime;     \
	}                                                  \
	i++;                                               \
	free(tmpfull);

	i = 0;
	pane->direntr =
		erealloc(pane->direntr, (10 + pane->dirc) * sizeof(Entry));
	while ((entry = readdir(dir)) != 0) {
		if (show_dotfiles == 1) {
			if (entry->d_name[0] == '.' &&
				(entry->d_name[1] == '\0' ||
					entry->d_name[1] == '.'))
				continue;
		} else {
			if (entry->d_name[0] == '.')
				continue;
		}

		if (filter == NULL) {
			ADD_ENTRY
		} else if (filter != NULL) {
			if (strcasestr(entry->d_name, filter) != NULL) {
				ADD_ENTRY
			}
		}
	}

	pane->dirc = i;
}

static int
listdir(Pane *pane)
{
	DIR *dir;
	struct dirent *entry;
	int filtercount = 0;
	size_t oldc = pane->dirc;

	pane->dirc = 0;

	dir = opendir(pane->dirn);
	if (dir == NULL)
		return -1;

	/* get content and filter sum */
	while ((entry = readdir(dir)) != 0) {
		if (pane->filter != NULL) {
			if (strcasestr(entry->d_name, pane->filter) != NULL)
				filtercount++;
		} else { /* no filter */
			pane->dirc++;
		}
	}

	if (pane->filter == NULL) {
		clear_pane(pane);
		pane->dirc -= 2;
	}

	if (pane->filter != NULL) {
		if (filtercount > 0) {
			pane->dirc = filtercount;
			clear_pane(pane);
			pane->hdir = 1;
		} else if (filtercount == 0) {
			if (closedir(dir) < 0)
				return -1;
			pane->dirc = oldc;
			return -1;
		}
	}

	/* print current directory title */
	pane->dircol.fg |= TB_BOLD;
	printf_tb(pane->x_srt, 0, pane->dircol, " %.*s", hwidth, pane->dirn);

	if (pane->filter == NULL) /* dont't watch when filtering */
		if (addwatch(pane) < 0)
			print_error("can't add watch");

	/* empty directory */
	if (pane->dirc == 0) {
		clear_status();
		if (closedir(dir) < 0)
			return -1;
		return 0;
	}

	rewinddir(dir); /* reset position */
	set_direntr(
		pane, entry, dir, pane->filter); /* create array of entries */
	qsort(pane->direntr, pane->dirc, sizeof(Entry), sort_name);
	refresh_pane(pane);

	if (pane->hdir > pane->dirc)
		pane->hdir = pane->dirc;

	if (pane == cpane && pane->dirc > 0)
		add_hi(pane, pane->hdir - 1);

	if (closedir(dir) < 0)
		return -1;
	return 0;
}

static void
t_resize(void)
{
	tb_clear();
	draw_frame();
	panes[Left].x_end = (twidth / 2) - 1;
	panes[Right].x_end = twidth - 1;
	panes[Right].x_srt = (twidth / 2) + 2;
	refresh_pane(&panes[Left]);
	refresh_pane(&panes[Right]);
	if (cpane->dirc > 0)
		add_hi(cpane, cpane->hdir - 1);
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
get_shell(void)
{
	shell[0] = getenv("SHELL");
	shell[1] = NULL;

	if (shell[0] == NULL)
		shell[0] = sh;
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

	pane_idx = Left; /* cursor pane */
	cpane = &panes[pane_idx];

	panes[Left].pane_id = 0;
	panes[Left].x_srt = 2;
	panes[Left].x_end = (twidth / 2) - 1;
	panes[Left].dircol = cpanell;
	panes[Left].firstrow = 0;
	panes[Left].direntr = ecalloc(0, sizeof(Entry));
	strncpy(panes[Left].dirn, cwd, MAX_P);
	panes[Left].hdir = 1;
	panes[Left].inotify_wd = -1;
	panes[Left].parent_row = 1;

	panes[Right].pane_id = 1;
	panes[Right].x_srt = (twidth / 2) + 2;
	panes[Right].x_end = twidth - 1;
	panes[Right].dircol = cpanelr;
	panes[Right].firstrow = 0;
	panes[Right].direntr = ecalloc(0, sizeof(Entry));
	strncpy(panes[Right].dirn, home, MAX_P);
	panes[Right].hdir = 1;
	panes[Right].inotify_wd = -1;
	panes[Right].parent_row = 1;
}

static void
draw_frame(void)
{
	int i;
	theight = tb_height();
	twidth = tb_width();
	hwidth = (twidth / 2) - 4;
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

void
th_handler(int num)
{
	if (fork_pid > 0) /* while forking don't listdir() */
		return;
	(void)num;
	PERROR(listdir(&panes[Left]));
	PERROR(listdir(&panes[Right]));
	tb_present();
}

static int
start_signal(void)
{
	struct sigaction sa;

	main_pid = getpid();
	sa.sa_handler = th_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	return sigaction(SIGUSR1, &sa, NULL);
}

static void
refresh(const Arg *arg)
{
	kill(main_pid, SIGWINCH);
}

static void
start(void)
{
	switch (tb_init()) {
	case TB_EFAILED_TO_OPEN_TTY:
		die("TB_EFAILED_TO_OPEN_TTY");
		break;
	case TB_EUNSUPPORTED_TERMINAL:
		die("TB_EUNSUPPORTED_TERMINAL");
		break;
	case TB_EPIPE_TRAP_ERROR:
		die("TB_EUNSUPPORTED_TERMINAL");
		break;
	case 0:
		break;
	default:
		die("UNKNOWN FAILURE");
	}

	if (tb_select_output_mode(TB_OUTPUT_256) != TB_OUTPUT_256)
		if (tb_select_output_mode(TB_OUTPUT_NORMAL) != TB_OUTPUT_NORMAL)
			die("output error");
	draw_frame();
	set_panes();
	get_editor();
	get_shell();
	PERROR(start_signal() < 0);
	PERROR(fsev_init() < 0);
	PERROR(listdir(&panes[Left]) < 0);
	PERROR(listdir(&panes[Right]) < 0);
	tb_present();

	pthread_create(&fsev_thread, NULL, read_th, NULL);
	start_ev();
}

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
