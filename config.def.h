/* See LICENSE file for copyright and license details.*/

#ifndef CONFIG_H
#define CONFIG_H

#include "termbox.h"

/* colors                     fg,          bg */
static const Cpair cdir    = { 33,         TB_DEFAULT };
static const Cpair cerr    = { 124,        TB_DEFAULT };
static const Cpair cexec   = { 2,          TB_DEFAULT };
static const Cpair cfile   = { TB_DEFAULT, TB_DEFAULT };
static const Cpair cframe  = { TB_DEFAULT, TB_DEFAULT };
static const Cpair cother  = { 3,          TB_DEFAULT };
static const Cpair cpanell = { 166,        TB_DEFAULT };
static const Cpair cpanelr = { 5,          TB_DEFAULT };
static const Cpair cprompt = { 33,         TB_DEFAULT };
static const Cpair csearch = { 255,        TB_DEFAULT };
static const Cpair cstatus = { TB_DEFAULT, TB_DEFAULT };

/* bookmarks */
static Bookmark bmarks[] = {
	{'\\', "/root"},
	{'e', "/etc"},
	{'u', "/usr/local/bin"},
};

/* openwith */
static const char *images[] = { "bmp", "jpg", "jpeg", "png", "gif", "xpm" };
static const char *web[] = { "htm", "html" };
static const char *pdf[] = { "epub", "pdf" };
static const char *arts[] = { "xcf" };
static const char *videos[] = { "avi", "flv", "wav", "webm", "wma", "wmv",
	"m2v", "m4a", "m4v", "mkv", "mov", "mp3", "mp4", "mpeg", "mpg" };
static const char *documents[] = { "odt", "doc", "docx", "xls", "xlsx", "odp",
	"ods", "pptx", "odg" };

static Rule rules[] = {
	{"mpv",         videos,    LEN(videos)    },
	{"sxiv",        images,    LEN(images)    },
	{"firefox",     web,       LEN(web)       },
	{"mupdf",       pdf,       LEN(pdf)       },
	{"libreoffice", documents, LEN(documents) },
	{"gimp",        arts,      LEN(arts)      },
};

static Key keys[] = {
	{ {.ch = 'j'},               mvdwn },
	{ {.ch = 'k'},               mvup },
	{ {.ch = 'l'},               mvfor },
	{ {.ch = 'h'},               mvbk },
	{ {.ch = 'g'},               mvtop },
	{ {.ch = 'G'},               mvbtm },
	{ {.ch = 'M'},               mvmid },
	{ {.key = TB_KEY_CTRL_U},    scrup },
	{ {.key = TB_KEY_CTRL_D},    scrdwn },
	{ {.ch = 'n'},               crnf },
	{ {.ch = 'N'},               crnd },
	{ {.ch = 'D'},               delfd },
	{ {.ch = 'x'},               calcdir },
	{ {.ch = '/'},               filter },
	{ {.ch = 'q'},               quit },
	{ {.key = TB_KEY_SPACE},     switch_pane },
};

static const mode_t new_dir_perm = 0755;

/* scroll */
static const size_t scrmv = 10; /* ctrl+u, ctrl+d movement */
static const size_t scrsp = 3;  /* space before scroll */

/* statusbar */
static const int show_ug   = 1;
static const int show_perm = 1;
static const int show_dt   = 1;
static const int show_size = 1;
static const char dtfmt[] = "%F %R"; /* date time format */

/* unicode chars */
static const uint32_t u_cne = 0x2510;
static const uint32_t u_cnw = 0x250C;
static const uint32_t u_cse = 0x2518;
static const uint32_t u_csw = 0x2514;
static const uint32_t u_hl  = 0x2500;
static const uint32_t u_mn  = 0x252C;
static const uint32_t u_ms  = 0x2534;
static const uint32_t u_vl  = 0x2502;

#endif /* CONFIG_H */
