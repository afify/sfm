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
typedef struct {
	char dirn[256];     // directory name
	char *dir;          // list of files in dir
	int dirc;  // total files in dir
	int dirx;  // position of dirname
	int hdir;  // highlighted_dir
	int nw[2];
	int ne[2];
	int sw[2];
	int se[2]; // max xy positions
} Panel;

/* function declarations */
static void print_tb(const char*, int, int, uint16_t, uint16_t);
static void printf_tb(int, int, uint16_t, uint16_t, const char*, ...);
static int set_panels(Panel*, Panel*, char*);
static int listdir(Panel*);
static void print_status(const char*, ...);
static void press(struct tb_event*, Panel*);
static void draw_frame(void);
static int start(void);

/* global variables */
static int current_panel = 0;

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

static int
set_panels(Panel *panel_l, Panel *panel_r, char *home)
{
	int height, width;
	width = tb_width(); // width of the terminal screen
	height = tb_height(); // height of the terminal screen
	char *root = "/";

	strcpy(panel_l->dirn, home);
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

	strcpy(panel_r->dirn, root);
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

	return 0;

}

static int
listdir(Panel *cpanel)
{
	int  y, i, n;
	struct dirent **namelist;
	uint16_t bg, fg;

	y = 1;
	n = scandir(cpanel->dirn, &namelist, 0, alphasort);

	if (n < 0)
		die("++%s++ scandir:", cpanel->dirn);
	else {
		for (i = 0; i < n; i++) {
			if (!(strcmp(namelist[i]->d_name, ".") == 0  ||
				strcmp(namelist[i]->d_name, "..") == 0 )) {
				bg = file_nor_b;
				fg = file_nor_f;
				if (y == cpanel->hdir) {
					bg = file_hig_b;
					fg = file_hig_f;
				}

				printf_tb(cpanel->dirx, y, fg,
					bg," %s ",
					namelist[i]->d_name);
				free(namelist[i]);
				y++;
			}
		}
	}

	free(namelist);
	cpanel->dirc = n - 2;
	return 0;
}

static void
clear_status(void)
{
	int i, width, height;
	width = tb_width();
	height = tb_height();
	for (i = 1; i < (width-1)/2 ; i++) {
		tb_change_cell(i, height-2, 0x0000, frame_fcol, frame_bcol);
	}
}

static void
print_status(const char *fmt, ...)
{
	int height, width;
	width = tb_width(); // width of the terminal screen
	height = tb_height(); // height of the terminal screen

	char buf[256];
	va_list vl;
	va_start(vl, fmt);
	vsnprintf(buf, sizeof(buf), fmt, vl);
	va_end(vl);
	clear_status();
	print_tb(buf, 2, height-2, TB_DEFAULT, TB_DEFAULT);

}

static void
press(struct tb_event *ev, Panel *cpanel)
{
	switch (ev->ch) {
	case 'j':
		if (cpanel->hdir < cpanel->dirc) {
			cpanel->hdir++;
			listdir(cpanel);
		}
		break;
	case 'k':
		if (cpanel->hdir > 1) {
			cpanel->hdir--;
			listdir(cpanel);
			print_status("%i", cpanel->hdir);
		}
		break;
	case 'h':
		print_status("go to parent");
		break;
	case 'l':
// 		listdir(current_dir, 'l');
		print_status("open dir or file");
		break;
	case 'g':
		cpanel->hdir = 1;
		listdir(cpanel);
		print_status("go to top");
		break;
	case last_dir:
		cpanel->hdir = cpanel->dirc;
		listdir(cpanel);
		print_status("go to bottom");
		break;
	case 'M':
		cpanel->hdir = (cpanel->dirc/2);
		listdir(cpanel);
		print_status("go to middel");
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
	int ret, mode, support_256, support_n, init_height, init_width;
	struct tb_event ev;
	Panel panel_r, panel_l;
	char *home;
	char *editor;

	home = getenv("HOME");
	editor = getenv("EDITOR");

	if (home == NULL)
		home = "/mnt/data/dev/sandbox/calcurse";

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
	set_panels(&panel_l, &panel_r, home);
	listdir(&panel_l);
	listdir(&panel_r);
	print_status("%i/%i", panel_l.hdir, panel_l.dirc);
	tb_present();

	while (tb_poll_event(&ev) != 0) {
		switch (ev.type) {
		case TB_EVENT_KEY:
			if (ev.ch == quit_key) {
				tb_shutdown();
				return 0;
			} else if (ev.key == TB_KEY_SPACE) {
				current_panel = !current_panel;
			}

			if (current_panel == 0) {
				press(&ev, &panel_l);
				print_status("%i/%i", panel_l.hdir, panel_l.dirc);

			} else if (current_panel == 1) {
				press(&ev, &panel_r);
				print_status("%i/%i", panel_r.hdir, panel_r.dirc);
			}

			tb_present();
			break;

		case TB_EVENT_RESIZE:
			tb_clear();
			draw_frame();
			(void)set_panels(&panel_l, &panel_r, home);
			(void)listdir(&panel_l);
			(void)listdir(&panel_r);
			if (current_panel == 0) {
				print_status("%i/%i", panel_l.hdir, panel_l.dirc);
			} else if (current_panel == 1) {
				print_status("%i/%i", panel_r.hdir, panel_r.dirc);
			}
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
