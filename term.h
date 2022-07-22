/* See LICENSE file for copyright and  license details. */

#ifndef TERM_H
#define TERM_H

/* macros */
#define CLEAR_SCREEN write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
#define CLEAR_LINE write(STDOUT_FILENO, "\x1b[2K", 4);
#define CURSOR_HIDE write(STDOUT_FILENO, "\x1b[?25l", 6);
#define CURSOR_SHOW write(STDOUT_FILENO, "\x1b[?25h", 6);
#define CURSOR_RESTORE write(STDOUT_FILENO, "\x1b[u", 3);

#define NORM 0
#define BOLD 1
#define RVS  7

#define XK_Left                          0xff51  /* Move left, left arrow */
#define XK_Up                            0xff52  /* Move up, up arrow */
#define XK_Right                         0xff53  /* Move right, right arrow */
#define XK_Down                          0xff54  /* Move down, down arrow */

#define XK_CTRL(k)       ((k)&0x1f)
#define XK_UP            0x41
#define XK_DOWN          0x42
#define XK_LEFT          0x43
#define XK_RIGHT         0x44

#define XK_BACKSPACE     0x08
#define XK_TAB           0x09
#define XK_ENTER         0x0D
#define XK_ESC           0x1B
#define XK_SPACE         0x20

/* typedef */
typedef struct {
	int cx;
	int cy;
	int rows;
	int cols;
	int avail_cols;
	struct termios term;
} Term;

typedef struct {
	uint16_t fg;
	uint16_t bg;
	uint8_t attr;
} Cpair;

typedef union {
	uint16_t key; /* one of the TB_KEY_* constants */
	uint32_t ch; /* unicode character */
} Evkey;

typedef struct {
	char *b;
	int len;
} Tbuf;

/* function declarations */
Term* init_term(void);
void quit_term(void);
void draw_frame(Cpair);
char getkey(void);
void move_to_col(int);
void move_to(int, int);
void twrite(int, int, char *, size_t, Cpair);
int get_term_size(int *, int *);
void termb_append(const char *s, int len);
void termb_write(void);
void termb_free(void);

#endif /* TERM_H */
