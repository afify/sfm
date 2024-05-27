#ifndef CONFIG_H
#define CONFIG_H

#include <stdlib.h>

#include "sfm.h"

/* color pairs */
static const ColorPair color_blk = { 95, 0, NORM };
static const ColorPair color_brlnk = { 0, 9, NORM };
static const ColorPair color_chr = { 94, 0, NORM };
static const ColorPair color_dir = { 31, 0, NORM };
static const ColorPair color_err = { 124, 0, BLINK };
static const ColorPair color_exec = { 91, 0, NORM };
static const ColorPair color_file = { 243, 0, NORM };
static const ColorPair color_frame = { 233, 124, NORM };
static const ColorPair color_ifo = { 93, 0, NORM };
static const ColorPair color_lnk = { 96, 0, NORM };
static const ColorPair color_other = { 90, 0, NORM };
static const ColorPair color_panell = { 166, 233, BOLD };
static const ColorPair color_panelr = { 5, 233, BOLD };
static const ColorPair color_prompt = { 33, 0, NORM };
static const ColorPair color_search = { 255, 0, NORM };
static const ColorPair color_sock = { 92, 0, NORM };
static const ColorPair color_status = { 243, 0, NORM };
static const ColorPair color_warn = { 220, 0, NORM };

/* normal keys */
static Key nkeys[] = {
	/* key                 function       arg */
	{ 'j',                 move_cursor,    { .i = +1 } },
	{ XK_DOWN,             move_cursor,    { .i = +1 } },
	{ 'k',                 move_cursor,    { .i = -1 } },
	{ XK_UP,               move_cursor,    { .i = -1 } },
	{ XK_CTRL('u'),        move_cursor,    { .i = -5 } },
	{ XK_CTRL('d'),        move_cursor,    { .i = +5 } },
	{ 'h',                 cd_to_parent,   { 0 }       },
	{ 'q',                 quit,           { 0 }       },
	{ 'G',                 move_bottom,    { 0 }       },
	{ 'g',                 move_top,       { 0 }       },

	{ XK_SPACE, switch_pane, { 0 } },
};

static int show_dotfiles = 1;
#endif // CONFIG_H
