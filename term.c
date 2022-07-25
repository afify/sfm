/* See LICENSE file for copyright and license details. */

#include <sys/ioctl.h>

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>

#include "term.h"
#include "util.h"

/* global variables */
Term oterm;
Term nterm;
Tbuf ab;

/* function declarations */
static void set_term(void);
static void reset_term(void);
static void backup_term(void);

/* function implementations */
Term *
init_term()
{
	backup_term();
	set_term();
	// raw_mode();
	// CLEAR_SCREEN
	// CURSOR_HIDE
	return &nterm;
}

void
quit_term(void)
{
	reset_term();
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

	for (y = 0; y < nterm.cols; y++) {
		termb_append(buf, buflen);
	}

	for (y = 0; y < nterm.rows - 2; y++) {
		termb_append(buf, buflen);
		move_to_col(nterm.cols / 2);
		termb_append(buf, buflen);
		move_to_col(nterm.cols - 1);
		termb_append(buf, buflen);
		termb_append("\r\n", 2);
	}

	for (y = 0; y < nterm.cols; y++) {
		termb_append(buf, buflen);
	}
	// termb_append("\r\n", 2);

	// for (y = 0; y < nterm.cols; y++) {
	//	termb_append(buf, buflen);
	// }

	termb_write();
}

uint32_t
getkey(void)
{
	uint32_t r = 0;

	if ((read(STDIN_FILENO, &r, sizeof(uint32_t))) < 0)
		die("read:");
	return r;
}

void
twrite(int x, int y, char *str, size_t len, Cpair col)
{
	if (x > nterm.cols || y > nterm.rows)
		return;
	char buf[nterm.cols];
	size_t buflen;

	snprintf(buf, nterm.cols,
	    "\x1b[%d;%df\x1b[%d;48;5;%d;38;5;%dm%s\x1b[0;0m", y, x, col.attr,
	    col.bg, col.fg, str);
	buflen = strlen(buf);
	write(STDOUT_FILENO, buf, buflen);
}

void
print_status(Cpair col, const char *fmt, ...)
{
	char buf[1000];
	size_t buflen;
	va_list vl;
	va_start(vl, fmt);
	(void)vsnprintf(buf, 1000, fmt, vl);
	va_end(vl);
	buflen = strlen(buf);

	// clear_status();
	twrite(1, nterm.rows, buf, buflen, col);
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

// ============================================================================
// static - local functions
// ============================================================================
static void
backup_term(void)
{
	if (tcgetattr(STDIN_FILENO, &oterm.term) < 0)
		die("tcgetattr:");
}

static void
set_term(void)
{
	setvbuf(stdout, NULL, _IOFBF, 0);

	nterm = oterm;
	nterm.cx = 0;
	nterm.cy = 0;
	if (get_term_size(&nterm.rows, &nterm.cols) == -1)
		die("get_term_size:");
	nterm.avail_cols = (nterm.cols - 6) / 2;
	nterm.term.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	nterm.term.c_oflag &= ~(OPOST);
	nterm.term.c_cflag |= (CS8);
	nterm.term.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	nterm.term.c_cc[VMIN] = 0;
	nterm.term.c_cc[VTIME] = 1;

	// if (tcsetattr(STDIN_FILENO, TCSANOW, &nterm.term) < 0)
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &nterm.term) < 0)
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
reset_term(void)
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

// ============================================================================
// Buffer
// ============================================================================

void
termb_free(void)
{
	free(ab.b);
	ab.b = NULL;
	ab.len = 0;
}

void
termb_append(const char *s, int len)
{
	char *new = erealloc(ab.b, ab.len + len);
	if (new == NULL)
		return;
	memcpy(&new[ab.len], s, len);
	ab.b = new;
	ab.len += len;
}

void
termb_write(void)
{
	write(STDOUT_FILENO, ab.b, ab.len);
	write(STDOUT_FILENO, "\x1b[0;0m", 6); // reset colors
	termb_free();
}

// void
// disableRawMode()
//{
//	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &oterm.term) == -1)
//		die("tcsetattr");
// }

// void
// raw_mode()
//{
//	//if (tcgetattr(STDIN_FILENO, &oterm.term) == -1)
//	//	die("tcgetattr");
//	//atexit(disableRawMode);
//	//struct termios raw = oterm.term;
//
//	//raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
//	//raw.c_oflag &= ~(OPOST);
//	//raw.c_cflag |= (CS8);
//	//raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
//	//raw.c_cc[VMIN] = 0;
//	//raw.c_cc[VTIME] = 1;
//
//	//if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
//	//	die("tcsetattr");
// }

// void
// tprintf(int x, int y, Cpair col, const char *fmt, ...)
//{
//	if (x > oterm.cols || y > oterm.rows)
//		return;
//	char buf[oterm.avail_cols]; // fmt result
//	char str[oterm.avail_cols+1]; // with color and position
//	va_list vl;
//	va_start(vl, fmt);
//	(void)vsnprintf(str, oterm.avail_cols+1, fmt, vl);
//	va_end(vl);
//
//	char mov[4+6];
//	snprintf(mov, 10, "\x1b[%d;%df", y, x);
//	write(STDOUT_FILENO, mov, strlen(mov));
//
//	snprintf(buf, oterm.cols,
//		"\x1b[%d;48;5;%d;38;5;%dm%s\x1b[0;0m",
//		col.attr, col.bg, col.fg, str);
//
//	write(STDOUT_FILENO, buf, strlen(buf));
// }
//
// void
// tprintf_status(const char *fmt, ...)
//{
//	int x = 2;
//	int y = oterm.rows - 1;
//
//	char buf[oterm.cols]; // fmt result
//	char str[oterm.cols]; // with color and position
//	size_t full_buf;
//	va_list vl;
//	va_start(vl, fmt);
//	(void)vsnprintf(buf, oterm.cols, fmt, vl);
//	va_end(vl);
//
//	//print_tb(buf, x, y, col.fg, col.bg);
//	snprintf(str, oterm.cols,
//		"\x1b[%d;%df\x1b[%d;48;5;%d;38;5;%dm%s\x1b[0;0m",
//		y, x, cstatus.attr, cstatus.bg, cstatus.fg, buf);
//	full_buf = strlen(str);
//	write(STDOUT_FILENO, str, full_buf);
//
//     //printf("\033[%d;H" // go to the bottom row
//     //        //"\033[2K" // clear the row
//     //        "\033[37;7;1m", // inverse + bold
//     //        rows);
// }
//

////void
////editorRefreshScreen()
////{
////	struct abuf ab = { NULL, 0 };
////	termb_append("\x1b[?25l", 6);
////	termb_append("\x1b[H", 3);
////	editorDrawRows(&ab);
////	char buf[32];
////	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", term.cy + 1, term.cx + 1);
////	termb_append(buf, strlen(buf));
////	termb_append("\x1b[?25h", 6);
////	write(STDOUT_FILENO, ab.b, ab.len);
////	abFree(&ab);
////}