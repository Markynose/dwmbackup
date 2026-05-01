#define _POSIX_C_SOURCE 200809L
#include <ncurses.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>

#define MAX_ENT   4096
#define MAX_YANK  512
#define C_DIR     1
#define C_FILE    2
#define C_SEL     3
#define C_BAR     4
#define C_MARK    5  /* bulk-selected files */

typedef struct {
	char  name[256];
	int   is_dir;
	off_t size;
} Entry;

static char  cwd[4096];
static Entry ents[MAX_ENT];
static int   nents, sel, offset, show_hidden;

/* yank buffer — supports multiple files */
static char yank_paths[MAX_YANK][4096];
static int  nyank  = 0;
static int  yank_op = 0;  /* 1=copy 2=move */

/* bulk selection */
static int selected[MAX_ENT];
static int nselected = 0;

static int entcmp(const void *a, const void *b) {
	const Entry *ea = a, *eb = b;
	if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir;
	return strcasecmp(ea->name, eb->name);
}

static void load_dir(const char *path) {
	DIR *d = opendir(path);
	if (!d) return;
	nents = 0;
	struct dirent *de;
	while ((de = readdir(d)) && nents < MAX_ENT) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
		if (!show_hidden && de->d_name[0] == '.') continue;
		Entry *e = &ents[nents++];
		strncpy(e->name, de->d_name, 255);
		char full[4096];
		snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
		struct stat st;
		if (stat(full, &st) == 0) {
			e->is_dir = S_ISDIR(st.st_mode);
			e->size   = st.st_size;
		} else {
			e->is_dir = (de->d_type == DT_DIR);
			e->size   = 0;
		}
	}
	closedir(d);
	qsort(ents, nents, sizeof(Entry), entcmp);
	memset(selected, 0, sizeof(selected));
	nselected = 0;
}

static void fmt_size(off_t sz, char *buf, size_t n) {
	if      (sz >= (off_t)1<<30) snprintf(buf, n, "%.1fG", (double)sz/(1<<30));
	else if (sz >= (off_t)1<<20) snprintf(buf, n, "%.1fM", (double)sz/(1<<20));
	else if (sz >= (off_t)1<<10) snprintf(buf, n, "%.1fK", (double)sz/(1<<10));
	else                         snprintf(buf, n, "%ldB",  (long)sz);
}

/* returns paths of selected files, or just the cursor file if none selected */
static int get_targets(char paths[][4096], int maxn) {
	int n = 0;
	if (nselected > 0) {
		for (int i = 0; i < nents && n < maxn; i++) {
			if (!selected[i]) continue;
			if (!strcmp(cwd, "/"))
				snprintf(paths[n], 4096, "/%s", ents[i].name);
			else
				snprintf(paths[n], 4096, "%s/%s", cwd, ents[i].name);
			n++;
		}
	} else if (nents > 0) {
		if (!strcmp(cwd, "/"))
			snprintf(paths[0], 4096, "/%s", ents[sel].name);
		else
			snprintf(paths[0], 4096, "%s/%s", cwd, ents[sel].name);
		n = 1;
	}
	return n;
}

static void draw(void) {
	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	int lh = rows - 2;

	if (sel < 0)          sel = 0;
	if (sel >= nents)     sel = nents > 0 ? nents - 1 : 0;
	if (sel < offset)     offset = sel;
	if (sel >= offset+lh) offset = sel - lh + 1;

	/* top bar */
	attron(COLOR_PAIR(C_BAR) | A_BOLD);
	mvhline(0, 0, ' ', cols);
	if (nyank > 0) {
		char *yn = nyank == 1 ? strrchr(yank_paths[0], '/') : NULL;
		if (nyank == 1)
			mvprintw(0, 1, " %s  [%s: %s]", cwd,
				yank_op == 1 ? "copy" : "move", yn ? yn+1 : yank_paths[0]);
		else
			mvprintw(0, 1, " %s  [%s: %d files]", cwd,
				yank_op == 1 ? "copy" : "move", nyank);
	} else {
		mvprintw(0, 1, " %s", cwd);
	}
	if (nselected > 0) {
		mvprintw(0, cols - 14, " [%d selected]", nselected);
	}
	attroff(COLOR_PAIR(C_BAR) | A_BOLD);

	/* entries */
	for (int i = 0; i < lh; i++) {
		int idx = offset + i;
		move(i + 1, 0);
		clrtoeol();
		if (idx >= nents) continue;
		Entry *e = &ents[idx];

		if (idx == sel)        attron(COLOR_PAIR(C_SEL)  | A_BOLD);
		else if (selected[idx]) attron(COLOR_PAIR(C_MARK) | A_BOLD);
		else if (e->is_dir)    attron(COLOR_PAIR(C_DIR)  | A_BOLD);
		else                   attron(COLOR_PAIR(C_FILE));

		mvhline(i + 1, 0, ' ', cols);

		char prefix[4] = "   ";
		if (selected[idx]) prefix[1] = '*';

		char disp[264];
		snprintf(disp, sizeof(disp), "%s%s%s", prefix, e->name, e->is_dir ? "/" : "");
		mvprintw(i + 1, 0, "%-*s", cols - 9, disp);

		if (!e->is_dir) {
			char sz[12];
			fmt_size(e->size, sz, sizeof(sz));
			mvprintw(i + 1, cols - 9, "%8s ", sz);
		}

		attroff(COLOR_PAIR(C_SEL)|COLOR_PAIR(C_DIR)|COLOR_PAIR(C_FILE)|COLOR_PAIR(C_MARK)|A_BOLD);
	}

	/* bottom bar */
	attron(COLOR_PAIR(C_BAR));
	mvhline(rows - 1, 0, ' ', cols);
	if (nents > 0)
		mvprintw(rows - 1, 1,
			" [%d/%d]  hjkl  Spc:sel  y/x/p  d:del  R:ren  a:arch  X:extract  e:xdg  ::cmd  q:quit",
			sel + 1, nents);
	else
		mvprintw(rows - 1, 1, " (empty)  h:up  .:hidden  q:quit");
	attroff(COLOR_PAIR(C_BAR));

	refresh();
}

/* generic input prompt — prefill can be NULL */
static int prompt_input(const char *label, const char *prefill, char *out, int outsz) {
	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	int startx = (int)strlen(label) + 2;

	attron(COLOR_PAIR(C_BAR));
	mvhline(rows - 1, 0, ' ', cols);
	mvprintw(rows - 1, 1, "%s", label);
	attroff(COLOR_PAIR(C_BAR));

	char input[256] = {0};
	int i = 0;
	if (prefill) {
		strncpy(input, prefill, outsz - 1);
		i = strlen(input);
		attron(COLOR_PAIR(C_BAR));
		mvprintw(rows - 1, startx, "%s", input);
		attroff(COLOR_PAIR(C_BAR));
	}

	echo(); curs_set(1);
	move(rows - 1, startx + i);
	refresh();

	int ch;
	while ((ch = getch()) != '\n' && ch != KEY_ENTER) {
		if (ch == 27) { noecho(); curs_set(0); return 0; }
		if ((ch == KEY_BACKSPACE || ch == 127) && i > 0) {
			i--;
			input[i] = 0;
			attron(COLOR_PAIR(C_BAR));
			mvprintw(rows - 1, startx, "%-*s", cols - startx - 1, input);
			attroff(COLOR_PAIR(C_BAR));
			move(rows - 1, startx + i);
		} else if (ch >= 32 && ch < 127 && i < outsz - 1) {
			input[i++] = ch;
			input[i] = 0;
			attron(COLOR_PAIR(C_BAR));
			mvaddch(rows - 1, startx + i - 1, ch);
			attroff(COLOR_PAIR(C_BAR));
		}
		refresh();
	}
	noecho(); curs_set(0);
	if (!input[0]) return 0;
	strncpy(out, input, outsz - 1);
	return 1;
}

static void go_up(void) {
	char *slash = strrchr(cwd, '/');
	if (!slash) return;
	char old[256];
	strncpy(old, slash + 1, 255);
	if (slash == cwd) cwd[1] = 0;
	else              *slash = 0;
	if (!cwd[0]) strcpy(cwd, "/");
	load_dir(cwd);
	sel = 0; offset = 0;
	for (int i = 0; i < nents; i++)
		if (!strcmp(ents[i].name, old)) { sel = i; break; }
}

static void open_sel(void) {
	if (!nents) return;
	Entry *e = &ents[sel];
	if (e->is_dir) {
		char newpath[4096];
		if (!strcmp(cwd, "/"))
			snprintf(newpath, sizeof(newpath), "/%s", e->name);
		else
			snprintf(newpath, sizeof(newpath), "%s/%s", cwd, e->name);
		strncpy(cwd, newpath, sizeof(cwd) - 1);
		load_dir(cwd);
		sel = 0; offset = 0;
		return;
	}
	char *editor = getenv("EDITOR");
	if (!editor) editor = "vi";
	char full[4096], cmd[4096 + 256];
	snprintf(full, sizeof(full), "%s/%s", cwd, e->name);
	snprintf(cmd,  sizeof(cmd),  "%s '%s'", editor, full);
	endwin();
	system(cmd);
	refresh(); clear();
}

static void toggle_select(void) {
	if (!nents) return;
	selected[sel] ^= 1;
	nselected += selected[sel] ? 1 : -1;
	sel++;  /* advance after marking, like ranger */
	if (sel >= nents) sel = nents - 1;
}

static void yank_sel(int op) {
	static char targets[MAX_YANK][4096];
	int n = get_targets(targets, MAX_YANK);
	if (!n) return;
	for (int i = 0; i < n; i++)
		strncpy(yank_paths[i], targets[i], 4095);
	nyank   = n;
	yank_op = op;
	memset(selected, 0, sizeof(selected));
	nselected = 0;
}

static void paste_sel(void) {
	if (!nyank || !yank_op) return;
	endwin();
	for (int i = 0; i < nyank; i++) {
		char *fname = strrchr(yank_paths[i], '/');
		if (!fname) continue;
		char dest[4096], cmd[8192];
		snprintf(dest, sizeof(dest), "%s/%s", cwd, fname + 1);
		if (yank_op == 1)
			snprintf(cmd, sizeof(cmd), "cp -r '%s' '%s'", yank_paths[i], dest);
		else
			snprintf(cmd, sizeof(cmd), "mv '%s' '%s'", yank_paths[i], dest);
		system(cmd);
	}
	if (yank_op == 2) { nyank = 0; yank_op = 0; }
	refresh(); clear();
	load_dir(cwd);
}

static void delete_sel(void) {
	static char targets[MAX_YANK][4096];
	int n = get_targets(targets, MAX_YANK);
	if (!n) return;

	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	attron(COLOR_PAIR(C_BAR) | A_BOLD);
	mvhline(rows - 1, 0, ' ', cols);
	if (n == 1) {
		char *nm = strrchr(targets[0], '/');
		mvprintw(rows - 1, 1, " delete '%s'? (y/n)", nm ? nm+1 : targets[0]);
	} else {
		mvprintw(rows - 1, 1, " delete %d files? (y/n)", n);
	}
	attroff(COLOR_PAIR(C_BAR) | A_BOLD);
	refresh();
	if (getch() != 'y') return;

	endwin();
	for (int i = 0; i < n; i++) {
		char cmd[4096 + 32];
		snprintf(cmd, sizeof(cmd), "rm -rf '%s'", targets[i]);
		system(cmd);
	}
	memset(selected, 0, sizeof(selected));
	nselected = 0;
	if (sel > 0 && sel >= nents - 1) sel--;
	refresh(); clear();
	load_dir(cwd);
}

static void rename_sel(void) {
	if (!nents) return;
	char newname[256] = {0};
	if (!prompt_input("rename: ", ents[sel].name, newname, sizeof(newname)))
		return;
	if (!strcmp(newname, ents[sel].name)) return;
	char src[4096], dst[4096], cmd[8192];
	snprintf(src, sizeof(src), "%s/%s", cwd, ents[sel].name);
	snprintf(dst, sizeof(dst), "%s/%s", cwd, newname);
	snprintf(cmd, sizeof(cmd), "mv '%s' '%s'", src, dst);
	system(cmd);
	load_dir(cwd);
	/* try to keep cursor on renamed file */
	for (int i = 0; i < nents; i++)
		if (!strcmp(ents[i].name, newname)) { sel = i; break; }
}

static void archive_sel(void) {
	static char targets[MAX_YANK][4096];
	int n = get_targets(targets, MAX_YANK);
	if (!n) return;

	/* default archive name */
	char defname[256];
	char *nm = strrchr(targets[0], '/');
	snprintf(defname, sizeof(defname), "%s.tar.gz", nm ? nm+1 : "archive");

	char archname[256] = {0};
	if (!prompt_input("archive name: ", defname, archname, sizeof(archname)))
		return;

	char archpath[4096];
	snprintf(archpath, sizeof(archpath), "%s/%s", cwd, archname);

	/* build: tar czf 'archpath' 'file1' 'file2' ... */
	char cmd[65536];
	int cx = snprintf(cmd, sizeof(cmd), "tar czf '%s'", archpath);
	for (int i = 0; i < n && cx < (int)sizeof(cmd) - 10; i++) {
		cx += snprintf(cmd + cx, sizeof(cmd) - cx, " '%s'", targets[i]);
	}

	endwin();
	chdir(cwd);
	system(cmd);
	printf("\n[press enter]");
	fflush(stdout);
	getchar();
	refresh(); clear();
	memset(selected, 0, sizeof(selected));
	nselected = 0;
	load_dir(cwd);
}

static void extract_sel(void) {
	if (!nents) return;
	Entry *e = &ents[sel];

	/* ./ prefix so filenames starting with '-' aren't parsed as flags */
	char rel[260];
	snprintf(rel, sizeof(rel), "./%s", e->name);

	char cmd[512];
	const char *n = e->name;
	if (strstr(n, ".tar.gz") || strstr(n, ".tgz"))
		snprintf(cmd, sizeof(cmd), "tar xzf '%s' -C .", rel);
	else if (strstr(n, ".tar.bz2") || strstr(n, ".tbz2"))
		snprintf(cmd, sizeof(cmd), "tar xjf '%s' -C .", rel);
	else if (strstr(n, ".tar.xz") || strstr(n, ".txz"))
		snprintf(cmd, sizeof(cmd), "tar xJf '%s' -C .", rel);
	else if (strstr(n, ".tar.zst"))
		snprintf(cmd, sizeof(cmd), "tar --zstd -xf '%s' -C .", rel);
	else if (strstr(n, ".tar"))
		snprintf(cmd, sizeof(cmd), "tar xf '%s' -C .", rel);
	else if (strstr(n, ".zip") || strstr(n, ".ZIP"))
		snprintf(cmd, sizeof(cmd), "unzip -d . '%s'", rel);
	else if (strstr(n, ".gz"))
		snprintf(cmd, sizeof(cmd), "gunzip -k '%s'", rel);
	else if (strstr(n, ".bz2"))
		snprintf(cmd, sizeof(cmd), "bunzip2 -k '%s'", rel);
	else if (strstr(n, ".xz"))
		snprintf(cmd, sizeof(cmd), "xz -dk '%s'", rel);
	else
		snprintf(cmd, sizeof(cmd), "tar xf '%s' -C .", rel);

	endwin();
	chdir(cwd);
	system(cmd);
	printf("\n[press enter]");
	fflush(stdout);
	getchar();
	refresh(); clear();
	load_dir(cwd);
}

static void run_cmd(void) {
	char input[1024] = {0};
	if (!prompt_input(":%%f=sel  cmd: ", NULL, input, sizeof(input)))
		return;

	/* replace %f with full path of selected file */
	char selpath[4096] = {0};
	if (nents > 0) {
		if (!strcmp(cwd, "/"))
			snprintf(selpath, sizeof(selpath), "/%s", ents[sel].name);
		else
			snprintf(selpath, sizeof(selpath), "%s/%s", cwd, ents[sel].name);
	}

	char cmd[4096] = {0};
	int ci = 0;
	for (int j = 0; input[j] && ci < (int)sizeof(cmd) - 1; j++) {
		if (input[j] == '%' && input[j+1] == 'f') {
			int slen = (int)strlen(selpath);
			if (ci + slen < (int)sizeof(cmd) - 1) {
				memcpy(cmd + ci, selpath, slen);
				ci += slen;
			}
			j++;
		} else {
			cmd[ci++] = input[j];
		}
	}
	cmd[ci] = 0;

	endwin();
	chdir(cwd);
	system(cmd);
	printf("\n[press enter]");
	fflush(stdout);
	getchar();
	refresh(); clear();
	load_dir(cwd);
}

static void open_xdg(void) {
	if (!nents) return;
	Entry *e = &ents[sel];
	char full[4096], cmd[4096 + 64];
	snprintf(full, sizeof(full), "%s/%s", cwd, e->name);
	const char *ext = strrchr(e->name, '.');
	if (ext && (!strcasecmp(ext, ".png") || !strcasecmp(ext, ".jpg") ||
	            !strcasecmp(ext, ".jpeg") || !strcasecmp(ext, ".gif") ||
	            !strcasecmp(ext, ".bmp") || !strcasecmp(ext, ".webp")))
		snprintf(cmd, sizeof(cmd), "chiview '%s' &", full);
	else if (ext && (!strcasecmp(ext, ".mp4") || !strcasecmp(ext, ".mkv") ||
	                 !strcasecmp(ext, ".avi") || !strcasecmp(ext, ".mov") ||
	                 !strcasecmp(ext, ".webm") || !strcasecmp(ext, ".flv")))
		snprintf(cmd, sizeof(cmd), "chivid '%s' &", full);
	else
		snprintf(cmd, sizeof(cmd), "xdg-open '%s' &", full);
	endwin();
	system(cmd);
	refresh(); clear();
}

int main(int argc, char *argv[]) {
	if (argc > 1) strncpy(cwd, argv[1], sizeof(cwd) - 1);
	else          getcwd(cwd, sizeof(cwd));

	load_dir(cwd);

	initscr(); noecho(); cbreak();
	keypad(stdscr, TRUE); curs_set(0);
	start_color(); use_default_colors();

	/* pink theme */
	init_pair(C_DIR,  COLOR_MAGENTA, -1);
	init_pair(C_FILE, -1, -1);
	init_pair(C_SEL,  COLOR_WHITE,   COLOR_MAGENTA);
	init_pair(C_BAR,  COLOR_WHITE,   COLOR_MAGENTA);
	init_pair(C_MARK, COLOR_MAGENTA, COLOR_BLACK);   /* bulk-selected */

	draw();
	int ch;
	while ((ch = getch()) != 'q') {
		switch (ch) {
		case 'j': case KEY_DOWN:  sel++; break;
		case 'k': case KEY_UP:    sel--; break;
		case 'g':                 sel = 0; break;
		case 'G':                 sel = nents - 1; break;
		case 'l': case KEY_RIGHT: case '\n': case KEY_ENTER: open_sel(); break;
		case 'h': case KEY_LEFT:  case KEY_BACKSPACE: case 127: go_up(); break;
		case '.': show_hidden ^= 1; load_dir(cwd); sel = 0; offset = 0; break;
		case ' ': toggle_select(); break;
		case 'y': yank_sel(1); break;
		case 'x': yank_sel(2); break;
		case 'p': paste_sel(); break;
		case 'd': delete_sel(); break;
		case 'R': rename_sel(); break;
		case 'a': archive_sel(); break;
		case 'X': extract_sel(); break;
		case 'e': open_xdg(); break;
		case ':': run_cmd(); break;
		case 'r': load_dir(cwd); break;
		case KEY_RESIZE: clear(); break;
		}
		draw();
	}

	endwin();
	printf("%s\n", cwd);
	return 0;
}
