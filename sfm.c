/* See LICENSE file for copyright and license details. */

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include "config.h"
#include "termbox.h"
#include "util.h"

#define MAX_P 4095
#define MAX_N 255

/* typedef */
typedef struct {
	char dirn[MAX_P];     // directory name
	int dirc;             // total files in dir
	int dirx;             // position of title
	unsigned long hdir;   // highlighted_dir
	char high_dir[MAX_P]; // highlighted_dir fullpath
	uint16_t dir_fg;
	uint16_t dir_bg;
	int width;
	int height;
} Panel;

typedef struct {
	char *name;
	unsigned int size;
	char *permission;
	char *parent;
	char *user;
	char *group;
} Cursor_entry;

/* function declarations */
static void print_tb(const char*, int, int, uint16_t, uint16_t);
static void printf_tb(int, int, uint16_t, uint16_t, const char*, ...);
static int sort_name(const void *const, const void *const);
static char *get_full_path(char*, char*);
static char *get_parent(char*);
static int open_files(char*);
static int set_panels(Panel*, Panel*);
static int listdir(Panel*);
static int check_dir(char*);
static void clear(int, int, int, uint16_t);
static void clear_status(void);
static void clear_error(void);
static void clear_panel(int);
static void print_status(const char*, ...);
static void print_error(const char*, ...);
static void press(struct tb_event*, Panel*);
static void draw_frame(void);
static void t_resize(void);
static int start(void);
static char *get_extentions(char*);

/* global variables */
static Panel panel_r, panel_l;
static int current_panel = 0;
static int resize = 0;

/* function implementations */
static void
print_tb(const char *str, int x, int y, uint16_t fg, uint16_t bg)
{
	while (*str) {
		uint32_t uni;
		str += tb_utf8_char_to_unicode(&uni, str);
		tb_change_cell(x, y, uni, fg, bg);
		x++;
	}
}

static void
printf_tb(int x, int y, uint16_t fg, uint16_t bg, const char *fmt, ...)
{
	char buf[4096];
	va_list vl;
	va_start(vl, fmt);
	vsnprintf(buf, sizeof(buf), fmt, vl);
	va_end(vl);
	print_tb(buf, x, y, fg, bg);
}

int
sort_name(const void *const A, const void *const B)
{
	int result;
	result = strcmp((*(struct dirent **) A)->d_name,
		(*(struct dirent **) B)->d_name);
	return result;
}

static char *
get_full_path(char *first, char *second)
{
	char *full_path;
	size_t full_path_len;

	full_path_len = strlen(first) + strlen(second) + 2;
	full_path = ecalloc(full_path_len, sizeof(char));

	if (strcmp(first, "/") == 0) {
		(void)snprintf(full_path, full_path_len, "/%s", second);

	} else {
		(void)snprintf(
			full_path, full_path_len, "%s/%s", first, second);
	}

	return full_path;
}

static char *
get_parent(char *dir)
{
	char *parent;
	char *dot;
	size_t counter, len, i;

	dot = "/";
	counter = 0;
	len = strlen(dir);

	for (i = len-1; i > 0; i--) {
		if (dir[i] == *dot) {
			break;
		} else {
			counter++;
		}
	}

	i = len - counter - 1;
	if (i == 0 )
		i = len - counter;
	parent = ecalloc(i+1, sizeof(char));
	strncpy(parent, dir, i);

	return parent;
}

static int
open_files(char *filename)
{
	char *term;
	term = getenv("TERM");

	char software[1000];
	char *file_ex = get_extentions(filename);

	if (strcmp(file_ex, "png") == 0) {
		strcpy(software, "sxiv");
	} else if (strcmp(file_ex, "mp4") == 0) {
		strcpy(software, "mpv");
	} else if (strcmp(file_ex, "pdf") == 0) {
		strcpy(software, "mupdf");
	} else {
		strcpy(software, "vi");
	}

	free(file_ex);

	pid_t pid;
	int status, r;
	char *filex[] = {software, filename, NULL};

	pid = fork();
	switch (pid) {
	case -1:
		return -1;
	case 0:
		(void)execvp(filex[0], filex);
		_exit(1);
// 	default:
// 		while ((r = waitpid(pid, &status, 0)) == -1 && errno == EINTR)
// 			continue;
// 		if (r == -1)
// 			return -1;
// 		if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
// 			return -1;
	}

	return 0;

// 	int exitc;
// 	char *ext;

// 	ext = get_extentions(filename);
// 	if (strcmp(ext, "mp4") == 0) {
// 		char *filex[] = {"mpv", filename, NULL};
// 		exitc = execvp(filex[0], filex);
// 		free(ext);
// 		return exitc;
// 	} else if (strcmp(ext, "c") == 0) {
// // 			if (fork() == 0) {

// // 		setsid();
// 		char *filex[] = {"vim", filename, NULL};
// 		exitc = execvp(filex[0], filex);
// 		free(ext);
// 		return exitc;
// // 			}

// 	} else {
// 		free(ext);
// 		return 0;
// 	}
}

static int
set_panels(Panel *panel_l, Panel *panel_r)
{
	int height, width;
	char *home;

	home = getenv("HOME");
	width = tb_width();   // width of the terminal screen
	height = tb_height(); // height of the terminal screen

	if (home == NULL)
		home = "/";

	strcpy(panel_l->dirn, home);
// 	panel_l->dirn = &*home;
	panel_l->dirx = 2;
	panel_l->width = width / 2;
	panel_l->height = height;
	panel_l->dir_fg = panel1_dir_f;
	panel_l->dir_bg = panel1_dir_b;
	if (resize == 0)
		panel_l->hdir = 1;

	strcpy(panel_r->dirn, "/");
// 	panel_r->dirn = root;
	panel_r->dirx = (width / 2) + 2;
	panel_r->width = width / 2;
	panel_r->height = height;
	panel_r->dir_fg = panel2_dir_f;
	panel_r->dir_bg = panel2_dir_b;
	if (resize == 0)
		panel_r->hdir = 1;

	return 0;
}

static void
clear(int sx, int ex, int y, uint16_t bg)
{
	/* clear line from to */
	/* x = line number  vertical */
	/* y = column number horizontal */
	int i;
	for (i = sx; i < ex; i++) {
		tb_change_cell(i, y, 0x0000, TB_DEFAULT, bg);
	}

}

static void
clear_status(void)
{
	int width, height;
	width = tb_width();
	height = tb_height();
	clear(1, width-10, height-2, status_bcol);
}

static void
clear_error(void)
{
	int width, height;
	width = tb_width();
	height = tb_height();
	clear(width-20, width-1, height-2, status_bcol);
}

static void
clear_panel(int panel)
{
	int x, ex, y, width, height;
	width = tb_width();
	height = tb_height();
	y = 1;

	if (panel == panel_l.dirx){
		x = 2;
		ex = (width/2) - 1;
	} else if (panel == panel_r.dirx) {
		x = (width/2) + 1;
		ex = width -1;
	}

int i = 0;
	while (i < height-2) {
// 		clear(x, ex, y, dir_nor_f);
		clear(x, ex, y, TB_DEFAULT);
		i++;
		y++;
	}

}

static void
print_status(const char *fmt, ...)
{
	int height, width;
	width = tb_width();   // width of the terminal screen
	height = tb_height(); // height of the terminal screen

	char buf[256];
	va_list vl;
	va_start(vl, fmt);
	vsnprintf(buf, sizeof(buf), fmt, vl);
	va_end(vl);
	clear_status();
	print_tb(buf, 2, height-2, TB_DEFAULT, status_bcol);

}

static void
print_error(const char *fmt, ...)
{
	int height, width;
	width = tb_width();   // width of the terminal screen
	height = tb_height(); // height of the terminal screen

	char buf[256];
	va_list vl;
	va_start(vl, fmt);
	vsnprintf(buf, sizeof(buf), fmt, vl);
	va_end(vl);
	clear_error();
	print_tb(buf, width-20, height-2, 124, status_bcol);

}

static int
listdir(Panel *cpanel)
{
	DIR *dir;
	unsigned long y;
	size_t i;
	struct dirent *entry;
	struct dirent **list; // array of elements
	uint16_t bg, fg;
	char *fullpath;

	y = 1;
	i = 0;
	cpanel->dirc = -2;

	dir = opendir(cpanel->dirn);
	if (dir == NULL) {
		print_error("%s", strerror(errno));
		return -1;
	}

	/* get content sum */
	while ((entry = readdir(dir)) != 0) {
// 		if (entry < 0) {
// 			print_erro("%s", strerror(errno));
// 			return -1;
// 		}
		cpanel->dirc++;
	}

	rewinddir(dir); /* reset position */

	/* create array of entries */
	list = ecalloc(cpanel->dirc, sizeof(*list));

	while ((entry = readdir(dir)) != 0) {
		if (!(strcmp(entry->d_name, ".") == 0  ||
			strcmp(entry->d_name, "..") == 0 )) {
			list[i++] = entry;
		}
	}

	qsort(list, cpanel->dirc, sizeof(*list), sort_name);

	for (i = 0; i < cpanel->dirc; i++) {
		/* highlighted (cursor) */
		bg = file_nor_b;
		fg = file_nor_f;
		if (y == cpanel->hdir) {
			bg = file_hig_b;
			fg = file_hig_f;
		}
		printf_tb(cpanel->dirx, y, fg, bg, "%s ", list[i]->d_name);
		y++;
	}

	/* get full path of cursor */
	fullpath = get_full_path(cpanel->dirn,
			list[cpanel->hdir-1]->d_name);
	strncpy(cpanel->high_dir, fullpath, (size_t)MAX_P);

	/* print info in statusbar */
	print_status("%d/%d %s",
		cpanel->hdir,
		cpanel->dirc,
		cpanel->high_dir);

	print_error("%s", cpanel->dirn);

	char *parent;
	parent = get_parent(cpanel->dirn);
	print_error("%s", parent);
	free(parent);

	printf_tb(cpanel->dirx, 0, cpanel->dir_fg | TB_BOLD, cpanel->dir_bg,
		" %s ", cpanel->dirn);

	free(fullpath);
	free(list);
	if (closedir(dir) < 0)
		return -1;

	return 0;
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
press(struct tb_event *ev, Panel *cpanel)
{
	char *parent;
	int isdir;
// 	char *old;
// 	char *fullpath;
	clear_error();

	switch (ev->ch) {
	case 'j':
// 		case SEL_PGDN:
// 			if (cur < ndents - 1)
// 				cur += MIN((LINES - 4) / 2, ndents - 1 - cur);
// 			break;
// 		case SEL_PGUP:
// 			if (cur > 0)
// 				cur -= MIN((LINES - 4) / 2, cur);
// 			break;

		if (cpanel->hdir < cpanel->dirc) {
			cpanel->hdir++;
			(void)listdir(cpanel);
		}
		break;
	case 'k':
		if (cpanel->hdir > 1) {
			cpanel->hdir--;
			(void)listdir(cpanel);
		}
		break;
	case 'h':
		parent = get_parent(cpanel->dirn);
		print_error("%s", parent);
		if (check_dir(parent) == 0) {
// 			cpanel->dirn = parent;
			strcpy(cpanel->dirn, parent);
			clear_panel(cpanel->dirx);
			cpanel->hdir = 1;
			(void)listdir(cpanel);
			/* print the new directory */
			printf_tb(cpanel->dirx, 0,
				panel1_dir_f | TB_BOLD, panel1_dir_b,
				" %s ", cpanel->dirn);

		/* failed to open directory */
		} else {
			print_error("%s", strerror(errno));
		}
		free(parent);

		break;

	case 'l':

		/* try current cursor directory */
		isdir = check_dir(cpanel->high_dir);

		switch (isdir) {
		case 0:
// 			cpanel->dirn = cpanel->high_dir;
			strcpy(cpanel->dirn, cpanel->high_dir);
			clear_panel(cpanel->dirx);
			cpanel->hdir = 1;
			(void)listdir(cpanel);
			/* print the new directory */
			printf_tb(cpanel->dirx, 0,
				panel1_dir_f | TB_BOLD, panel1_dir_b,
				" %s ", cpanel->dirn);
			break;
		case 1:
		/* is not a directory open it */
			(void)open_files(cpanel->high_dir);
			break;

		default:
			print_error("%s", strerror(errno));

		}

		break;
	case 'g':
		cpanel->hdir = 1;
		(void)listdir(cpanel);
		break;
	case 'G':
		cpanel->hdir = cpanel->dirc;
		(void)listdir(cpanel);
		break;
	case 'M':
		cpanel->hdir = (cpanel->dirc/2);
		(void)listdir(cpanel);
		break;
	}
}

static void
draw_frame(void)
{
	int height, width, i;

	width = tb_width();   // width of the terminal screen
	height = tb_height(); // height of the terminal screen

	/* 2 horizontal lines */
	for (i = 1; i < width-1 ; ++i) {
		tb_change_cell(i, 0,        u_hl, frame_fcol, frame_bcol);
		tb_change_cell(i, height-1, u_hl, frame_fcol, frame_bcol);
	}

	/* 3 vertical lines */
	for (i = 1; i < height-1 ; ++i) {
		tb_change_cell(0,           i, u_vl, frame_fcol, frame_bcol);
		tb_change_cell((width-1)/2, i-1, u_vl, frame_fcol, frame_bcol);
		tb_change_cell(width-1,     i, u_vl, frame_fcol, frame_bcol);
	}

	/* 4 corners */
	tb_change_cell(0,       0,        u_cnw, frame_fcol, frame_bcol);
	tb_change_cell(width-1, 0,        u_cne, frame_fcol, frame_bcol);
	tb_change_cell(0,       height-1, u_csw, frame_fcol, frame_bcol);
	tb_change_cell(width-1, height-1, u_cse, frame_fcol, frame_bcol);

	/* 2 middel top and bottom */
	tb_change_cell((width-1)/2, 0,        u_mn, frame_fcol, frame_bcol);
// 	tb_change_cell((width-1)/2, height-1, u_ms, frame_fcol, frame_bcol);

}

static char *
get_extentions(char *str)
{
	char *ext;
	char *dot;
	size_t counter, len, i;

	dot = ".";
	counter = 0;
	len = strlen(str);

	for (i = len-1; i > 0; i--) {
		if (str[i] == *dot) {
			break;
		} else {
			counter++;
		}
	}

	ext = ecalloc(counter, sizeof(char));
	strncpy(ext, &str[len-counter], counter);
	return ext;
}

static void
t_resize(void)
{
	tb_clear();
	draw_frame();
	resize = 1;
	(void)set_panels(&panel_l, &panel_r);
	(void)listdir(&panel_r);
	(void)listdir(&panel_l);
	tb_present();

}

static int
start(void)
{
	int ret, mode, support_256, support_n, init_height, init_width;
	struct tb_event ev;
	char *editor;

	editor = getenv("EDITOR");

	ret = tb_init();
	if (ret != 0)
		die("tb_init() failed with error code %d\n", ret);

	init_width = tb_width();
	init_height = tb_height();
	mode = tb_select_input_mode(TB_INPUT_ESC);
	support_256 = tb_select_output_mode(TB_OUTPUT_256);

	if (support_256 != TB_OUTPUT_256)
		support_n = tb_select_output_mode(TB_OUTPUT_NORMAL);

	tb_clear();
	draw_frame();
	(void)set_panels(&panel_l, &panel_r);
	(void)listdir(&panel_r);
	(void)listdir(&panel_l);
	tb_present();

	while (tb_poll_event(&ev) != 0) {
		switch (ev.type) {
		case TB_EVENT_KEY:
			if (ev.ch == q) {
				tb_shutdown();
				return 0;
			} else if (ev.key == TB_KEY_SPACE) {
				current_panel = !current_panel;
			}

			if (current_panel == 0) {
				press(&ev, &panel_l);

			} else if (current_panel == 1) {
				press(&ev, &panel_r);
			}

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
	return 0;
}

int
main(int argc, char *argv[])
{
#ifdef __OpenBSD__
	if (pledge("stdio tty rpath proc exec", NULL) == -1)
		die("pledge");
#endif /* __OpenBSD__ */
	if (argc == 1) {
		if (start() != 0)
			die("start failed");
	} else if (
		argc == 2 && strlen(argv[1]) == (size_t)2 &&
		strcmp("-v", argv[1]) == 0) {
			die("sfm-"VERSION);
	} else {
		die("usage: sfm [-v]");
	}
	return 0;
}
