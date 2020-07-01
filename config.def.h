/* See LICENSE file for copyright and license details.*/

#ifndef CONFIG_H
#define CONFIG_H

#include "termbox.h"

/* colors */
static const uint16_t dir_b    = TB_DEFAULT;
static const uint16_t dir_f    = 33;
static const uint16_t file_b   = TB_DEFAULT;
static const uint16_t file_f   = TB_DEFAULT;
static const uint16_t frame_b  = TB_DEFAULT;
static const uint16_t frame_f  = TB_DEFAULT;
static const uint16_t other_b  = TB_DEFAULT;
static const uint16_t other_f  = 3;
static const uint16_t pane_l_b = TB_DEFAULT;
static const uint16_t pane_l_f = 166;
static const uint16_t pane_r_b = TB_DEFAULT;
static const uint16_t pane_r_f = 5;
static const uint16_t status_b = 234;
static const uint16_t status_f = TB_DEFAULT;

/* bookmarks */
static Bookmark bmarks[] = {
	{'\\', "/root"},
	{'e', "/etc"},
	{'u', "/usr/local/bin"},
};

/* openwith */
static char audio[]  = "mpv";
static char html[]   = "firefox";
static char images[] = "sxiv";
static char pdf[]    = "mupdf";
static char videos[] = "mpv";

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
