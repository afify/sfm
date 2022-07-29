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
#define RVS 7

#define XK_CTRL(k) ((k)&0x1f)
#define XK_ALT(k) (k)1b
#define XK_UP 0x415b1b
#define XK_DOWN 0x425b1b
#define XK_RIGHT 0x435b1b
#define XK_LEFT 0x445b1b
#define XK_HOME 0x485b1b
#define XK_END 0x7e345b1b
#define XK_PGUP 0x7e355b1b
#define XK_PGDOWN 0x7e365b1b

#define XK_BACKSPACE 0x08
#define XK_TAB 0x09
#define XK_ENTER 0x0D
#define XK_ESC 0x1B
#define XK_SPACE 0x20

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

/* function declarations */
Term *init_term(void);
void quit_term(void);
void draw_frame(Cpair);
uint32_t getkey(void);
void move_to_col(int);
void move_to(int, int);
void twrite(int, int, char *, size_t, Cpair);
void print_status(Cpair, const char *, ...);
int get_term_size(int *, int *);
void termb_append(const char *s, int len);
void termb_write(void);
void termb_free(void);

#endif /* TERM_H */
