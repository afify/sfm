/* See LICENSE file for copyright and license details.*/

#ifndef CONFIG_H
#define CONFIG_H

#include "termbox.h"

/* colors */
static const uint16_t dir_b     = TB_DEFAULT;
static const uint16_t dir_f     = 33;
static const uint16_t file_b    = TB_DEFAULT;
static const uint16_t file_f    = TB_DEFAULT;
static const uint16_t frame_b   = TB_DEFAULT;
static const uint16_t frame_f   = TB_DEFAULT;
static const uint16_t other_b   = TB_DEFAULT;
static const uint16_t other_f   = 3;
static const uint16_t pane_l_b  = TB_DEFAULT;
static const uint16_t pane_l_f  = 166;
static const uint16_t pane_r_b  = TB_DEFAULT;
static const uint16_t pane_r_f  = 5;
static const uint16_t search_b  = 166;
static const uint16_t search_f  = 255;
static const uint16_t status_b  = 234;
static const uint16_t status_f  = TB_DEFAULT;
static const uint16_t sprompt_f = 33;
static const uint16_t sprompt_b = 234;
static const uint16_t serr_f    = 124;
static const uint16_t serr_b    = 234;
static const uint16_t exec_f    = 2;

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
static const char *videos[] = { "avi", "flv",
	"m2v", "m4a", "m4v", "mkv", "mov", "mp3", "mp4", "mpeg", "mpg",
	"wav", "webm", "wma", "wmv" };
static const char *documents[] = { "odt", "doc", "docx", "xls", "xlsx", "odp",
	"ods", "pptx", "odg" };
static const char *arts[] = { "xcf" };

static Rule rules[] = {
	{"mpv",         videos,    LEN(videos)    },
	{"sxiv",        images,    LEN(images)    },
	{"firefox",     web,       LEN(web)       },
	{"mupdf",       pdf,       LEN(pdf)       },
	{"libreoffice", documents, LEN(documents) },
	{"gimp",        arts,      LEN(arts)      },
};

static const size_t move_ud = 10; /* ctrl U, ctrl D movement */
static const mode_t new_dir_perm = 0755;
static const char dt_fmt[] = "%d/%m %I:%M%p"; /* date time format */

/* statusbar */
static const int show_ug     = 1;
static const int show_perm   = 1;
static const int show_dt     = 1;
static const int show_size   = 1;

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
