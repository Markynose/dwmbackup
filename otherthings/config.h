/* See LICENSE file for copyright and license details. */
#include <X11/XF86keysym.h>

/* appearance */
static const unsigned int borderpx  = 2;        /* border pixel of windows */
static const unsigned int snap      = 32;       /* snap pixel */
static const int showbar            = 1;        /* 0 means no bar */
static const int topbar             = 1;        /* 0 means bottom bar */
static const char *fonts[]          = { "monospace:size=10" };
static const char dmenufont[]       = "monospace:size=10";

/* pink colorscheme */
static const char col_bg[]          = "#1e1224";  /* dark purple-black */
static const char col_bg2[]         = "#3d2b45";  /* inactive border / unfocused */
static const char col_fg[]          = "#f0b8d0";  /* soft pink foreground */
static const char col_fg2[]         = "#ffffff";  /* bright white for selected text */
static const char col_pink[]        = "#c9479b";  /* hot pink accent */

static const char *colors[][3]      = {
	/*               fg         bg         border   */
	[SchemeNorm] = { col_fg,   col_bg,    col_bg2  },
	[SchemeSel]  = { col_fg2,  col_pink,  col_pink },
};

/* tagging */
static const char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

static const Rule rules[] = {
	/* xprop(1):
	 *	WM_CLASS(STRING) = instance, class
	 *	WM_NAME(STRING) = title
	 */
	/* class      instance    title       tags mask     isfloating   monitor */
	{ "Gimp",     NULL,       NULL,       0,            1,           -1 },
	{ "Firefox",  NULL,       NULL,       1 << 8,       0,           -1 },
};

/* layout(s) */
static const float mfact     = 0.55; /* factor of master area size [0.05..0.95] */
static const int nmaster     = 1;    /* number of clients in master area */
static const int resizehints = 1;    /* 1 means respect size hints in tiled resizals */
static const int lockfullscreen = 1; /* 1 will force focus on the fullscreen window */
static const int refreshrate = 120;  /* refresh rate (per second) for client move/resize */

static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[@]",      spiral },  /* first entry is default */
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ "[M]",      monocle },
};

/* key definitions — Super (Win key) as modifier, Hyprland-style */
#define MODKEY Mod4Mask
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      view,           {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask,           KEY,      toggleview,     {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask,             KEY,      tag,            {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask|ShiftMask, KEY,      toggletag,      {.ui = 1 << TAG} },

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
static char dmenumon[2] = "0";
static const char *dmenucmd[]    = { "chilauncher", NULL };
static const char *termcmd[]     = { "st", NULL };
static const char scratchpadname[] = "scratchpad";
static const char *scratchpadcmd[] = { "st", "-t", scratchpadname, "-g", "100x30", NULL };
static const char *browsercmd[]  = { "firefox", NULL };
static const char *keyscmd[]     = { "st", "-t", "chikeys", "-e", "sh", "-c", "chikeys | less", NULL };
static const char *calccmd[]     = { "chicalc", NULL };
static const char *advcalccmd[]  = { "st", "-t", "chiadvcalc", "-e", "chiadvcalc", NULL };
static const char *filecmd[]     = { "st", "-e", "chifm", NULL };
static const char *clipcmd[]     = { "chiclip", NULL };
static const char *scrotcmd[]    = { "gnome-screenshot", NULL };
static const char *scrotregion[] = { "gnome-screenshot", "-a", NULL };
static const char *rebootcmd[]   = { "doas", "reboot", NULL };
static const char *shutdowncmd[] = { "doas", "poweroff", NULL };
static const char *volup[]       = { "dwmstatus", "vol", "up",   NULL };
static const char *voldown[]     = { "dwmstatus", "vol", "down", NULL };
static const char *volmute[]     = { "dwmstatus", "vol", "mute", NULL };
static const char *brightup[]    = { "dwmstatus", "bright", "up",   NULL };
static const char *brightdown[]  = { "dwmstatus", "bright", "down", NULL };

static const Key keys[] = {
	/* modifier                     key              function        argument */

	/* --- scratchpad --- */
	{ MODKEY, XK_grave, togglescratch, {.v = scratchpadcmd } }, /* Super+` → scratchpad */

	/* --- calculator --- */
	{ MODKEY|ShiftMask,             XK_c,            spawn,          {.v = calccmd } },   /* Super+Shift+C   → dmenu calc */
	{ MODKEY,                       XK_c,            spawn,          {.v = clipcmd } },   /* Super+C         → clipboard history */

	/* --- apps (Hyprland defaults) --- */
	{ MODKEY,                       XK_Return,       spawn,          {.v = termcmd } },    /* Super+Enter     → terminal */
	{ MODKEY,                       XK_r,            spawn,          {.v = dmenucmd } },   /* Super+R         → app launcher */
	{ MODKEY,                       XK_e,            spawn,          {.v = filecmd } },    /* Super+E         → file manager */
	{ MODKEY,                       XK_b,            spawn,          {.v = browsercmd } }, /* Super+B         → browser */
	{ MODKEY,                       XK_Print,        spawn,          {.v = scrotcmd } },   /* Super+PrtSc     → screenshot */
	{ MODKEY|ShiftMask,             XK_Print,        spawn,          {.v = scrotregion } },/* Super+Shift+PrtSc → region screenshot */

	/* --- window management --- */
	{ MODKEY,                       XK_q,            killclient,     {0} },                /* Super+Q         → close window */
	{ MODKEY,                       XK_f,            spawn,          {.v = keyscmd } },   /* Super+F         → keybind cheatsheet */
	{ MODKEY,                       XK_space,        togglefloating, {0} },                /* Super+Space     → toggle floating */
	{ MODKEY,                       XK_t,            setlayout,      {.v = &layouts[0]} }, /* Super+T         → tile layout */
	{ MODKEY,                       XK_v,            setlayout,      {.v = &layouts[1]} }, /* Super+V         → float layout */
	{ MODKEY,                       XK_m,            setlayout,      {.v = &layouts[2]} }, /* Super+M         → monocle */

	/* --- focus / stack navigation --- */
	{ MODKEY,                       XK_j,            focusstack,     {.i = +1 } },         /* Super+J         → focus next window */
	{ MODKEY,                       XK_k,            focusstack,     {.i = -1 } },         /* Super+K         → focus prev window */
	{ MODKEY|ShiftMask,             XK_Return,       zoom,           {0} },                /* Super+Shift+Enter → promote to master */
	{ MODKEY|ShiftMask,             XK_j,            pushdown,       {0} },                /* Super+Shift+J   → push window down */
	{ MODKEY|ShiftMask,             XK_k,            pushup,         {0} },                /* Super+Shift+K   → push window up */

	/* --- resize master --- */
	{ MODKEY,                       XK_h,            setmfact,       {.f = -0.05} },       /* Super+H         → shrink master */
	{ MODKEY,                       XK_l,            setmfact,       {.f = +0.05} },       /* Super+L         → expand master */
	{ MODKEY,                       XK_i,            incnmaster,     {.i = +1 } },         /* Super+I         → add master slot */
	{ MODKEY,                       XK_d,            incnmaster,     {.i = -1 } },         /* Super+D         → remove master slot */

	/* --- bar & misc --- */
	{ MODKEY,                       XK_Tab,          view,           {0} },                /* Super+Tab       → last tag */
	{ MODKEY,                       XK_p,            togglebar,      {0} },                /* Super+P         → toggle bar */

	/* --- monitor focus --- */
	{ MODKEY,                       XK_comma,        focusmon,       {.i = -1 } },
	{ MODKEY,                       XK_period,       focusmon,       {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_comma,        tagmon,         {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_period,       tagmon,         {.i = +1 } },

	/* --- workspaces / tags --- */
	{ MODKEY,                       XK_0,            view,           {.ui = ~0 } },        /* Super+0         → view all tags */
	{ MODKEY|ShiftMask,             XK_0,            tag,            {.ui = ~0 } },        /* Super+Shift+0   → pin to all tags */
	TAGKEYS(                        XK_1,                            0)                    /* Super+1..9      → go to workspace */
	TAGKEYS(                        XK_2,                            1)                    /* Super+Shift+1..9→ move to workspace */
	TAGKEYS(                        XK_3,                            2)
	TAGKEYS(                        XK_4,                            3)
	TAGKEYS(                        XK_5,                            4)
	TAGKEYS(                        XK_6,                            5)
	TAGKEYS(                        XK_7,                            6)
	TAGKEYS(                        XK_8,                            7)
	TAGKEYS(                        XK_9,                            8)

	/* --- volume & brightness (media keys) --- */
	{ 0,      XF86XK_AudioRaiseVolume,   spawn, {.v = volup } },
	{ 0,      XF86XK_AudioLowerVolume,   spawn, {.v = voldown } },
	{ 0,      XF86XK_AudioMute,          spawn, {.v = volmute } },
	{ 0,      XF86XK_MonBrightnessUp,    spawn, {.v = brightup } },
	{ 0,      XF86XK_MonBrightnessDown,  spawn, {.v = brightdown } },
	{ MODKEY, XK_F1,                     spawn, {.v = volup } },      /* test: Super+F1 → vol up */

	/* --- session --- */
	{ MODKEY|ShiftMask,             XK_e,            quit,           {0} },                /* Super+Shift+E   → quit dwm */
	{ MODKEY|ShiftMask,             XK_r,            spawn,          {.v = rebootcmd } },  /* Super+Shift+R   → reboot */
	{ MODKEY|ShiftMask,             XK_p,            spawn,          {.v = shutdowncmd } },/* Super+Shift+P   → poweroff */
};

/* button definitions */
/* click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static const Button buttons[] = {
	/* click                event mask      button          function        argument */
	{ ClkLtSymbol,          0,              Button1,        setlayout,      {0} },
	{ ClkLtSymbol,          0,              Button3,        setlayout,      {.v = &layouts[2]} },
	{ ClkWinTitle,          0,              Button2,        zoom,           {0} },
	{ ClkStatusText,        0,              Button2,        spawn,          {.v = termcmd } },
	{ ClkClientWin,         MODKEY,         Button1,        movemouse,      {0} },          /* Super+LMB drag  → move window */
	{ ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} },
	{ ClkClientWin,         MODKEY,         Button3,        resizemouse,    {0} },          /* Super+RMB drag  → resize window */
	{ ClkTagBar,            0,              Button1,        view,           {0} },
	{ ClkTagBar,            0,              Button3,        toggleview,     {0} },
	{ ClkTagBar,            MODKEY,         Button1,        tag,            {0} },
	{ ClkTagBar,            MODKEY,         Button3,        toggletag,      {0} },
};
