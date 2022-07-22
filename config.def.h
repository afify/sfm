/* See LICENSE file for copyright and  license details. */

#ifndef CONFIG_H
#define CONFIG_H

/* colors                      fg,  bg   att*/
static const Cpair cdir    = { 31,  0,   NORM };
static const Cpair cfile   = { 243, 0,   NORM };
static const Cpair clnk    = { 96,  0,   NORM };
static const Cpair cblk    = { 95,  0,   NORM };
static const Cpair cchr    = { 94,  0,   NORM };
static const Cpair cifo    = { 93,  0,   NORM };
static const Cpair csock   = { 92,  0,   NORM };
static const Cpair cexec   = { 91,  0,   NORM };
static const Cpair cother  = { 90,  0,   NORM };

static const Cpair cframe  = { 233, 5,   NORM };
static const Cpair cpanell = { 166, 233, BOLD };
static const Cpair cpanelr = { 5,   233, BOLD };
static const Cpair cerr    = { 124, 0,   NORM };
static const Cpair cprompt = { 33,  0,   NORM };
static const Cpair csearch = { 255, 0,   NORM };
static const Cpair cstatus = { 243, 233, NORM };

/* statusbar */
static const char dtfmt[] = "%F %R"; /* date time format */

/* dotfiles */
static int show_dotfiles = 1;

#endif /* CONFIG_H */
