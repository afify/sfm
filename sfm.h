/* See LICENSE file for copyright and  license details. */

#ifndef SFM_H
#define SFM_H

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
} Termbuf;

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
static Termbuf ab;
static Term *term, oterm, nterm;
static char **sel_files;
static char *editor[2];
static char *shell[2];
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

#endif /* SFM_H */
