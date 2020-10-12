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
};

/* software */
static const char *mpv[]          = { "mpv", "--fullscreen", NULL };
static const char *sxiv[]         = { "sxiv", NULL };
static const char *surf[]         = { "surf", NULL };
static const char *mupdf[]        = { "mupdf", "-I", NULL };
static const char *libreoffice[]  = { "libreoffice", NULL };
static const char *gimp[]         = { "gimp", NULL };
static const char *r2[]           = { "r2", "-c", "vv", NULL };
static const char *binary[]       = { "r2", "-d", "-c", "vv", NULL };

/* extension */
static const char *images[]    = { "bmp", "jpg", "jpeg", "png", "gif", "xpm" };
static const char *web[]       = { "htm", "html" };
static const char *pdf[]       = { "epub", "pdf" };
static const char *arts[]      = { "xcf" };
static const char *obj[]       = { "o", "a", "so" };
static const char *videos[]    = { "avi", "flv", "wav", "webm", "wma", "wmv",
				   "m2v", "m4a", "m4v", "mkv", "mov", "mp3",
				   "mp4", "mpeg", "mpg" };
static const char *documents[] = { "odt", "doc", "docx", "xls", "xlsx", "odp",
				   "ods", "pptx", "odg" };

static Rule rules[] = {
	{videos,    LEN(videos),    mpv},
	{images,    LEN(images),    sxiv},
	{web,       LEN(web),       surf},
	{pdf,       LEN(pdf),       mupdf},
	{documents, LEN(documents), libreoffice},
	{arts,      LEN(arts),      gimp},
	{obj,       LEN(obj),       r2},
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

static const mode_t ndir_perm = S_IRWXU | S_IRWXG |S_IROTH | S_IXOTH;
static const mode_t nf_perm   = S_IRUSR | S_IWUSR;

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
#define u_hl  0x2500 /* ─ */
#define u_vl  0x2502 /* │ */
#define u_cnw 0x250C /* ┌ */
#define u_cne 0x2510 /* ┐ */
#define u_csw 0x2514 /* └ */
#define u_cse 0x2518 /* ┘ */
#define u_mn  0x252C /* ┬ */
#define u_ms  0x2534 /* ┴ */

#endif /* CONFIG_H */
