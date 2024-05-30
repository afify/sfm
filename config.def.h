#ifndef CONFIG_H
#define CONFIG_H

#include <stdlib.h>

#include "sfm.h"

/* color pairs                          fg   bg   attr*/
static const ColorPair color_blk =    { 95,  0,   NORM };
static const ColorPair color_brlnk =  { 0,   9,   NORM };
static const ColorPair color_chr =    { 94,  0,   NORM };
static const ColorPair color_dir =    { 31,  0,   NORM };
static const ColorPair color_err =    { 124, 0,   BOLD };
static const ColorPair color_exec =   { 91,  0,   NORM };
static const ColorPair color_file =   { 243, 0,   NORM };
static const ColorPair color_frame =  { 233, 124, NORM };
static const ColorPair color_ifo =    { 93,  0,   NORM };
static const ColorPair color_lnk =    { 96,  0,   NORM };
static const ColorPair color_other =  { 90,  0,   NORM };
static const ColorPair color_panell = { 166, 233, BOLD };
static const ColorPair color_panelr = { 5,   233, BOLD };
static const ColorPair color_prompt = { 33,  0,   NORM };
static const ColorPair color_search = { 255, 0,   NORM };
static const ColorPair color_sock =   { 92,  0,   NORM };
static const ColorPair color_status = { 243, 0,   NORM };
static const ColorPair color_warn =   { 220, 0,   NORM };

/* software */
static const char *mpv[]          = { "mpv", "--fullscreen" };
static const char *sxiv[]         = { "sxiv" };
static const char *mupdf[]        = { "mupdf", "-I" };
static const char *libreoffice[]  = { "libreoffice" };
static const char *gimp[]         = { "gimp" };
static const char *r2[]           = { "r2", "-c", "vv" };

/* extensions*/
static const char *images[]    = { "bmp", "jpg", "jpeg", "png", "gif", "webp", "xpm" };
static const char *pdf[]       = { "epub", "pdf" };
static const char *arts[]      = { "xcf" };
static const char *obj[]       = { "o", "a", "so" };
static const char *videos[]    = { "avi", "flv", "wav", "webm", "wma", "wmv",
				   "m2v", "m4a", "m4v", "mkv", "mov", "mp3",
				   "mp4", "mpeg", "mpg" };
static const char *documents[] = { "odt", "doc", "docx", "xls", "xlsx", "odp",
				   "ods", "pptx", "odg" };

static Rule rules[] = {
	{videos,    LEN(videos),    mpv,         LEN(mpv)         },
	{images,    LEN(images),    sxiv,        LEN(sxiv)        },
	{pdf,       LEN(pdf),       mupdf,       LEN(mupdf)       },
	{documents, LEN(documents), libreoffice, LEN(libreoffice) },
	{arts,      LEN(arts),      gimp,        LEN(gimp)        },
	{obj,       LEN(obj),       r2,          LEN(r2)          },
};

/* normal keys */
static Key nkeys[] = {
	/* key                 function          arg */
	{ 'j',                 move_cursor,      { .i = +1 } },
	{ XK_DOWN,             move_cursor,      { .i = +1 } },
	{ 'k',                 move_cursor,      { .i = -1 } },
	{ XK_UP,               move_cursor,      { .i = -1 } },
	{ XK_CTRL('u'),        move_cursor,      { .i = -5 } },
	{ XK_CTRL('d'),        move_cursor,      { .i = +5 } },
	{ 'l',                 open_entry,       { 0 }       },
	{ 'h',                 cd_to_parent,     { 0 }       },
	{ 'q',                 quit,             { 0 }       },
	{ 'G',                 move_bottom,      { 0 }       },
	{ 'g',                 move_top,         { 0 }       },
	{ XK_SPACE,            switch_pane,      { 0 }       },
	{ '.',                 toggle_dotfiles,  { 0 }       },
	{ XK_CTRL('r'),        refresh,          { 0 }       },
};

static const size_t nkeyslen = LEN(nkeys);

static int show_dotfiles = 1;
static char default_home[] = "/";
static char default_editor[] = "vi";
static char default_shell[] = "/bin/sh";
#endif // CONFIG_H
