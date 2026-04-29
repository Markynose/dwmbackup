/* dmenu config — pink gradient theme */

static int topbar            = 1;   /* 1 = top, 0 = bottom */
static int centered          = 0;   /* 1 = centered on screen */
static int min_width         = 500;
static const char *fonts[]   = { "monospace:size=11" };
static const char *prompt    = NULL;

/* pink gradient — dark bg → hot pink accent → white text */
static const char *colors[SchemeLast][2] = {
	/*               fg          bg        */
	[SchemeNorm] = { "#f0b8d0",  "#1e1224" }, /* normal item:   soft pink on dark */
	[SchemeSel]  = { "#ffffff",  "#c9479b" }, /* selected item: white on hot pink */
	[SchemeOut]  = { "#1e1224",  "#ff79c6" }, /* marked output: dark on neon pink */
};

/* characters not considered part of a word — for word-delete (ctrl+w) */
static const char worddelimiters[] = " <>()[]{}/\\";

/* -l option; if nonzero, dmenu uses vertical list of that many lines */
static unsigned int lines = 0;

/* -h option; minimum height of a menu line */
static unsigned int lineheight = 0;

/* -x, -y, -w — position & width overrides (0 = auto) */
static int dmx = 0;
static int dmy = 0;
static unsigned int dmw = 0;
