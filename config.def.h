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

static const Cpair cframe  = { 0,   9, NORM };  // TODO change
static const Cpair cpanell = { 166, 7, BOLD };
static const Cpair cpanelr = { 5,   8, BOLD };
static const Cpair cerr    = { 124, 0,   NORM };
static const Cpair cwarn   = { 220, 0,   NORM };
static const Cpair cprompt = { 33,  98,   NORM };
static const Cpair csearch = { 255, 0,   NORM };
static const Cpair cstatus = { 243, 222, NORM };

/* commands */
#if defined(__linux__)
#define CHFLAG "chattr"
#else
#define CHFLAG "chflags"
#endif
static const char *rm_cmd[]      = { "rm", "-rf" }; /* delete */
static const char *cp_cmd[]      = { "cp", "-r" }; /* copy */
static const char *chown_cmd[]   = { "chown", "-R" }; /* change file owner and group */
static const char *chmod_cmd[]   = { "chmod" }; /* change file mode bits */
static const char *chflags_cmd[] = { CHFLAG }; /* change file flags */
static const char *mv_cmd[]      = { "mv" }; /* move */
static const char delconf[]      = "yes";

static const size_t rm_cmd_len      = LEN(rm_cmd);
static const size_t cp_cmd_len      = LEN(cp_cmd);
static const size_t chown_cmd_len   = LEN(chown_cmd);
static const size_t chmod_cmd_len   = LEN(chmod_cmd);
static const size_t chflags_cmd_len = LEN(chflags_cmd);
static const size_t mv_cmd_len      = LEN(mv_cmd);
static const size_t delconf_len     = LEN(delconf);

/* bookmarks */
static const char root[]   = "/";

/* software */
static const char *mpv[]          = { "mpv", "--fullscreen" };
static const char *sxiv[]         = { "sxiv" };
static const char *mupdf[]        = { "mupdf" };
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
	/* key                 function      arg */
	{ 'j',                 mv_ver,       {.i = -1}       },
	{ XK_DOWN,             mv_ver,       {.i = -1}       },
	{ 'k',                 mv_ver,       {.i = +1}       },
	{ XK_UP,               mv_ver,       {.i = +1}       },
	{ XK_CTRL('u'),        mv_ver,       {.i = +3}       },
	{ XK_CTRL('d'),        mv_ver,       {.i = -3}       },
	{ 'l',                 mvfwd,        {0}             },
	{ XK_RIGHT,            mvfwd,        {0}             },
	{ 'h',                 mvbk,         {0}             },
	{ XK_LEFT,             mvbk,         {0}             },
	{ 'g',                 mvtop,        {0}             },
	{ 'G',                 mvbtm,        {0}             },
	{ 'n',                 crnf,         {0}             },
	{ 'N',                 crnd,         {0}             },
	{ 'd',                 delent,       {0}             },
	{ 'D',                 dupl,         {0}             },
	{ 'x',                 calcdir,      {0}             },
	{ 'q',                 quit,         {0}             },
	{ 'v',                 start_vmode,  {0}             },
	{ 'y',                 yank,         {0}             },
	{ 'p',                 paste,        {0}             },
	{ 'P',                 selmv,        {0}             },
	{ 'c',                 start_change, {0}             },
	{ 'b',                 opnsh,        {0}             },
	{ XK_SPACE,            switch_pane,  {0}             },
	{ XK_CTRL('r'),        refresh,      {0}             },
	{ '\\',                bkmrk,        {.v = root}     },
	{ '.',                 toggle_df,    {0}             },
	{ '/',                 start_filter, {0}             },
};

/* change keys */
static Key ckeys[] = {
	/* key                 function          arg */
	{ 'w',                 rname,            {0}            },
	{ 'o',                 chngo,            {0}            },
	{ 'm',                 chngm,            {0}            },
	{ 'f',                 chngf,            {0}            },
	{ 'q',                 exit_change,      {0}            },
	{ 'c',                 exit_change,      {0}            },
	{ XK_ESC,              exit_change,      {0}            },
};

/* visual keys */
static Key vkeys[] = {
	/* key                 function         arg */
	{ 'j',                 seldwn,          {.i = -1}      },
	{ XK_DOWN,             seldwn,          {.i = -1}      },
	{ 'k',                 selup,           {.i = +1}      },
	{ XK_UP,               selup,           {.i = +1}      },
	{ 'a',                 selall,          {0}            },
	{ 'y',                 selynk,          {0}            },
	{ 'd',                 seldel,          {.v = delconf} },
	{ 'q',                 exit_vmode,      {0}            },
	{ 'v',                 exit_vmode,      {0}            },
	{ XK_ESC,              exit_vmode,      {0}            },
};

static const size_t nkeyslen = LEN(nkeys);
static const size_t vkeyslen = LEN(vkeys);
static const size_t ckeyslen = LEN(ckeys);

/* permissions */
static const mode_t ndir_perm = S_IRWXU;
static const mode_t nf_perm   = S_IRUSR | S_IWUSR;

/* dotfiles */
static int show_dotfiles = 1;

/* statusbar */
static const char dtfmt[] = "%F %R"; /* date time format */

#endif /* CONFIG_H */
