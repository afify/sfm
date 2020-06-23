/* See LICENSE file for copyright and license details.*/

#ifndef CONFIG_H
#define CONFIG_H

#include "termbox.h"

/* normal mode keybind */
const char up           = 'j';
const char down         = 'k';
const char open_dir     = 'l';
const char back         = 'h';
const char last_dir     = 'G';
const char top          = 'g';
const char switch_pane  = TB_KEY_SPACE;
const uint32_t q        = 0x0071;

const char copy_p  = 'c'; // copy to other pane
const char move_p  = 'm'; // move to other pane
const char yank    = 'y';
const char past    = 'p'; // past yanked
const char move    = 'P'; // move yanked
const char delete  = 'D';

/* unicode chars */
uint32_t u_vl = 0x2502;
uint32_t u_hl = 0x2500;

uint32_t u_cnw = 0x250C;
uint32_t u_cne = 0x2510;
uint32_t u_csw = 0x2514;
uint32_t u_cse = 0x2518;

uint32_t u_mn = 0x252C;
uint32_t u_ms = 0x2534;

/* colors */
uint16_t frame_fcol = TB_DEFAULT;
uint16_t frame_bcol = TB_DEFAULT;

uint16_t status_fcol = TB_DEFAULT;
uint16_t status_bcol = 234;

uint16_t pane1_dir_f = 166;
uint16_t pane2_dir_f = 27;
uint16_t pane1_dir_b = TB_DEFAULT;
uint16_t pane2_dir_b = TB_DEFAULT;

uint16_t dir_nor_f = 33;
uint16_t dir_nor_b = TB_DEFAULT;
uint16_t dir_hig_f = 23;
uint16_t dir_hig_b = 233;

uint16_t file_nor_f = TB_DEFAULT;
uint16_t file_nor_b = TB_DEFAULT;
uint16_t file_hig_f = 2;
uint16_t file_hig_b = 233;

uint16_t other_nor_f = 3;
uint16_t other_nor_b = TB_DEFAULT;
uint16_t other_hig_f = 214;
uint16_t other_hig_b = 233;


/* openwith */
char images[] = "sxiv";
char videos[] = "mpv";
char audio[]  = "mpv";
char pdf[]    = "mupdf";
char html[]   = "firefox";

#endif /* CONFIG_H */
