/* See LICENSE file for copyright and license details.*/

#ifndef CONFIG_H
#define CONFIG_H

/* colors                      fg,  bg */
static const Cpair cdir    = { 33,  0 };
static const Cpair cerr    = { 124, 0 };
static const Cpair cexec   = { 2,   0 };
static const Cpair cfile   = { 243, 0 };
static const Cpair cframe  = { 233, 233 };
static const Cpair cother  = { 3,   0 };
static const Cpair cpanell = { 166, 233 };
static const Cpair cpanelr = { 5,   233 };
static const Cpair cprompt = { 33,  0 };
static const Cpair csearch = { 255, 0 };
static const Cpair cstatus = { 243, 0 };

/* commands */
static const char *rm_cmd[] = { "rm", "-rf" }; /* delete */
static const char *cp_cmd[] = { "cp", "-r" }; /* copy */
static const char *mv_cmd[] = { "mv" }; /* move */

static const size_t rm_cmd_len = LEN(rm_cmd);
static const size_t cp_cmd_len = LEN(cp_cmd);
static const size_t mv_cmd_len = LEN(mv_cmd);

/* bookmarks */
static Bookmark bmarks[] = {
	{'\\', "/root"},
};

/* software */
static const char *mpv[]          = { "mpv", "--fullscreen" };
static const char *sxiv[]         = { "sxiv" };
static const char *surf[]         = { "surf" };
static const char *mupdf[]        = { "mupdf", "-I" };
static const char *libreoffice[]  = { "libreoffice" };
static const char *gimp[]         = { "gimp" };
static const char *r2[]           = { "r2", "-c", "vv" };

/* extensions*/
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
	{videos,    LEN(videos),    mpv,         LEN(mpv)         },
	{images,    LEN(images),    sxiv,        LEN(sxiv)        },
	{web,       LEN(web),       surf,        LEN(surf)        },
	{pdf,       LEN(pdf),       mupdf,       LEN(mupdf)       },
	{documents, LEN(documents), libreoffice, LEN(libreoffice) },
	{arts,      LEN(arts),      gimp,        LEN(gimp)        },
	{obj,       LEN(obj),       r2,          LEN(r2)          },
};

/* normal keys */
static Key nkeys[] = {
	/* keyval                      function         arg */
	{ {.ch = 'j'},                 mvdwn,           {.i = 0}     },
	{ {.key = TB_KEY_ARROW_DOWN},  mvdwn,           {.i = 0}     },
	{ {.ch = 'k'},                 mvup,            {.i = 0}     },
	{ {.key = TB_KEY_ARROW_UP},    mvup,            {.i = 0}     },
	{ {.ch = 'l'},                 mvfwd,           {.i = 0}     },
	{ {.key = TB_KEY_ARROW_RIGHT}, mvfwd,           {.i = 0}     },
	{ {.ch = 'h'},                 mvbk,            {.i = 0}     },
	{ {.key = TB_KEY_ARROW_LEFT},  mvbk,            {.i = 0}     },
	{ {.ch = 'g'},                 mvtop,           {.i = 0}     },
	{ {.ch = 'G'},                 mvbtm,           {.i = 0}     },
	{ {.ch = 'M'},                 mvmid,           {.i = 0}     },
	{ {.key = TB_KEY_CTRL_U},      scrup,           {.i = 0}     },
	{ {.key = TB_KEY_CTRL_D},      scrdwn,          {.i = 0}     },
	{ {.ch = 'n'},                 crnf,            {0}          },
	{ {.ch = 'N'},                 crnd,            {0}          },
	{ {.ch = 'd'},                 delent,          {0}          },
	{ {.ch = 'x'},                 calcdir,         {0}          },
	{ {.ch = '/'},                 start_filter,    {0}          },
	{ {.ch = 'q'},                 quit,            {0}          },
	{ {.ch = 'v'},                 start_vmode,     {0}          },
	{ {.ch = 'y'},                 yank,            {0}          },
	{ {.ch = 'p'},                 paste,           {0}          },
	{ {.ch = 'P'},                 selmv,           {0}          },
	{ {.ch = 'c'},                 rname,           {0}          },
	{ {.key = TB_KEY_SPACE},       switch_pane,     {0}          },
};

/* visual keys */
static Key vkeys[] = {
	/* keyval                      function         arg */
	{ {.ch = 'j'},                 seldwn,          {.i = 0}     },
	{ {.key = TB_KEY_ARROW_DOWN},  seldwn,          {.i = 0}     },
	{ {.ch = 'k'},                 selup,           {.i = 0}     },
	{ {.key = TB_KEY_ARROW_UP},    selup,           {.i = 0}     },
	{ {.ch = 'a'},                 selall,          {.i = 0}     },
	{ {.ch = 'y'},                 selynk,          {.i = 0}     },
	{ {.ch = 'd'},                 seldel,          {.i = 0}     },
	{ {.ch = 'q'},                 exit_vmode,      {.i = 0}     },
	{ {.ch = 'v'},                 exit_vmode,      {.i = 0}     },
	{ {.key = TB_KEY_ESC},         exit_vmode,      {.i = 0}     },
};

static const size_t nkeyslen = LEN(nkeys);
static const size_t vkeyslen = LEN(vkeys);

/* permissions */
static const mode_t ndir_perm = S_IRWXU;
static const mode_t nf_perm   = S_IRUSR | S_IWUSR;

/* scroll */
static const int scrmv = 10; /* ctrl+u, ctrl+d movement */
static const int scrsp = 3;  /* space before scroll */

/* statusbar */
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
