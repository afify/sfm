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
	char high_dir[MAX_P]; // highlighted_dir fullpath
	int dirx;             // position of title
	size_t hdir;          // highlighted_dir
	size_t dirc;          // total files in dir
	uint16_t dir_bg;
	uint16_t dir_fg;
} Pane;

typedef struct {
	char *name;
	unsigned int size;
	char *perm;
	char *user;
	char *group;
} Cursor;

/* function declarations */
static void print_tb(const char*, int, int, uint16_t, uint16_t);
static void printf_tb(int, int, uint16_t, uint16_t, const char*, ...);
static int sort_name(const void *const, const void *const);
static char *get_full_path(char*, char*);
static char *get_parent(char*);
static int open_files(char*);
static int set_panes(Pane*, Pane*);
static int listdir(Pane*);
static int check_dir(char*);
static void clear(int, int, int, uint16_t);
static void clear_status(void);
static void clear_error(void);
static void clear_pane(int);
static void print_status(const char*, ...);
static void print_error(const char*, ...);
static void press(struct tb_event*, Pane*);
static void draw_frame(void);
static void t_resize(void);
static int start(void);
static char *get_extentions(char*);

/* global variables */
static Pane pane_r, pane_l;
static int current_pane = 0;
static int resize = 0;

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
printf_tb(int x, int y, uint16_t fg, uint16_t bg, const char *fmt, ...)
{
	char buf[4096];
	va_list vl;
	va_start(vl, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, vl);
	va_end(vl);
	print_tb(buf, x, y, fg, bg);
}

static int
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
	// TODO
	/* open editor in other window */
	/* wait option */
	char *editor, *file_ex, *software, *term;
	int status, r;
	pid_t pid;
	int wait_finish = 0;

	editor = getenv("EDITOR");
	term = getenv("TERM");

	file_ex = get_extentions(filename);

	if (strcmp(file_ex, "png") == 0) {
		software = images;
	} else if (strcmp(file_ex, "mp4") == 0) {
		software = videos;
	} else if (strcmp(file_ex, "pdf") == 0) {
		software = pdf;
	} else {
		software = editor;
		wait_finish = 1;
	}

	free(file_ex);

	if (software == NULL)
		term = "vi";

	char *filex[] = {software, filename, NULL};

	pid = fork();
	switch (pid) {
	case -1:
		return -1;
	case 0:
		(void)execvp(filex[0], filex);
		exit(EXIT_SUCCESS);
// 		_exit(1);
	default:
		if (wait_finish == 1) {
		while ((r = waitpid(pid, &status, 0)) == -1 && errno == EINTR)
			continue;
		if (r == -1)
			return -1;
		if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
			return -1;
		}
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
set_panes(Pane *pane_l, Pane *pane_r)
{
	int height, width;
	char *home;

	home = getenv("HOME");
	width = tb_width();   // width of the terminal screen
	height = tb_height(); // height of the terminal screen

	if (home == NULL)
		home = "/";

	strcpy(pane_l->dirn, home);
	pane_l->dirx = 2;
// 	pane_l->width = (width / 2) - 4;
// 	pane_l->height = height - 2;
	pane_l->dir_fg = pane1_dir_f;
	pane_l->dir_bg = pane1_dir_b;
	if (resize == 0)
		pane_l->hdir = 1;

	strcpy(pane_r->dirn, "/");
	pane_r->dirx = (width / 2) + 2;
// 	pane_r->width = (width / 2) - 4;
// 	pane_r->height = height - 2;
	pane_r->dir_fg = pane2_dir_f;
	pane_r->dir_bg = pane2_dir_b;
	if (resize == 0)
		pane_r->hdir = 1;

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
	clear(1, width-30, height-2, status_bcol);
}

static void
clear_error(void)
{
	int width, height;
	width = tb_width();
	height = tb_height();
	clear(width-30, width-1, height-2, status_bcol);
}

static void
clear_pane(int pane)
{
	int i, x, ex, y, width, height;
	width = tb_width();
	height = tb_height();
	y = 0;
	i = 0;

	if (pane == pane_l.dirx){
		x = 2;
		ex = (width/2) - 1;
	} else if (pane == pane_r.dirx) {
		x = (width/2) + 1;
		ex = width -1;
	}

	while (i < height-2) {
// 		clear(x, ex, y, dir_nor_f);
		clear(x, ex, y, TB_DEFAULT);
		i++;
		y++;
	}
	// TODO if empty directory no listdir -> no dir name
	/* draw top line */
	for (y = x; y < ex ; ++y) {
		tb_change_cell(y, 0, u_hl, frame_fcol, frame_bcol);
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
	print_tb(buf, width-30, height-2, 124, status_bcol);

}

static int
listdir(Pane *cpane)
{
	DIR *dir;
	struct dirent *entry;
	struct dirent **list; // array of elements
	size_t y, i;
	uint16_t bg, fg;
	char *fullpath;
	size_t height, width, dyn_max, dyn_y;

	height = tb_height() - 2;
	width = (tb_width() / 2) - 4;
	cpane->dirc = - 2; /* dont't count . .. */
	i = 0;

	dir = opendir(cpane->dirn);
	if (dir == NULL) {
		print_error("%s", strerror(errno));
		return -1;
	}

	/* get content sum */
	while ((entry = readdir(dir)) != 0) {
		cpane->dirc++;
	}

	/* empty directory */
	if (cpane->dirc == 0) {
		if (closedir(dir) < 0)
			return -1;
		return 0;
	}

	rewinddir(dir); /* reset position */

	/* create array of entries */
	list = ecalloc(cpane->dirc, sizeof(*list));

	while ((entry = readdir(dir)) != 0) {
		if (!(strcmp(entry->d_name, ".") == 0  ||
			strcmp(entry->d_name, "..") == 0 )) {
			list[i++] = entry;
		}
	}

	qsort(list, cpane->dirc, sizeof(*list), sort_name);

	i = 0;
	y = 1;
	dyn_y = 0;

	if (cpane->dirc > height - 1) /* clear before print if scroll */
		clear_pane(cpane->dirx);

	if (cpane->hdir >= height - 4){
		i = (cpane->hdir - height) + 4;
		dyn_y = i;
	}

	dyn_max = MIN(cpane->dirc, height-1+i);

	while (i < dyn_max) {
		/* highlighted (cursor) */
		bg = file_nor_b;
		fg = file_nor_f;
		if (y + dyn_y == cpane->hdir) {
			bg = file_hig_b;
			fg = file_hig_f;
		}
		/* print each element in directory */
		printf_tb(cpane->dirx, y, fg, bg, "%.*s",
			width, list[i]->d_name);
		y++;
		i++;
	}

	/* get full path of cursor */
	fullpath = get_full_path(cpane->dirn,
			list[cpane->hdir-1]->d_name);
	strncpy(cpane->high_dir, fullpath, (size_t)MAX_P);

	/* print info in statusbar */
	print_status("%lu/%lu %s",
		cpane->hdir,
		cpane->dirc,
		cpane->high_dir);

	/* print current directory title */
	printf_tb(cpane->dirx, 0, cpane->dir_fg | TB_BOLD, cpane->dir_bg,
		" %.*s ", width, cpane->dirn);

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
press(struct tb_event *ev, Pane *cpane)
{
	char *parent;
	int isdir;
	int ret;
// 	char *old;
// 	char *fullpath;
	clear_error();

	switch (ev->ch) {
	case 'j':
		if (cpane->hdir < cpane->dirc) {
			cpane->hdir++;
			(void)listdir(cpane);
		}
		break;
	case 'k':
		if (cpane->hdir > 1) {
			cpane->hdir--;
			(void)listdir(cpane);
		}
		break;
	case 'h':
		parent = get_parent(cpane->dirn);
		if (check_dir(parent) == 0) {
			strcpy(cpane->dirn, parent);
			clear_pane(cpane->dirx);
			cpane->hdir = 1;
			(void)listdir(cpane);

		/* failed to open directory */
		} else {
			print_error("%s", strerror(errno));
		}
		free(parent);

		break;

	case 'l':
		/* try current cursor directory */
		isdir = check_dir(cpane->high_dir);
		switch (isdir) {
		case 0:
// 			cpane->dirn = cpane->high_dir;
			strcpy(cpane->dirn, cpane->high_dir);
			clear_pane(cpane->dirx);
			cpane->hdir = 1;
			(void)listdir(cpane);
			break;
		case 1:
			/* is not a directory open it */
			if (open_files(cpane->high_dir) < 0) {
				print_error("%s", strerror(errno));
			}
			tb_shutdown();
			ret = tb_init();
			if (ret != 0)
				die("tb_init() failed with error code %d\n", ret);
			t_resize();
			break;
		case -1:
			print_error("%s", strerror(errno));
		}

		break;
	case 'g':
		cpane->hdir = 1;
		(void)listdir(cpane);
		break;
	case 'G':
		cpane->hdir = cpane->dirc;
		(void)listdir(cpane);
		break;
	case 'M':
		cpane->hdir = (cpane->dirc/2);
		(void)listdir(cpane);
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
	(void)set_panes(&pane_l, &pane_r);
	(void)listdir(&pane_r);
	(void)listdir(&pane_l);
	tb_present();

}

static int
start(void)
{
	int ret, mode, support_256, support_n, init_height, init_width;
	struct tb_event ev;
// 	Pane pane_r, pane_l;

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
	(void)set_panes(&pane_l, &pane_r);
	(void)listdir(&pane_r);
	(void)listdir(&pane_l);
	tb_present();

	while (tb_poll_event(&ev) != 0) {
		switch (ev.type) {
		case TB_EVENT_KEY:
			if (ev.ch == q) {
				tb_shutdown();
				return 0;
			} else if (ev.key == TB_KEY_SPACE) {
				current_pane = !current_pane;
			}

			if (current_pane == 0) {
				press(&ev, &pane_l);

			} else if (current_pane == 1) {
				press(&ev, &pane_r);
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
