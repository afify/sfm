#ifndef SFM_H
#define SFM_H

#include <sys/types.h>
#include <sys/stat.h>

#include <limits.h>
#include <stdint.h>

/* macros */
#define NORM   0
#define BOLD   1
#define DIM    2
#define ITALIC 3
#define UNDERL 4
#define BLINK  5
#define RVS    7
#define HIDDEN 8
#define STRIKE 9

#define XK_CTRL(k)   ((k) & 0x1f)
#define XK_ALT(k)    (k)1b
#define XK_UP        0x415b1b
#define XK_DOWN      0x425b1b
#define XK_RIGHT     0x435b1b
#define XK_LEFT      0x445b1b
#define XK_HOME      0x485b1b
#define XK_END       0x7e345b1b
#define XK_PGUP      0x7e355b1b
#define XK_PGDOWN    0x7e365b1b
#define XK_BACKSPACE 0x7f
#define XK_TAB       0x09
#define XK_ENTER     0x0D
#define XK_ESC       0x1B
#define XK_SPACE     0x20

#define UINT8_LEN  3
#define UINT16_LEN 5

#define GROUP_MAX     32
#define USER_MAX      32
#define DATETIME_MAX  20
#define EXTENTION_MAX 4

#define MAX(A, B)        ((A) > (B) ? (A) : (B))
#define MIN(A, B)        ((A) < (B) ? (A) : (B))
#define LEN(A)           (sizeof(A) / sizeof(A[0]))
#define BETWEEN(X, A, B) ((A) <= (X) && (X) <= (B))

typedef struct {
	struct termios orig;
	struct termios new;
	int rows;
	int cols;
	char *buffer;
	unsigned long buffer_size;
	unsigned long buffer_left;
	ssize_t buffer_index;
} Terminal;

typedef struct {
	uint8_t fg;
	uint8_t bg;
	uint8_t attr;
} ColorPair;

typedef struct {
	char fullpath[PATH_MAX];
	char name[NAME_MAX];
	struct stat st;
} Entry;

typedef struct {
	char directory[PATH_MAX];
	pthread_t watcher_thread;
#if defined(__linux__)
	int inotify_fd;
	int watch_descriptor;
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
	defined(__APPLE__)
	int directory_fd;
	int kq;
#endif
} Watcher;

typedef struct {
	char path[PATH_MAX];
	Entry *entries;
	int entry_count;
	int start_index;
	int current_index;
	Watcher watcher;
} Pane;

typedef union {
	int i;
	const void *v;
} Arg;

typedef struct {
	const uint32_t k;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	const char **ext;
	size_t exlen;
	const void *v;
	size_t vlen;
} Rule;

typedef struct {
	char **source_paths;     /* Source file paths */
	size_t source_count;     /* Number of source files */
	char **command;          /* Command and its arguments */
	size_t command_count;    /* Command arg count */
	char *target;            /* Single file path or destination path */
	int wait_for_completion; /* Flag to wait for command completion */
} Command;

#define EVENT_SIZE          (sizeof(struct inotify_event))
#define EVENT_BUFFER_LENGTH (1024 * (EVENT_SIZE + 16))

void filesystem_event_init(void);
void filesystem_event_quit(void);
void start_watcher_threads(void);
void stop_watcher_threads(void);
void add_watch(Pane *);
void remove_watch(Pane *);
void *watch_directory(void *);

static void start(void);
static void init_term(void);
static void enable_raw_mode(void);
static void get_term_size(void);
static void get_env(void);
static int start_signal(void);
static void sighandler(int);
static void set_panes(void);
static void set_pane_entries(Pane *);
static int count_entries(const char *);
static int should_skip_entry(const struct dirent *);
static void get_fullpath(char *, char *, char *);
static int entry_compare(const void *const, const void *const);
static void update_screen(void);
static void disable_raw_mode(void);
static void append_entries(Pane *, int);
static void handle_keypress(char);
static void grabkeys(uint32_t, Key *, size_t);
static void print_status(ColorPair, const char *, ...);
static void display_entry_details(void);
static void get_entry_color(ColorPair *, mode_t);
static char *get_entry_datetime(time_t);
static char *get_entry_permission(mode_t);
static char *get_file_size(off_t);
static char *get_entry_owner(uid_t);
static char *get_entry_group(gid_t);
static int check_dir(char *);
static int open_file(char *);
static char *get_file_extension(char *);
static int check_rule(char *);
static int execute_command(Command *);
static void termb_append(const char *, size_t);
static void termb_write(void);
static void termb_print_at(
	uint16_t, uint16_t, ColorPair, int, const char *, ...);
static void termb_resize(void);
static void cd_to_parent(const Arg *);
static void move_bottom(const Arg *);
static void move_cursor(const Arg *);
static void move_top(const Arg *);
static void open_entry(const Arg *);
static void quit(const Arg *);
static void switch_pane(const Arg *);
static void refresh(const Arg *arg);
static void toggle_dotfiles(const Arg *);
static void die(const char *, ...);
static void *ecalloc(size_t, size_t);
static void *erealloc(void *, size_t);
static void log_to_file(const char *, int, const char *, ...);

#endif // SFM_H
