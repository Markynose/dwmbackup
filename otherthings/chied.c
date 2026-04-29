#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <ctype.h>

/* ---- types ---- */

typedef struct {
	char  *data;
	int    len;
	int    cap;
} Row;

typedef struct {
	Row   *rows;
	int    nrows;
	int    crow, ccol;   /* cursor row, col */
	int    rowoff, coloff;
	int    scrrows, scrcols;
	char  *filename;
	int    dirty;
	char   msg[128];
	/* undo */
	/* mode */
	int    mode;         /* 0=normal 1=insert */
} Ed;

#define MODE_NORMAL 0
#define MODE_INSERT 1

static Ed E;
static struct termios orig_termios;

/* ---- terminal ---- */

static void die(const char *s) {
	write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
	perror(s); exit(1);
}

static void term_restore(void) {
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
	write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
}

static void term_raw(void) {
	if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) die("tcgetattr");
	atexit(term_restore);
	struct termios raw = orig_termios;
	raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |=  (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) die("tcsetattr");
}

static void get_winsize(void) {
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) die("ioctl");
	E.scrcols = ws.ws_col;
	E.scrrows = ws.ws_row - 2; /* reserve status + cmd line */
}

/* ---- key reading ---- */

#define KEY_ESC       27
#define KEY_ENTER     13
#define KEY_BACKSPACE 127
#define KEY_UP        1000
#define KEY_DOWN      1001
#define KEY_LEFT      1002
#define KEY_RIGHT     1003
#define KEY_DEL       1004
#define KEY_HOME      1005
#define KEY_END       1006
#define KEY_PGUP      1007
#define KEY_PGDN      1008

static int read_key(void) {
	char c;
	if (read(STDIN_FILENO, &c, 1) != 1) return 0;
	if (c != '\x1b') return (unsigned char)c;

	/* short timeout for escape sequences vs bare ESC */
	struct termios t = orig_termios;
	t.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	t.c_iflag &= ~(IXON | ICRNL);
	t.c_oflag &= ~(OPOST);
	t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 1; /* 100ms */
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);

	char seq[4] = {0};
	int r0 = read(STDIN_FILENO, &seq[0], 1);

	/* restore raw mode */
	t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);

	if (r0 != 1) return KEY_ESC;
	if (read(STDIN_FILENO, &seq[1], 1) != 1) return KEY_ESC;

	if (seq[0] == '[') {
		if (seq[1] >= '0' && seq[1] <= '9') {
			read(STDIN_FILENO, &seq[2], 1);
			if (seq[2] == '~') {
				switch (seq[1]) {
				case '1': return KEY_HOME;
				case '3': return KEY_DEL;
				case '4': return KEY_END;
				case '5': return KEY_PGUP;
				case '6': return KEY_PGDN;
				case '7': return KEY_HOME;
				case '8': return KEY_END;
				}
			}
		}
		switch (seq[1]) {
		case 'A': return KEY_UP;
		case 'B': return KEY_DOWN;
		case 'C': return KEY_RIGHT;
		case 'D': return KEY_LEFT;
		case 'H': return KEY_HOME;
		case 'F': return KEY_END;
		}
	}
	return KEY_ESC;
}

/* ---- buffer helpers ---- */

static void row_insert_char(Row *r, int at, char c) {
	if (r->len + 2 > r->cap) {
		r->cap = r->cap ? r->cap * 2 : 16;
		r->data = realloc(r->data, r->cap);
	}
	memmove(r->data + at + 1, r->data + at, r->len - at);
	r->data[at] = c;
	r->len++;
	r->data[r->len] = 0;
}

static void row_delete_char(Row *r, int at) {
	if (at < 0 || at >= r->len) return;
	memmove(r->data + at, r->data + at + 1, r->len - at);
	r->len--;
}

static void insert_row(int at, const char *s, int len) {
	E.rows = realloc(E.rows, sizeof(Row) * (E.nrows + 1));
	memmove(&E.rows[at + 1], &E.rows[at], sizeof(Row) * (E.nrows - at));
	E.rows[at].len  = len;
	E.rows[at].cap  = len + 1;
	E.rows[at].data = malloc(len + 1);
	memcpy(E.rows[at].data, s, len);
	E.rows[at].data[len] = 0;
	E.nrows++;
}

static void delete_row(int at) {
	if (at < 0 || at >= E.nrows) return;
	free(E.rows[at].data);
	memmove(&E.rows[at], &E.rows[at + 1], sizeof(Row) * (E.nrows - at - 1));
	E.nrows--;
}

/* ---- cursor movement ---- */

static void clamp_cursor(void) {
	if (E.crow < 0) E.crow = 0;
	if (E.crow >= E.nrows) E.crow = E.nrows > 0 ? E.nrows - 1 : 0;
	int rowlen = E.nrows ? E.rows[E.crow].len : 0;
	int maxcol = E.mode == MODE_INSERT ? rowlen : (rowlen > 0 ? rowlen - 1 : 0);
	if (E.ccol > maxcol) E.ccol = maxcol;
	if (E.ccol < 0) E.ccol = 0;
}

/* ---- rendering ---- */

static char rbuf[65536];
static int  rlen;

static void rb_append(const char *s, int n) {
	if (rlen + n >= (int)sizeof(rbuf)) return;
	memcpy(rbuf + rlen, s, n);
	rlen += n;
}

static void rb_str(const char *s) { rb_append(s, strlen(s)); }

static void draw_rows(void) {
	for (int y = 0; y < E.scrrows; y++) {
		int filerow = y + E.rowoff;
		rb_str("\x1b[K");
		if (filerow < E.nrows) {
			int len = E.rows[filerow].len - E.coloff;
			if (len < 0) len = 0;
			if (len > E.scrcols) len = E.scrcols;
			if (len > 0)
				rb_append(E.rows[filerow].data + E.coloff, len);
		} else {
			rb_str("~");
		}
		rb_str("\r\n");
	}
}

static void draw_status(void) {
	rb_str("\x1b[7m"); /* reverse video */
	char stat[256];
	const char *mode = E.mode == MODE_INSERT ? "INSERT" : "NORMAL";
	const char *fname = E.filename ? E.filename : "[No Name]";
	int n = snprintf(stat, sizeof(stat), " %s | %s%s ",
		mode, fname, E.dirty ? " [+]" : "");
	if (n > E.scrcols) n = E.scrcols;
	rb_append(stat, n);
	char pos[32];
	int plen = snprintf(pos, sizeof(pos), " %d:%d ", E.crow + 1, E.ccol + 1);
	while (n < E.scrcols - plen) { rb_str(" "); n++; }
	rb_append(pos, plen);
	rb_str("\x1b[m\r\n");
}

static void draw_cmdline(void) {
	rb_str("\x1b[K");
	if (E.msg[0]) rb_str(E.msg);
}

static void refresh_screen(void) {
	/* scroll */
	if (E.crow < E.rowoff) E.rowoff = E.crow;
	if (E.crow >= E.rowoff + E.scrrows) E.rowoff = E.crow - E.scrrows + 1;
	if (E.ccol < E.coloff) E.coloff = E.ccol;
	if (E.ccol >= E.coloff + E.scrcols) E.coloff = E.ccol - E.scrcols + 1;

	rlen = 0;
	rb_str("\x1b[?25l\x1b[H");
	draw_rows();
	draw_status();
	draw_cmdline();

	/* reposition cursor */
	char cur[32];
	snprintf(cur, sizeof(cur), "\x1b[%d;%dH",
		(E.crow - E.rowoff) + 1,
		(E.ccol - E.coloff) + 1);
	rb_str(cur);
	rb_str("\x1b[?25h");
	write(STDOUT_FILENO, rbuf, rlen);
}

/* ---- file I/O ---- */

static void open_file(const char *path) {
	free(E.filename);
	E.filename = strdup(path);
	FILE *f = fopen(path, "r");
	if (!f) { /* new file */ return; }
	char *line = NULL; size_t cap = 0; ssize_t n;
	while ((n = getline(&line, &cap, f)) > 0) {
		while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) n--;
		insert_row(E.nrows, line, n);
	}
	free(line);
	fclose(f);
	E.dirty = 0;
}

static void save_file(void) {
	if (!E.filename) {
		snprintf(E.msg, sizeof(E.msg), "No filename.");
		return;
	}
	FILE *f = fopen(E.filename, "w");
	if (!f) { snprintf(E.msg, sizeof(E.msg), "Can't save: %s", E.filename); return; }
	for (int i = 0; i < E.nrows; i++)
		fprintf(f, "%s\n", E.rows[i].data);
	fclose(f);
	E.dirty = 0;
	snprintf(E.msg, sizeof(E.msg), "Saved %s", E.filename);
}

/* ---- command line ---- */

static void run_command(void) {
	/* read a : command from the bottom line */
	char cmd[256] = {0};
	int ci = 0;
	E.msg[0] = ':'; E.msg[1] = 0;
	refresh_screen();

	char c;
	while (1) {
		if (read(STDIN_FILENO, &c, 1) != 1) break;
		if (c == KEY_ENTER || c == '\r') break;
		if (c == KEY_ESC) { E.msg[0] = 0; return; }
		if ((c == KEY_BACKSPACE || c == 127) && ci > 0) {
			cmd[--ci] = 0;
		} else if (c >= 32 && ci < (int)sizeof(cmd) - 1) {
			cmd[ci++] = c;
			cmd[ci] = 0;
		}
		snprintf(E.msg, sizeof(E.msg), ":%s", cmd);
		refresh_screen();
	}

	if (!strcmp(cmd, "w"))        save_file();
	else if (!strcmp(cmd, "q"))  { if (!E.dirty) { exit(0); } else snprintf(E.msg, sizeof(E.msg), "Unsaved changes. Use :q!"); }
	else if (!strcmp(cmd, "q!")) exit(0);
	else if (!strcmp(cmd, "wq") || !strcmp(cmd, "x")) { save_file(); exit(0); }
	else snprintf(E.msg, sizeof(E.msg), "Unknown command: %s", cmd);
}

/* ---- normal mode ---- */

static void insert_newline(void) {
	if (E.nrows == 0) {
		insert_row(0, "", 0);
		E.crow = 0; E.ccol = 0;
		return;
	}
	Row *r = &E.rows[E.crow];
	int rest_len = r->len - E.ccol;
	char *rest = malloc(rest_len + 1);
	memcpy(rest, r->data + E.ccol, rest_len);
	rest[rest_len] = 0;
	/* truncate current row */
	r->len = E.ccol;
	r->data[r->len] = 0;
	insert_row(E.crow + 1, rest, rest_len);
	free(rest);
	E.crow++;
	E.ccol = 0;
	E.dirty = 1;
}

static void join_row(void) {
	/* join current row with previous (backspace at col 0) */
	if (E.crow == 0) return;
	Row *prev = &E.rows[E.crow - 1];
	Row *cur  = &E.rows[E.crow];
	int newcol = prev->len;
	int newlen = prev->len + cur->len;
	prev->data = realloc(prev->data, newlen + 1);
	memcpy(prev->data + prev->len, cur->data, cur->len);
	prev->len = newlen;
	prev->data[newlen] = 0;
	delete_row(E.crow);
	E.crow--;
	E.ccol = newcol;
	E.dirty = 1;
}

static void process_normal(int k) {
	E.msg[0] = 0;
	static int g_pending = 0;

	switch (k) {
	/* movement */
	case 'h': case KEY_LEFT:  E.ccol--; break;
	case 'l': case KEY_RIGHT: E.ccol++; break;
	case 'k': case KEY_UP:    E.crow--; break;
	case 'j': case KEY_DOWN:  E.crow++; break;
	case '0': case KEY_HOME:  E.ccol = 0; break;
	case '$': case KEY_END:
		E.ccol = E.nrows ? E.rows[E.crow].len - 1 : 0;
		break;
	case 'g':
		if (g_pending) { E.crow = 0; E.ccol = 0; g_pending = 0; }
		else g_pending = 1;
		return;
	case 'G':
		E.crow = E.nrows > 0 ? E.nrows - 1 : 0;
		E.ccol = 0;
		break;
	case KEY_PGUP:
		E.crow -= E.scrrows;
		break;
	case KEY_PGDN:
		E.crow += E.scrrows;
		break;
	case 'w': { /* next word */
		if (E.nrows == 0) break;
		Row *r = &E.rows[E.crow];
		int c = E.ccol;
		while (c < r->len && !isspace((unsigned char)r->data[c])) c++;
		while (c < r->len &&  isspace((unsigned char)r->data[c])) c++;
		E.ccol = c;
		break;
	}
	case 'b': { /* prev word */
		if (E.nrows == 0) break;
		Row *r = &E.rows[E.crow];
		int c = E.ccol - 1;
		while (c > 0 && isspace((unsigned char)r->data[c])) c--;
		while (c > 0 && !isspace((unsigned char)r->data[c-1])) c--;
		E.ccol = c;
		break;
	}
	/* mode switches */
	case 'i': E.mode = MODE_INSERT; break;
	case 'I': E.ccol = 0; E.mode = MODE_INSERT; break;
	case 'a':
		if (E.nrows && E.rows[E.crow].len > 0) E.ccol++;
		E.mode = MODE_INSERT;
		break;
	case 'A':
		E.ccol = E.nrows ? E.rows[E.crow].len : 0;
		E.mode = MODE_INSERT;
		break;
	case 'o':
		E.ccol = E.nrows ? E.rows[E.crow].len : 0;
		insert_newline();
		E.mode = MODE_INSERT;
		break;
	case 'O':
		if (E.nrows == 0) { insert_row(0, "", 0); }
		else insert_row(E.crow, "", 0);
		E.ccol = 0;
		E.mode = MODE_INSERT;
		break;
	/* editing */
	case 'x':
		if (E.nrows && E.rows[E.crow].len > 0) {
			row_delete_char(&E.rows[E.crow], E.ccol);
			E.dirty = 1;
		}
		break;
	case 'd': { /* dd — delete line */
		static int d_pending = 0;
		if (d_pending) {
			if (E.nrows) { delete_row(E.crow); E.dirty = 1; }
			if (E.crow >= E.nrows && E.crow > 0) E.crow--;
			d_pending = 0;
		} else { d_pending = 1; return; }
		break;
	}
	case 'y': { /* yy — yank line (to internal buffer) */
		static int y_pending = 0;
		static char yank_buf[4096] = {0};
		if (y_pending) {
			if (E.nrows) strncpy(yank_buf, E.rows[E.crow].data, sizeof(yank_buf)-1);
			snprintf(E.msg, sizeof(E.msg), "Yanked");
			y_pending = 0;
		} else { y_pending = 1; return; }
		break;
	}
	case 'p': { /* paste yanked line below */
		static char yank_buf[4096] = {0};
		if (yank_buf[0]) {
			insert_row(E.crow + 1, yank_buf, strlen(yank_buf));
			E.crow++;
			E.dirty = 1;
		}
		break;
	}
	case 'u': /* placeholder for undo */
		snprintf(E.msg, sizeof(E.msg), "Undo not yet implemented");
		break;
	/* commands */
	case ':': run_command(); break;
	}
	g_pending = 0;
	clamp_cursor();
}

/* ---- insert mode ---- */

static void process_insert(int k) {
	switch (k) {
	case KEY_ESC:
		E.mode = MODE_NORMAL;
		if (E.ccol > 0) E.ccol--;
		clamp_cursor();
		break;
	case KEY_ENTER:
		insert_newline();
		E.dirty = 1;
		break;
	case KEY_BACKSPACE:
		if (E.ccol > 0) {
			row_delete_char(&E.rows[E.crow], E.ccol - 1);
			E.ccol--;
			E.dirty = 1;
		} else {
			join_row();
		}
		break;
	case KEY_DEL:
		if (E.nrows && E.ccol < E.rows[E.crow].len) {
			row_delete_char(&E.rows[E.crow], E.ccol);
			E.dirty = 1;
		}
		break;
	case KEY_LEFT:  E.ccol--; clamp_cursor(); break;
	case KEY_RIGHT: E.ccol++; clamp_cursor(); break;
	case KEY_UP:    E.crow--; clamp_cursor(); break;
	case KEY_DOWN:  E.crow++; clamp_cursor(); break;
	default:
		if (k >= 32 && k < 127) {
			if (E.nrows == 0) insert_row(0, "", 0);
			row_insert_char(&E.rows[E.crow], E.ccol, (char)k);
			E.ccol++;
			E.dirty = 1;
		}
		break;
	}
}

/* ---- main ---- */

int main(int argc, char *argv[]) {
	term_raw();
	get_winsize();

	if (argc >= 2) open_file(argv[1]);

	snprintf(E.msg, sizeof(E.msg), "chied — :w save  :q quit  i insert  Esc normal");

	while (1) {
		refresh_screen();
		int k = read_key();
		if (E.mode == MODE_NORMAL) process_normal(k);
		else                       process_insert(k);
	}
}
