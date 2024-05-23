#ifndef SFM_H
#define SFM_H

#include <sys/types.h>
#include <sys/stat.h>

#include <limits.h>
#include <stdint.h>

/* macros */
#define NORM 0
#define BOLD 1
#define DIM 2
#define ITALIC 3
#define UNDERL 4
#define BLINK 5
#define RVS 7
#define HIDDEN 8
#define STRIKE 9
#define BUFFER_SIZE 10240

#define XK_CTRL(k) ((k) & 0x1f)
#define XK_ALT(k) (k)1b
#define XK_UP 0x415b1b
#define XK_DOWN 0x425b1b
#define XK_RIGHT 0x435b1b
#define XK_LEFT 0x445b1b
#define XK_HOME 0x485b1b
#define XK_END 0x7e345b1b
#define XK_PGUP 0x7e355b1b
#define XK_PGDOWN 0x7e365b1b
#define XK_BACKSPACE 0x7f
#define XK_TAB 0x09
#define XK_ENTER 0x0D
#define XK_ESC 0x1B
#define XK_SPACE 0x20

#define UINT8_LEN 3
#define UINT16_LEN 5
#define MAX_DTF 20
#define MAX_GRPN 32

typedef struct {
	char *buffer;
	size_t size;
} TermBuffer;

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
	char path[PATH_MAX];
	Entry *entries;
	int entry_count;
	int start_index;
	int current_index;
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

static void die(const char *, ...);
static void *ecalloc(size_t, size_t);
static void enable_raw_mode(void);
static void disable_raw_mode(void);
static void get_term_size(int *, int *);
static int count_entries(const char *);
static int entry_compare(const void *const, const void *const);
static void list_dir(Pane *);
static void get_entry_color(ColorPair *, mode_t);
static void termb_append(const char *, size_t);
static void termb_write(void);
static void display_entries(Pane *, int, int, int);
static void display_entry_details(Pane *, int, int);
static void print_status(ColorPair, const char *, ...);
static void handle_keypress(char);
static void move_cursor(const Arg *);
static void switch_pane(const Arg *);
static void quit(const Arg *);
static void grabkeys(uint32_t, Key *, size_t);
static void update_screen(void);
static int get_fdt(char *, time_t);
static void get_entry_owner_group(uid_t, gid_t);
static char *get_fperm(mode_t);
static char *get_fsize(off_t);
static void print_info(void);
static int should_skip_entry(const struct dirent *);
static void get_fullpath(char *, char *, char *);
static void termb_print_at(uint16_t, uint16_t, ColorPair, int, const char *, ...);
static void set_panes(void);

#endif // SFM_H
