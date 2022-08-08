/* See LICENSE file for copyright and license details. */

#if defined(__linux__)
#define _GNU_SOURCE
#elif defined(__APPLE__)
#define _DARWIN_C_SOURCE
#elif defined(__FreeBSD__)
#define __BSD_VISIBLE 1
#endif
#include <sys/types.h>
#include <sys/ioctl.h>
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
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* macros */
#define NORM		 0X0
#define BOLD		 0X1
#define DIM		 0X2
#define ITALIC		 0X3
#define UNDERL		 0X4
#define BLINK		 0X5
#define RVS		 0X7
#define HIDDEN		 0X8
#define STRIKE		 0X9
#define XK_CTRL(k)	 ((k)&0x1f)
#define XK_ALT(k)	 (k)1b
#define XK_UP		 0x415b1b
#define XK_DOWN		 0x425b1b
#define XK_RIGHT	 0x435b1b
#define XK_LEFT		 0x445b1b
#define XK_HOME		 0x485b1b
#define XK_END		 0x7e345b1b
#define XK_PGUP		 0x7e355b1b
#define XK_PGDOWN	 0x7e365b1b
#define XK_BACKSPACE	 0x7f
#define XK_TAB		 0x09
#define XK_ENTER	 0x0D
#define XK_ESC		 0x1B
#define XK_SPACE	 0x20
#define MAX_P		 4096
#define MAX_N		 255
#define MAX_USRI	 32
#define MAX_EXT		 4
#define MAX_STATUS	 255
#define MAX_LINE	 4096
#define MAX_USRN	 32
#define MAX_GRPN	 32
#define MAX_DTF		 32
#define CURSOR(x)	 (x)->direntry[(x)->hdir - 1]
#define TERM_ROWS	 term->rows - 2
#define MAX(A, B)	 ((A) > (B) ? (A) : (B))
#define MIN(A, B)	 ((A) < (B) ? (A) : (B))
#define LEN(A)		 (sizeof(A) / sizeof(A[0]))
#define BETWEEN(X, A, B) ((A) <= (X) && (X) <= (B))

/* typedef */
typedef struct {
	int rows;
	int cols;
	struct termios term;
} Term;

typedef struct {
	uint16_t fg;
	uint16_t bg;
	uint8_t attr;
} Cpair;

typedef struct {
	char *b;
	int len;
} Tbuf;

typedef struct {
	char name[MAX_N];
	gid_t group;
	mode_t mode;
	off_t size;
	time_t dt;
	uid_t user;
} Entry;

typedef struct {
	char dirname[MAX_P];
	char *filter;
	Entry *direntry;
	int dirc;
	int hdir;
	int x_srt;
	int x_end;
	int width;
	int firstrow;
	// int parent_firstrow;
	// int parent_row; // FIX
	Cpair dircol;
	int pane_id;
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
	int i;
	const void *v;
} Arg;

typedef struct {
	const uint32_t k;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

/* function declarations */
static void add_hi(Pane *, size_t);
static int addwatch(Pane *);
static void backup_term(void);
static void bkmrk(const Arg *);
static void calcdir(const Arg *);
static int check_dir(char *);
static void chngf(const Arg *);
static void chngm(const Arg *);
static void chngo(const Arg *);
static void clear_pane(Pane *);
static void clear_status(void);
static void create_dir_entries(Pane *, DIR *, char *);
static void crnd(const Arg *);
static void crnf(const Arg *);
static void delent(const Arg *);
static void die(const char *, ...);
static void draw_frame(Cpair);
static void dupl(const Arg *);
static void *ecalloc(size_t, size_t);
static void *erealloc(void *, size_t);
static void exit_change(const Arg *);
static void exit_vmode(const Arg *);
static void free_files(void);
static int frules(char *);
static int fsev_init(void);
static void fsev_shdn(void);
static void get_dirp(char *);
static void get_dirsize(char *, off_t *);
static void get_editor(void);
static char *get_ext(char *);
static int get_fdt(char *, time_t);
static char *get_fgrp(gid_t);
static char *get_fperm(mode_t);
static char *get_fsize(off_t);
static char *get_fullpath(char *, char *);
static char *get_fusr(uid_t);
static void get_hicol(Cpair *, mode_t);
static uint32_t getkey(void);
static void get_shell(void);
static int get_term_size(int *, int *);
static int get_usrinput(char *, size_t, const char *, ...);
static void grabkeys(uint32_t, Key *, size_t);
static void init_files(void);
static Term *init_term(void);
static int listdir(Pane *);
static void move_to_col(int);
static void move_to(int, int);
static void mvbk(const Arg *);
static void mvbtm(const Arg *);
static void mvfwd(const Arg *);
static void mvtop(const Arg *);
static void mv_ver(const Arg *);
static int opnf(char *);
static void opnsh(const Arg *);
static void paste(const Arg *);
static void print_dir_entries(Pane *);
static void print_dirname(Pane *);
static void print_entry(Pane *, size_t, Cpair);
static void print_error(char *);
static void print_info(Pane *, char *);
static void print_status(Cpair, const char *, ...);
static void quit(const Arg *);
static void quit_term(void);
static int read_events(void);
static void *read_th(void *);
static void refresh(const Arg *);
static void rm_hi(Pane *, size_t);
static void rmwatch(Pane *);
static void rname(const Arg *);
static void selall(const Arg *);
static void selcalc(void);
static void seldel(const Arg *);
static void seldwn(const Arg *);
static void selmv(const Arg *);
static void selref(void);
static void selup(const Arg *);
static void selynk(const Arg *);
static void set_panes(void);
static void set_term(void);
static void sighandler(int);
static int sort_name(const void *const, const void *const);
static int spawn(const void *, size_t, const void *, size_t, char *, int);
static void start_change(const Arg *);
static void start_ev(void);
static void start_filter(const Arg *);
static int start_signal(void);
static void start_vmode(const Arg *);
static void start(void);
static void switch_pane(const Arg *);
static void termb_append(const char *, int);
static void termb_free(void);
static void termb_write(void);
static void toggle_df(const Arg *);
static void t_resize(void);
static void yank(const Arg *);

/* global variables */
static Pane *cpane;
static Pane panes[2];
static Tbuf ab;
static Term *term;
static Term nterm;
static Term oterm;
static char **sel_files;
static char *editor[2];
static char *shell[2];
static char fed[] = "vi"; // TODO rename var
static char sh[] = "/bin/sh";
static int *sel_indexes;
static int cont_change = 0;
static int cont_vmode = 0;
static int pane_idx;
static pid_t fork_pid = 0, main_pid;
static pthread_t fsev_thread;
static size_t sel_len = 0;
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
enum { Left, Right };	 /* panes */
enum { Wait, DontWait }; /* spawn forks */

/* configuration, allows nested code to access above variables */
#include "config.h"

static void
add_hi(Pane *pane, size_t entpos)
{
	Cpair col;
	get_hicol(&col, pane->direntry[entpos].mode);
	col.attr = RVS;
	move_to((pane->hdir) + 1 - pane->firstrow, pane->x_srt);
	print_entry(pane, entpos, col);
	termb_write();
}

static int
addwatch(Pane *pane)
{
#if defined(_SYS_INOTIFY_H)
	return pane->inotify_wd = inotify_add_watch(inotify_fd, pane->dirname,
		   IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_CREATE |
		       IN_ATTRIB | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF);
#elif defined(_SYS_EVENT_H_)
	pane->event_fd = open(pane->dirname, O_RDONLY);
	if (pane->event_fd < 0)
		return pane->event_fd;
	EV_SET(&evlist[pane->pane_id], pane->event_fd, EVFILT_VNODE,
	    EV_ADD | EV_CLEAR,
	    NOTE_DELETE | NOTE_EXTEND | NOTE_LINK | NOTE_RENAME | NOTE_ATTRIB |
		NOTE_REVOKE | NOTE_WRITE,
	    0, NULL);
	return 0;
#endif
}

static void
backup_term(void)
{
	if (tcgetattr(STDIN_FILENO, &oterm.term) < 0)
		die("tcgetattr:");
}

static void
bkmrk(const Arg *arg)
{
	if (check_dir((char *)arg->v) != 0) {
		print_error(strerror(errno));
		return;
	}

	strncpy(cpane->dirname, (char *)arg->v, MAX_P);
	cpane->firstrow = 0;
	// cpane->parent_row = 1;
	cpane->hdir = 1;
	if (listdir(cpane) < 0)
		print_error(strerror(errno));
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
chngf(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	char *input_og;
	char *tmp[1];

	input_og = ecalloc(MAX_N, sizeof(char));

	if (get_usrinput(input_og, MAX_N,
		CHFLAG " %s: ", basename(CURSOR(cpane).name)) < 0) {
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
chngm(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	char *input_og;
	char *tmp[1];

	input_og = ecalloc(MAX_N, sizeof(char));

	if (get_usrinput(input_og, MAX_N,
		"chmod %s: ", basename(CURSOR(cpane).name)) < 0) {
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
chngo(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	char *input_og;
	char *tmp[1];

	input_og = ecalloc(MAX_N, sizeof(char));

	if (get_usrinput(input_og, MAX_N,
		"OWNER:GROUP %s: ", basename(CURSOR(cpane).name)) < 0) {
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
clear_pane(Pane *pane)
{
	int i;

	char buf[64];
	size_t buflen;

	// print outer frame
	snprintf(buf, term->cols, "\x1b[%d;48;5;%d;38;5;%dm%s\x1b[0;0m",
	    cframe.attr, cframe.bg, cframe.fg, "  ");
	buflen = strnlen(buf, 64);

	if (pane->x_srt == 3) { // left pane
		for (i = 0; i < term->rows - 1; i++) {
			move_to(i, pane->x_end);
			// erase start of line to the cursor
			termb_append("\x1b[1K", 4);
			move_to(i, 0);
			termb_append(buf, buflen);
		}
	} else { // right pane
		for (i = 0; i < term->rows - 1; i++) {
			move_to(i, pane->x_srt);
			// erase from cursor to end of line
			termb_append("\x1b[0K", 4);
			move_to(i, term->cols - 1);
			termb_append(buf, buflen);
		}
	}

	termb_write();
}

void
clear_status(void)
{
	write(STDOUT_FILENO,
	    "\x1b[999;1f" // moves cursor to line 999, column 1
	    "\x1b[2K",	  // erase the entire line
	    12);
}

static void
create_dir_entries(Pane *pane, DIR *dir, char *filter)
{
	int i;
	char *tmpfull;
	struct stat status;
	struct dirent *entry;

	i = 0;
	pane->dirc = 0;
	while ((entry = readdir(dir)) != NULL) {
		if (show_dotfiles == 1) {
			if (entry->d_name[0] == '.' &&
			    (entry->d_name[1] == '\0' ||
				entry->d_name[1] == '.'))
				continue;
		} else {
			if (entry->d_name[0] == '.')
				continue;
		}

		pane->dirc++;
		pane->direntry = erealloc(pane->direntry,
		    (pane->dirc) * sizeof(Entry));

		tmpfull = get_fullpath(pane->dirname, entry->d_name);
		strncpy(pane->direntry[i].name, tmpfull, MAX_N);
		if (lstat(tmpfull, &status) == 0) {
			pane->direntry[i].size = status.st_size;
			pane->direntry[i].mode = status.st_mode;
			pane->direntry[i].group = status.st_gid;
			pane->direntry[i].user = status.st_uid;
			pane->direntry[i].dt = status.st_mtime;
		}
		i++;
		free(tmpfull);
	}

	// pane->dirc = i;
}

static void
crnd(const Arg *arg)
{
	char *user_input, *path;

	user_input = ecalloc(MAX_USRI, sizeof(char));
	if (get_usrinput(user_input, MAX_USRI, "new dir: ") < 0) {
		free(user_input);
		return;
	}

	path = ecalloc(MAX_P, sizeof(char));
	if (snprintf(path, MAX_P, "%s/%s", cpane->dirname, user_input) < 0) {
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
crnf(const Arg *arg)
{
	char *user_input, *path;
	int rf;

	user_input = ecalloc(MAX_USRI, sizeof(char));
	if (get_usrinput(user_input, MAX_USRI, "new file: ") < 0) {
		free(user_input);
		return;
	}

	path = ecalloc(MAX_P, sizeof(char));
	if (snprintf(path, MAX_P, "%s/%s", cpane->dirname, user_input) < 0) {
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
delent(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	char *inp_conf;

	inp_conf = ecalloc(delconf_len, sizeof(char));
	if ((get_usrinput(inp_conf, delconf_len, "delete files(s) (%s) ? ",
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

void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] != '\0' && fmt[strlen(fmt) - 1] == ':') {
		(void)fputc(' ', stderr);
		perror(NULL);
	} else {
		(void)fputc('\n', stderr);
	}

	exit(EXIT_FAILURE);
}

void
draw_frame(Cpair cframe)
{
	int y;
	char buf[64];
	size_t buflen;

	snprintf(buf, nterm.cols, "\x1b[%d;48;5;%d;38;5;%dm%s\x1b[0;0m",
	    cframe.attr, cframe.bg, cframe.fg, "  ");
	buflen = strnlen(buf, 64);
	write(STDOUT_FILENO, "\x1b[H", 3);

	// for (y = 0; y < nterm.cols; y++) {
	//	termb_append(buf, buflen);
	// }

	// middle line
	for (y = 0; y < nterm.rows - 2; y++) {
		// termb_append(buf, buflen);
		move_to_col(nterm.cols / 2);
		termb_append(buf, buflen);
		// move_to_col(nterm.cols - 1);
		// termb_append(buf, buflen);
		termb_append("\r\n", 2);
	}

	// bottom line
	move_to(nterm.rows - 1, 1);
	for (y = 0; y < nterm.cols; y++) {
		termb_append(buf, buflen);
	}

	termb_write();
}

static void
dupl(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	char new_name[MAX_P];
	char *input_name;

	input_name = ecalloc(MAX_N, sizeof(char));

	if (get_usrinput(input_name, MAX_N,
		"new name %s: ", basename(CURSOR(cpane).name)) < 0) {
		free(input_name);
		return;
	}

	if (snprintf(new_name, MAX_P, "%s/%s", cpane->dirname, input_name) <
	    0) {
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

void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;
	if ((p = calloc(nmemb, size)) == NULL)
		die("calloc:");
	return p;
}

void *
erealloc(void *p, size_t len)
{
	if ((p = realloc(p, len)) == NULL)
		die("realloc: %s\n", strerror(errno));
	return p;
}

static void
exit_change(const Arg *arg)
{
	cont_change = -1;
	print_info(cpane, NULL);
}

static void
exit_vmode(const Arg *arg)
{
	print_dir_entries(cpane);
	add_hi(cpane, cpane->hdir - 1);
	cont_vmode = -1;
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
get_editor(void)
{
	editor[0] = getenv("EDITOR");
	editor[1] = NULL;

	if (editor[0] == NULL)
		editor[0] = fed;
}

static char *
get_ext(char *str)
{
	char *ext;
	size_t counter, len, i;

	counter = 0;
	len = strnlen(str, MAX_N);

	for (i = len - 1; i > 0; i--) {
		if (str[i] == '.') {
			break;
		} else {
			counter++;
		}
	}

	ext = ecalloc(MAX_EXT + 1, sizeof(char));
	strncpy(ext, &str[len - counter], MAX_EXT);
	ext[MAX_EXT] = '\0';
	for (char *p = ext; *p; p++) {
		*p = tolower(*(const unsigned char *)p);
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

uint32_t
getkey(void)
{
	uint32_t r = 0;

	if ((read(STDIN_FILENO, &r, sizeof(uint32_t))) < 0)
		die("read:");
	return r;
}

static void
get_shell(void)
{
	shell[0] = getenv("SHELL");
	shell[1] = NULL;

	if (shell[0] == NULL)
		shell[0] = sh;
}

int
get_term_size(int *rows, int *cols)
{
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
		return -1;
	*cols = ws.ws_col;
	*rows = ws.ws_row;
	return 0;
}

static int
get_usrinput(char *result, size_t max_chars, const char *fmt, ...)
{
	uint32_t c = 0;
	size_t i = 0;
	char erase[] = "\b \b";
	char msg[MAX_N];
	va_list vl;

	// print msg
	va_start(vl, fmt);
	vsnprintf(msg, MAX_N, fmt, vl);
	print_status(cprompt, msg);
	va_end(vl);

	while (1) {
		c = getkey();
		switch (c) {
		case XK_ESC:
			print_info(cpane, NULL);
			return -1;
		case XK_ENTER:
			if (i > 0) {
				result[i] = '\0';
				return 0;
			}
			print_info(cpane, NULL);
			return -1;
		case XK_BACKSPACE:
			if (i > 0) {
				write(STDOUT_FILENO, &erase, 3);
				i--;
			}
			break;
		default:
			if (i < max_chars) {
				write(STDOUT_FILENO, &c, 1);
				result[i] = (char)c;
				i++;
			}
		}
	}

	return 0;
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
	print_status(cwarn, "No key binding found for key 0x%x", k);
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
		strncpy(sel_files[i], cpane->direntry[sel_indexes[i] - 1].name,
		    MAX_P);
	}
}

Term *
init_term()
{
	backup_term();
	set_term();
	return &nterm;
}

static int
listdir(Pane *pane)
{
	DIR *dir;

	dir = opendir(pane->dirname);
	if (dir == NULL)
		return -1;

	create_dir_entries(pane, dir, NULL); /* create array of entries */
	qsort(pane->direntry, pane->dirc, sizeof(Entry), sort_name);
	print_dir_entries(pane);

	if (addwatch(pane) < 0)
		print_error("can't add watch");

	if (pane == cpane && pane->dirc > 0)
		add_hi(pane, pane->hdir - 1);

	if (closedir(dir) < 0)
		return -1;
	return 0;
}

void
move_to_col(int y)
{
	char buf[16];
	size_t buflen;
	snprintf(buf, 16, "\x1b[%dG", y);
	buflen = strlen(buf);
	termb_append(buf, buflen);
}

void
move_to(int x, int y)
{
	char buf[16];
	size_t buflen;
	snprintf(buf, 16, "\x1b[%d;%df", x, y);
	buflen = strlen(buf);
	termb_append(buf, buflen);
}

static void
mvbk(const Arg *arg)
{
	if (cpane->dirname[0] == '/' &&
	    cpane->dirname[1] == '\0') { /* cwd = / */
		return;
	}
	rmwatch(cpane);

	get_dirp(cpane->dirname);
	if (check_dir(cpane->dirname) < 0) {
		print_error(strerror(errno));
		return;
	}

	// cpane->firstrow = cpane->parent_firstrow;
	// cpane->hdir = cpane->parent_row;
	cpane->hdir = 1;
	if (listdir(cpane) < 0)
		print_error(strerror(errno));
	// cpane->parent_firstrow = 0;
	// cpane->parent_row = 1;
}

static void
mvbtm(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	if (cpane->dirc > (TERM_ROWS)) { // need to scroll
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir = cpane->dirc;
		cpane->firstrow = cpane->dirc - (TERM_ROWS) + 1;
		print_dir_entries(cpane);
		add_hi(cpane, cpane->hdir - 1);
	} else { // no need to scroll
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

	rmwatch(cpane);
	switch (check_dir(CURSOR(cpane).name)) {
	case 0:
		strncpy(cpane->dirname, CURSOR(cpane).name, MAX_P);
		// cpane->parent_row = cpane->hdir;
		// cpane->parent_firstrow = cpane->firstrow;
		cpane->hdir = 1;
		cpane->firstrow = 0;
		if (listdir(cpane) < 0)
			print_error(strerror(errno));
		break;
	case 1: /* not a directory open file */
		quit_term();
		s = opnf(CURSOR(cpane).name);
		term = init_term();
		t_resize();
		if (s < 0)
			print_error("process failed non-zero exit");
		break;
	case -1: /* failed to open directory */
		print_error(strerror(errno));
		break;
	}
}

static void
mvtop(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	if (cpane->dirc > TERM_ROWS) {
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir = 1;
		cpane->firstrow = 0;
		print_dir_entries(cpane);
		add_hi(cpane, cpane->hdir - 1);
	} else {
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir = 1;
		add_hi(cpane, cpane->hdir - 1);
		print_info(cpane, NULL);
	}
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
		print_dir_entries(cpane);
		add_hi(cpane, cpane->hdir - 1);
		return;
	}

	if (cpane->hdir - cpane->firstrow >= TERM_ROWS + arg->i &&
	    arg->i < 0) { /* scroll down */
		cpane->firstrow = cpane->firstrow - arg->i;
		rm_hi(cpane, cpane->hdir - 1);
		cpane->hdir = cpane->hdir - arg->i;
		print_dir_entries(cpane);
		add_hi(cpane, cpane->hdir - 1);
		return;
	}

	rm_hi(cpane, cpane->hdir - 1);
	cpane->hdir = cpane->hdir - arg->i;
	add_hi(cpane, cpane->hdir - 1);
	print_info(cpane, NULL);
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
		return spawn((char **)rules[c].v, rules[c].vlen, NULL, 0, fn,
		    Wait);
}

static void
opnsh(const Arg *arg)
{
	int s;

	quit_term();
	chdir(cpane->dirname);
	s = spawn(shell, 1, NULL, 0, NULL, Wait);
	term = init_term();
	t_resize();
	if (s < 0)
		print_error("process failed non-zero exit");
}

static void
paste(const Arg *arg)
{
	if (sel_files == NULL) {
		print_error("nothing to paste");
		return;
	}

	if (spawn(cp_cmd, cp_cmd_len, sel_files, sel_len, cpane->dirname,
		DontWait) < 0)
		print_error(strerror(errno));
	else
		print_status(cprompt, "%zu files are copied", sel_len);

	free_files();
}

static void
print_dir_entries(Pane *pane)
{
	size_t y, dyn_max, start_from;
	Cpair col;

	clear_pane(pane);
	y = 1;
	start_from = pane->firstrow;
	// dyn_max = MIN(pane->dirc, (TERM_ROWS) + pane->firstrow);
	dyn_max = MIN(pane->dirc, (TERM_ROWS - 1) + pane->firstrow);

	/* print each entry in directory */
	while (start_from < dyn_max) {
		get_hicol(&col, pane->direntry[start_from].mode);
		move_to(y + 1, pane->x_srt);
		print_entry(pane, start_from, col);
		start_from++;
		y++;
	}

	if (pane->dirc > 0)
		print_info(pane, NULL);
	else
		clear_status();

	print_dirname(pane);
	termb_write();
}

static void
print_dirname(Pane *pane)
{
	char buf[MAX_P];
	size_t buflen;

	if (pane->x_srt == 3) { // left pane
		snprintf(buf, MAX_P,
		    "\x1b[1;%df" // moves cursor to first line, x_end
		    "\x1b[1K"	 // erase start of line to the cursor
		    "\x1b[1;%df" // moves cursor to first line, x_srt
		    "\x1b[%d;48;5;%d;38;5;%dm" // set string colors
		    "\b\b  ",		       // top left corner
		    pane->x_end, pane->x_srt, cpanell.attr, cpanell.bg,
		    cpanell.fg);
		buflen = strnlen(buf, MAX_N);
		termb_append(buf, buflen);

	} else { // right pane
		snprintf(buf, MAX_P,
		    "\x1b[1;%df" // moves cursor to first line, pane->x_srt
		    "\x1b[0K"	 // erase from cursor to end of line
		    "\x1b[%d;48;5;%d;38;5;%dm", // set string colors
		    pane->x_srt, cpanelr.attr, cpanelr.bg, cpanelr.fg);
		buflen = strnlen(buf, MAX_N);
		termb_append(buf, buflen);
	}

	snprintf(buf, pane->width + 1, "%*s\x1b[0;0m", -(pane->width),
	    pane->dirname);
	buflen = strnlen(buf, MAX_N);
	termb_append(buf, buflen);
}

static void
print_entry(Pane *pane, size_t entpos, Cpair col)
{
	char *result, *rez_pth;
	char lnk_full[MAX_N];
	char buf[MAX_P];
	size_t buflen = 0;

	result = basename(pane->direntry[entpos].name);

	if (S_ISLNK(pane->direntry[entpos].mode) != 0) {
		rez_pth = ecalloc(MAX_P, sizeof(char));
		if (realpath(pane->direntry[entpos].name, rez_pth) == NULL)
			col = cbrlnk;
		snprintf(lnk_full, MAX_N, "%s -> %s", result, rez_pth);
		result = lnk_full;
		free(rez_pth);
	}

	/* set colors */
	snprintf(buf, MAX_P, "\x1b[%d;48;5;%d;38;5;%dm", col.attr, col.bg,
	    col.fg);
	buflen = strnlen(buf, MAX_N);
	termb_append(buf, buflen);

	/* set name */
	snprintf(buf, pane->width + 1, "%*s", -(pane->width), result);
	buflen = strnlen(buf, MAX_N);
	termb_append(buf, buflen);

	/* reset color */
	termb_append("\x1b[0;0m", 6);
}

static void
print_error(char *msg)
{
	print_status(cerr, msg);
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

void
print_status(Cpair col, const char *fmt, ...)
{
	char buf[nterm.cols];
	va_list vl;
	va_start(vl, fmt);
	(void)vsnprintf(buf, nterm.cols, fmt, vl);
	va_end(vl);

	char result[1024];
	size_t resultlen;

	snprintf(result, 1024,
	    "\x1b[%d;1f"	       // moves cursor to last line, column 1
	    "\x1b[2K"		       // erase the entire line
	    "\x1b[%d;48;5;%d;38;5;%dm" // set string colors
	    "%s"
	    "\x1b[0;0m", // reset colors
	    nterm.rows, col.attr, col.bg, col.fg, buf);
	resultlen = strlen(result);
	write(STDOUT_FILENO, result, resultlen);
}

static void
quit(const Arg *arg)
{
	if (cont_vmode == -1) { /* check if selection was allocated */
		free(sel_indexes);
		if (sel_files != NULL)
			free_files();
	}
	fsev_shdn();
	free(panes[Left].direntry);
	free(panes[Right].direntry);
	quit_term();
	exit(EXIT_SUCCESS);
}

void
quit_term(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &oterm.term) < 0)
		die("tcsetattr:");
	write(STDOUT_FILENO,
	    "\033[u"	  // restores the cursor to the last saved position
	    "\033[?47l"	  // restore screen
	    "\033[?1049l" // disables the alternative buffer
	    "\033[?7h"	  // enable line wrapping
	    //"\033[?25h"   // unhide cursor
	    //"\033[r"      // reset scroll region
	    ,
	    23);
}

static int
read_events(void)
{
#if defined(_SYS_INOTIFY_H)
	char *p;
	ssize_t r;
	struct inotify_event *event;
	const size_t events = 32;
	const size_t evbuflen = events *
	    (sizeof(struct inotify_event) + MAX_N + 1);
	char buf[evbuflen];

	if (cpane->inotify_wd < 0)
		return -1;
	r = read(inotify_fd, buf, evbuflen);
	print_status(cerr, "START CATCH = %d", buf);
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

static void *
read_th(void *arg)
{
	struct timespec tim;
	tim.tv_sec = 0;
	tim.tv_nsec = 500000000L; /* 0.05 sec */

	while (1) {
		if (read_events() > READEVSZ) {
			kill(main_pid, SIGUSR1);
			nanosleep(&tim, NULL);
		}
	}
	return arg;
}

static void
refresh(const Arg *arg)
{
	kill(main_pid, SIGWINCH);
}

static void
rm_hi(Pane *pane, size_t entpos)
{
	Cpair col;
	get_hicol(&col, pane->direntry[entpos].mode);
	move_to((pane->hdir) + 1 - pane->firstrow, pane->x_srt);
	print_entry(pane, entpos, col);
	termb_write();
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
rname(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	char new_name[MAX_P];
	char *input_name;

	input_name = ecalloc(MAX_N, sizeof(char));

	if (get_usrinput(input_name, MAX_N,
		"rename: %s: ", basename(CURSOR(cpane).name)) < 0) {
		exit_change(0);
		free(input_name);
		return;
	}

	if (snprintf(new_name, MAX_P, "%s/%s", cpane->dirname, input_name) <
	    0) {
		free(input_name);
		print_error(strerror(errno));
		return;
	}

	char *rename_cmd[] = { "mv", CURSOR(cpane).name, new_name };
	if (spawn(rename_cmd, 3, NULL, 0, NULL, DontWait) < 0) {
		print_error(strerror(errno));
	}

	free(input_name);
	exit_change(0);
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
seldel(const Arg *arg)
{
	char *inp_conf;

	inp_conf = ecalloc(delconf_len, sizeof(char));
	if ((get_usrinput(inp_conf, delconf_len, "delete files(s) (%s) ? ",
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
seldwn(const Arg *arg)
{
	mv_ver(arg);
	int index = abs(cpane->hdir - sel_indexes[0]);
	print_status(cprompt, "-- VISUAL -- d i=%d", index);

	if (cpane->hdir > sel_indexes[0]) {
		sel_indexes[index] = cpane->hdir;
		add_hi(cpane, sel_indexes[index] - 2);
	} else {
		sel_indexes[index + 1] = 0;
	}
	if (cpane->dirc >= TERM_ROWS ||
	    cpane->hdir >= cpane->dirc) { /* rehighlight all if scrolling */
		selref();
	}
}

static void
selmv(const Arg *arg)
{
	if (sel_files == NULL) {
		print_error("nothing to move");
		return;
	}

	if (spawn(mv_cmd, mv_cmd_len, sel_files, sel_len, cpane->dirname,
		DontWait) < 0)
		print_error(strerror(errno));
	else
		print_status(cprompt, "%zu files are moved", sel_len);

	free_files();
}

static void
selref(void)
{
	int i;
	for (i = 0; i < cpane->dirc; i++) {
		if (sel_indexes[i] < (TERM_ROWS + cpane->firstrow) &&
		    sel_indexes[i] >
			cpane->firstrow) { /* checks if in the frame of the
					      directories */
			add_hi(cpane, sel_indexes[i] - 1);
		}
	}
}

static void
selup(const Arg *arg)
{
	mv_ver(arg);
	int index = abs(cpane->hdir - sel_indexes[0]);
	print_status(cprompt, "-- VISUAL -- u i=%d", index);

	if (cpane->hdir < sel_indexes[0]) {
		sel_indexes[index] = cpane->hdir;
		add_hi(cpane, sel_indexes[index]);
	} else if (index < cpane->dirc) {
		sel_indexes[index + 1] = 0;
	}
	if (cpane->dirc >= TERM_ROWS ||
	    cpane->hdir <= 1) { /* rehighlight all if scrolling */
		selref();
	}
}

static void
selynk(const Arg *arg)
{
	init_files();
	print_dir_entries(cpane);
	add_hi(cpane, cpane->hdir - 1);
	print_status(cprompt, "%zu files are yanked", sel_len);
	cont_vmode = -1;
}

static void
set_panes(void)
{
	char *home;
	char cwd[MAX_P];

	get_term_size(&term->rows, &term->cols);

	home = getenv("HOME");
	if (home == NULL)
		home = "/";
	if ((getcwd(cwd, sizeof(cwd)) == NULL))
		strncpy(cwd, home, MAX_P);

	pane_idx = Left; /* cursor pane */
	cpane = &panes[pane_idx];

	panes[Left].pane_id = 0;
	panes[Left].x_srt = 3;
	panes[Left].x_end = (term->cols / 2) - 1;
	panes[Left].width = panes[Left].x_end - panes[Left].x_srt + 1;
	panes[Left].dircol = cpanell;
	panes[Left].firstrow = 0;
	panes[Left].direntry = ecalloc(0, sizeof(Entry));
	strncpy(panes[Left].dirname, cwd, MAX_P);
	panes[Left].hdir = 1;
	panes[Left].inotify_wd = -1;
	// panes[Left].parent_row = 1;

	panes[Right].pane_id = 1;
	panes[Right].x_srt = (term->cols / 2) + 2;
	panes[Right].x_end = term->cols - 2;
	panes[Right].width = panes[Right].x_end - panes[Right].x_srt + 1;
	panes[Right].dircol = cpanelr;
	panes[Right].firstrow = 0;
	panes[Right].direntry = ecalloc(0, sizeof(Entry));
	strncpy(panes[Right].dirname, home, MAX_P);
	panes[Right].hdir = 1;
	panes[Right].inotify_wd = -1;
	// panes[Right].parent_row = 1;
}

static void
set_term(void)
{
	setvbuf(stdout, NULL, _IOFBF, 0);

	nterm = oterm;
	if (get_term_size(&nterm.rows, &nterm.cols) == -1)
		die("get_term_size:");
	nterm.term.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	nterm.term.c_oflag &= ~(OPOST);
	nterm.term.c_cflag |= (CS8);
	nterm.term.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	// nterm.term.c_cc[VMIN] = 0;
	// nterm.term.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &nterm.term) < 0)
		// if (tcsetattr(STDIN_FILENO, TCSANOW, &nterm.term) < 0)
		die("tcsetattr:");

	write(STDOUT_FILENO,
	    "\033[s"	  // save cursor position
	    "\033[?47h"	  // save screen
	    "\033[H"	  // go home
	    "\033[?1049h" // enables the alternative buffer
	    "\033[?7l"	  // disable line wrapping
	    //"\033[?25l"   // hide cursor
	    //"\033[2J"     // clear screen
	    //	"\033[2;%dr", // limit scrolling to our rows
	    ,
	    25);
}

static void
sighandler(int signo)
{
	switch (signo) {
	case SIGWINCH:
		t_resize();
		break;
	case SIGUSR1:
		if (fork_pid > 0) /* while forking don't listdir() */
			return;
		if (listdir(&panes[Left]) < 0)
			print_error(strerror(errno));
		if (listdir(&panes[Right]) < 0)
			print_error(strerror(errno));
		t_resize();
		break;
	case SIGUSR2:
		break;
	}
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

static int
spawn(const void *com_argv, size_t com_argc, const void *f_argv, size_t f_argc,
    char *fn, int waiting)
{
	int ws;
	size_t argc;
	pid_t r;

	argc = com_argc + f_argc + 2;
	char *argv[argc];

	memcpy(argv, com_argv, com_argc * sizeof(char *));	  /* command */
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

static void
start_change(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	uint32_t c = 0;

	cont_change = 0;
	print_status(cprompt, "c [womf]");
	while (1) {
		c = getkey();
		grabkeys(c, ckeys, ckeyslen);
		if (cont_change == -1)
			return;
		break;
	}
}

static void
start_ev(void)
{
	uint32_t c = 0;

	while (1) {
		c = getkey();
		grabkeys(c, nkeys, nkeyslen);
	}
}

static void
start_filter(const Arg *arg)
{
	if (cpane->dirc < 1)
		return;
	char *user_input;
	user_input = ecalloc(MAX_USRI, sizeof(char));
	if (get_usrinput(user_input, MAX_USRI, "filter ") < 0) {
		free(user_input);
		return;
	}
	cpane->filter = user_input;
	if (listdir(cpane) < 0)
		print_error("no match");
	cpane->filter = NULL;
	free(user_input);
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
start_vmode(const Arg *arg)
{
	uint32_t c = 0;

	if (cpane->dirc < 1) {
		return;
	}

	if (sel_indexes != NULL) {
		free(sel_indexes);
		sel_indexes = NULL;
	}

	sel_indexes = ecalloc(cpane->dirc, sizeof(size_t));
	sel_indexes[0] = cpane->hdir;
	cont_vmode = 0;
	print_status(cprompt, "-- VISUAL --");

	while (1) {
		c = getkey();
		grabkeys(c, vkeys, vkeyslen);
		if (cont_vmode == -1)
			return;
	}
}

static void
start(void)
{
	term = init_term();
	draw_frame(cframe);
	set_panes();
	get_editor();
	get_shell();
	start_signal();
	if (fsev_init() < 0)
		die("fsev_init");
	if (listdir(&panes[Left]) < 0)
		print_error(strerror(errno));
	if (listdir(&panes[Right]) < 0)
		print_error(strerror(errno));
	pthread_create(&fsev_thread, NULL, read_th, NULL);
	start_ev();
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

void
termb_append(const char *s, int len)
{
	// printf("%s", s);
	char *new = erealloc(ab.b, ab.len + len);
	if (new == NULL)
		return;
	memcpy(&new[ab.len], s, len);
	ab.b = new;
	ab.len += len;
}

void
termb_free(void)
{
	free(ab.b);
	ab.b = NULL;
	ab.len = 0;
}

void
termb_write(void)
{
	// fflush(stdout);
	write(STDOUT_FILENO, ab.b, ab.len);
	// write(STDOUT_FILENO, "\x1b[0;0m", 6); // reset colors
	termb_free();
}

static void
toggle_df(const Arg *arg)
{
	show_dotfiles = !show_dotfiles;
	if (listdir(&panes[Left]) < 0)
		print_error(strerror(errno));
	if (listdir(&panes[Right]) < 0)
		print_error(strerror(errno));
}

static void
t_resize(void)
{
	write(STDOUT_FILENO, "\x1b[2J", 4); // clear screen
	get_term_size(&term->rows, &term->cols);

	panes[Left].x_end = (term->cols / 2) - 1;
	panes[Left].width = panes[Left].x_end - panes[Left].x_srt + 1;

	panes[Right].x_srt = (term->cols / 2) + 2;
	panes[Right].x_end = term->cols - 2;
	panes[Right].width = panes[Right].x_end - panes[Right].x_srt + 1;

	// print_status(cdir, "left=%d right=%d", panes[Left].width,
	//     panes[Right].width);

	draw_frame(cframe);
	print_dir_entries(&panes[Left]);
	print_dir_entries(&panes[Right]);
	if (cpane->dirc > 0)
		add_hi(cpane, cpane->hdir - 1);
	print_info(cpane, NULL);
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
