/* See LICENSE file for copyright and license details.*/

#ifndef CONFIG_H
#define CONFIG_H

#include "termbox.h"

/* colors */
uint16_t dir_b    = TB_DEFAULT;
uint16_t dir_f    = 33;
uint16_t file_b   = TB_DEFAULT;
uint16_t file_f   = TB_DEFAULT;
uint16_t frame_b  = TB_DEFAULT;
uint16_t frame_f  = TB_DEFAULT;
uint16_t other_b  = TB_DEFAULT;
uint16_t other_f  = 3;
uint16_t pane_l_b = TB_DEFAULT;
uint16_t pane_l_f = 166;
uint16_t pane_r_b = TB_DEFAULT;
uint16_t pane_r_f = 5;
uint16_t status_b = 234;
uint16_t status_f = TB_DEFAULT;

/* openwith */
char audio[]  = "mpv";
char html[]   = "firefox";
char images[] = "sxiv";
char pdf[]    = "mupdf";
char videos[] = "mpv";

/* unicode chars */
uint32_t u_cne = 0x2510;
uint32_t u_cnw = 0x250C;
uint32_t u_cse = 0x2518;
uint32_t u_csw = 0x2514;
uint32_t u_hl  = 0x2500;
uint32_t u_mn  = 0x252C;
uint32_t u_ms  = 0x2534;
uint32_t u_vl  = 0x2502;

#endif /* CONFIG_H */
