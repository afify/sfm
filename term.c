/* See LICENSE file for copyright and license details. */

#include <sys/ioctl.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "term.h"
#include "util.h"

/* global variables */
Term oterm;
Term nterm;
Tbuf ab;

/* function declarations */
static void set_term(void);
static void backup_term(void);

/* function implementations */
Term *
init_term()
{
	backup_term();
	set_term();
	return &nterm;
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

void
clear_status(void)
{
	write(STDOUT_FILENO,
	    "\x1b[999;1f" // moves cursor to line 999, column 1
	    "\x1b[2K",	  // erase the entire line
	    12);
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
	printf("%s", s);
	// char *new = erealloc(ab.b, ab.len + len);
	// if (new == NULL)
	//	return;
	// memcpy(&new[ab.len], s, len);
	// ab.b = new;
	// ab.len += len;
}

void
termb_write(void)
{
	fflush(stdout);
	// write(STDOUT_FILENO, ab.b, ab.len);
	//  write(STDOUT_FILENO, "\x1b[0;0m", 6); // reset colors
	// termb_free();
}
