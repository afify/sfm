/* See LICENSE file for copyright and license details. */
#include <dirent.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"
#include "termbox.h"
#include "util.h"

/* macros */

/* typedef */
struct Panel{
	char dirn[256];     // directory name
	char *dir;          // list of files in dir
	unsigned int dirx;  // directory name x position
	unsigned int dirc;  // total files in dir
	unsigned int hdir;  // highlighted_dir
	unsigned int nw[2];
	unsigned int ne[2];
	unsigned int sw[2];
	unsigned int se[2]; // max xy positions
};

/* function declarations */
static void print_tb(const char*, int, int, uint16_t, uint16_t);
static void printf_tb(int, int, uint16_t, uint16_t, const char*, ...);
static void printf_tb_c(int, int, uint16_t, uint16_t, const char*, ...);
static int compare(const void*, const void*);
static int set_panels(struct Panel*, struct Panel*, char*);
static int listdir(char*, int);
static void print_insert_mode(const char*, ...);
static void press(struct tb_event*);
static void draw_frame(void);
static int start(void);

/* global variables */
static const int nmode = 0;
static const int imode = 1;
static unsigned int current_mode = nmode;
static unsigned int highlighted_dir = 1;
static unsigned int input_mode_line;

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

static void
printf_tb_c(int max_width, int y, uint16_t fg, uint16_t bg, const char *fmt, ...)
{
	char buf[4096];
	va_list vl;
	va_start(vl, fmt);
	vsnprintf(buf, sizeof(buf), fmt, vl);
	va_end(vl);
	int x = ((max_width - strlen(fmt)) / 2 );
	print_tb(buf, x, y, fg, bg);
}

static int
compare( const void* a, const void* b)
{
	int int_a = * ( (int*) a );
	int int_b = * ( (int*) b );

	if ( int_a == int_b ) {
		return 0;
	} else if ( int_a < int_b ) {
		return -1;
	} else {
		return 1;
	}
}

static int
set_panels(struct Panel *panel_l, struct Panel *panel_r, char *home)
{
	int height, width;
	width = tb_width(); // width of the terminal screen
	height = tb_height(); // height of the terminal screen

	int title1_total_w = width / 2;

	strcpy(panel_l->dirn, home);
	size_t title1_len = strlen(panel_l->dirn);
	panel_l->dirx = 2;
// 	panel_l->dir = sorted_dir(panel_l.dirn);
// 	panel_l->dirc = count_dir(panel_l.dirn);
	panel_l->hdir = 1;

	panel_l->nw[0] = 0;
	panel_l->nw[1] = 0;
	panel_l->ne[0] = (width - 1) / 2;
	panel_l->ne[1] = 0;
	panel_l->sw[0] = 0;
	panel_l->sw[1] = height - 1;
	panel_l->se[0] = (width - 1) / 2;
	panel_l->se[1] = height - 1;

	strcpy(panel_r->dirn, home);
	size_t title2_len = strlen(panel_r->dirn);
	panel_r->dirx = (width / 2) + 1;
// 	panel_r->dir = sorted_dir(panel_l.dirn);
// 	panel_r->dirc = count_dir(panel_l.dirn);
	panel_r->hdir = 1;

	panel_r->nw[0] = (width -1 ) / 2;
	panel_r->nw[1] = 0;
	panel_r->ne[0] = (width - 1);
	panel_r->ne[1] = 0;
	panel_r->sw[0] = (width -1) / 2;
	panel_r->sw[1] = height - 1;
	panel_r->se[0] = width - 1;
	panel_r->se[1] = height - 1;

	printf_tb(panel_l->dirx, 0, panel1_dir_f | TB_BOLD, panel1_dir_b,
		" %s ", panel_l->dirn);

	printf_tb(panel_r->dirx, 0, panel2_dir_f | TB_BOLD, panel2_dir_b,
		" %s ", panel_r->dirn);

// 	listdir(panel_l.dirn);
// 	printf_tb(title2_x, 0, TB_BLUE | TB_BOLD, TB_DEFAULT, " %s ", title2_str);

// 	tb_change_cell(panel_r.nw[0], panel_r.nw[1], u_cnw, TB_RED, frame_bcol);
// 	tb_change_cell(panel_r.ne[0], panel_r.ne[1], u_cne, TB_BLUE, frame_bcol);
// 	tb_change_cell(panel_r.sw[0], panel_r.sw[1], u_csw, TB_YELLOW, frame_bcol);
// 	tb_change_cell(panel_r.se[0], panel_r.se[1], u_cse, TB_MAGENTA, frame_bcol);

}

static int
listdir(char *dirname, int x)
{
	int y = 1;
	char *all_dirs[256];
	int all_dirs_l;
	DIR *dir;
	struct dirent *ent;
	struct stat buf;
	off_t size;
	char *full_path;
	size_t full_path_size = (size_t)1024;


	dir = opendir(dirname);
	if (dir == NULL)
		return -1;

	while ((ent = readdir (dir)) != NULL) {
		if (!(strcmp(ent->d_name, ".") == 0  ||
			strcmp(ent->d_name, "..") == 0 )) {

			full_path = ecalloc(full_path_size, sizeof(char));

			(void)snprintf(full_path, full_path_size, "%s/%s",
				dirname, ent->d_name);


			stat(full_path, &buf);
// 			fstat(ent->d_name, &buf);
			size = buf.st_size;

			/* is directory */
			if (S_ISDIR(buf.st_mode)) {
				if (y == highlighted_dir) {
					printf_tb(x, y, dir_hig_f| TB_BOLD,
					file_hig_b," %s %jd ",
					full_path, size);
				} else {
					printf_tb(x, y, dir_nor_f,
					file_nor_b, " %s %jd ",
					full_path, size);
				}

			/* is regular file */
			} else if (S_ISREG(buf.st_mode)) {

				if (y == highlighted_dir) {
					printf_tb(x, y, file_hig_f| TB_BOLD,
					file_hig_b," %s %jd ",
					full_path, size);
				} else {
					printf_tb(x, y, file_nor_f,
					file_nor_b, " %s %jd ",
					full_path, size);
				}

			/* other files */
			} else {
				if (y == highlighted_dir) {
					printf_tb(x, y, other_hig_f| TB_BOLD,
					file_hig_b," %s %jd ",
					full_path, size);
				} else {
					printf_tb(x, y, other_nor_f,
					file_nor_b, " %s %jd ",
					full_path, size);
				}
			}
			y++;
			all_dirs_l++;
			free(full_path);
		}
	}

		(void)closedir(dir);

// 	all_dirs_l = LEN(all_dirs);
// 	qsort(all_dirs, all_dirs_l, sizeof(char), compare)

	return 0;
}

static void
print_insert_mode(const char *fmt, ...)
{
	int height, width;
	width = tb_width(); // width of the terminal screen
	height = tb_height(); // height of the terminal screen

	char buf[4096];
	va_list vl;
	va_start(vl, fmt);
	vsnprintf(buf, sizeof(buf), fmt, vl);
	va_end(vl);
	print_tb(buf, 2, height-2, TB_CYAN, TB_DEFAULT);

// 	printf_tb(2, height-2, TB_CYAN, TB_DEFAULT, "%s", input);
}

static void
press(struct tb_event *ev)
{
	int total_dirs = 23;

	switch (ev->ch) {
	case 'j':
		if (highlighted_dir < total_dirs)
			highlighted_dir++;
			print_insert_mode("%i", highlighted_dir);
		break;
	case 'k':
		if (highlighted_dir > 1)
			highlighted_dir--;
			print_insert_mode("%i", highlighted_dir);
		break;

	case 'h':
		print_insert_mode("go to parent");
		break;
	case 'l':
		print_insert_mode("open dir or file");
		break;
	}

}

static void
draw_frame(void)
{
	int height, width, i;

	width = tb_width(); // width of the terminal screen
	height = tb_height(); // height of the terminal screen

	/* 3 horizontal lines */
	for (i = 1; i < width-1 ; ++i) {
		tb_change_cell(i, 0,        u_hl, frame_fcol, frame_bcol);
		tb_change_cell(i, height-1, u_hl, frame_fcol, frame_bcol);
// 		tb_change_cell(i, height-2, ' ',  frame_fcol, TB_GREEN);
	}

	/* 3 vertical lines */
	for (i = 1; i < height-1 ; ++i) {
		tb_change_cell(0,           i, u_vl, frame_fcol, frame_bcol);
		tb_change_cell((width-1)/2, i, u_vl, frame_fcol, frame_bcol);
		tb_change_cell(width-1,     i, u_vl, frame_fcol, frame_bcol);
	}

	/* 4 corners */
	tb_change_cell(0,       0,        u_cnw, frame_fcol, frame_bcol);
	tb_change_cell(width-1, 0,        u_cne, frame_fcol, frame_bcol);
	tb_change_cell(0,       height-1, u_csw, frame_fcol, frame_bcol);
	tb_change_cell(width-1, height-1, u_cse, frame_fcol, frame_bcol);

	/* 2 middel top and bottom */
	tb_change_cell((width-1)/2, 0,        u_mn, frame_fcol, frame_bcol);
	tb_change_cell((width-1)/2, height-1, u_ms, frame_fcol, frame_bcol);

}

static int
start(void)
{
	int ret;
	int mode;
	int height, width;
	struct tb_event ev;
	struct Panel panel_r, panel_l;
	char *home;
	char *editor;

	width = tb_width();          /* width of the terminal screen */
	height = tb_height();        /* height of the terminal screen */
	home = getenv("HOME");       /* default directory */
	editor = getenv("EDITOR");   /* open files with */

	if (home == NULL) /* failed to read env var HOME */
		home = "/mnt/data/dev/sandbox/calcurse";

	ret = tb_init();
	if (ret != 0)
		die("tb_init() failed with error code %d\n", ret);

	mode = tb_select_input_mode(TB_INPUT_ESC);
	tb_select_output_mode(TB_OUTPUT_256);

	tb_clear();
	draw_frame();
	set_panels(&panel_l, &panel_r, home);
	listdir(panel_l.dirn, 1);
// 	listdir(panel_r.dirn, (width/2) + 1);
	tb_present();

	while (tb_poll_event(&ev) != 0) {
		switch (ev.type) {
		case TB_EVENT_KEY:

			if (ev.ch == 'q') {
				tb_shutdown();
				return 0;
			}

			tb_clear();
			draw_frame();
			set_panels(&panel_l, &panel_r, home);
			press(&ev);
			listdir(panel_l.dirn, 1);
// 	listdir(panel_r.dirn, (width/2) + 1);
			tb_present();
			break;

		case TB_EVENT_RESIZE:
			tb_clear();
			draw_frame();
			set_panels(&panel_l, &panel_r, home);
			press(&ev);
			listdir(panel_l.dirn, 1);
// 			listdir(panel_r.dirn, (width/2) + 1);
// 			pretty_print_resize(&ev);
			tb_present();
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
	if (pledge("stdio", NULL) == -1)
		die("pledge");
#endif /* __OpenBSD__ */
	if (argc == 1) {
		if (start() != 0)
			die("start failed");
	} else if (
		argc == 2 && strlen(argv[1]) == (size_t)2 &&
		strcmp("-v", argv[1]) == 0) {
			die("smp-"VERSION);
	} else {
		die("usage: smp [-v]");
	}
	return 0;
}
