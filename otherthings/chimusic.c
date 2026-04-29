#define _POSIX_C_SOURCE 200809L
#include <ncurses.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

#define MAX_TRACKS 4096
#define C_BAR  1
#define C_SEL  2
#define C_PLAY 3
#define C_DIM  4

typedef struct {
	char path[4096];
	char name[256];
} Track;

static Track  tracks[MAX_TRACKS];
static int    ntracks, sel, vscroll;
static int    playing = -1;
static int    paused  = 0;
static pid_t  ffpid   = -1;
static time_t play_start = 0;
static long   paused_elapsed = 0;

static int is_music(const char *name) {
	const char *ext = strrchr(name, '.');
	if (!ext) return 0;
	return !strcasecmp(ext, ".flac") || !strcasecmp(ext, ".mp3")  ||
	       !strcasecmp(ext, ".ogg")  || !strcasecmp(ext, ".wav")  ||
	       !strcasecmp(ext, ".m4a")  || !strcasecmp(ext, ".opus") ||
	       !strcasecmp(ext, ".aac");
}

static int trackcmp(const void *a, const void *b) {
	return strcasecmp(((Track*)a)->name, ((Track*)b)->name);
}

static void load_dir(const char *path) {
	DIR *d = opendir(path);
	if (!d) return;
	ntracks = 0;
	struct dirent *de;
	while ((de = readdir(d)) && ntracks < MAX_TRACKS) {
		if (!is_music(de->d_name)) continue;
		Track *t = &tracks[ntracks++];
		snprintf(t->path, sizeof(t->path), "%s/%s", path, de->d_name);
		strncpy(t->name, de->d_name, sizeof(t->name) - 1);
	}
	closedir(d);
	qsort(tracks, ntracks, sizeof(Track), trackcmp);
}

static void stop_playback(void) {
	if (ffpid > 0) {
		kill(ffpid, SIGTERM);
		waitpid(ffpid, NULL, 0);
		ffpid = -1;
	}
	paused = 0;
	play_start = 0;
	paused_elapsed = 0;
}

static void play_track(int idx) {
	if (idx < 0 || idx >= ntracks) return;
	stop_playback();
	playing = idx;
	ffpid = fork();
	if (ffpid == 0) {
		/* child: redirect stdout/stderr to /dev/null */
		int devnull = open("/dev/null", O_WRONLY);
		dup2(devnull, STDOUT_FILENO);
		dup2(devnull, STDERR_FILENO);
		execlp("ffplay", "ffplay", "-nodisp", "-loglevel", "quiet",
		       "-autoexit", tracks[idx].path, NULL);
		_exit(1);
	}
	play_start = time(NULL);
	paused_elapsed = 0;
	paused = 0;
}

static void toggle_pause(void) {
	if (ffpid <= 0) return;
	if (paused) {
		kill(ffpid, SIGCONT);
		play_start = time(NULL) - paused_elapsed;
		paused = 0;
	} else {
		kill(ffpid, SIGSTOP);
		paused_elapsed = time(NULL) - play_start;
		paused = 1;
	}
}

static long elapsed_sec(void) {
	if (play_start == 0) return 0;
	if (paused) return paused_elapsed;
	return (long)(time(NULL) - play_start);
}

static int check_finished(void) {
	if (ffpid <= 0) return 0;
	int status;
	pid_t r = waitpid(ffpid, &status, WNOHANG);
	if (r == ffpid) { ffpid = -1; return 1; }
	return 0;
}

static void fmt_time(long s, char *buf, size_t n) {
	snprintf(buf, n, "%ld:%02ld", s / 60, s % 60);
}

static void draw(void) {
	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	int lh = rows - 3;

	if (sel < 0) sel = 0;
	if (sel >= ntracks && ntracks > 0) sel = ntracks - 1;
	if (sel < vscroll) vscroll = sel;
	if (sel >= vscroll + lh) vscroll = sel - lh + 1;

	/* top bar */
	attron(COLOR_PAIR(C_BAR) | A_BOLD);
	mvhline(0, 0, ' ', cols);
	mvprintw(0, 1, " chimusic  [%d tracks]  space:pause  n:next  b:prev  q:quit", ntracks);
	attroff(COLOR_PAIR(C_BAR) | A_BOLD);

	/* track list */
	for (int i = 0; i < lh; i++) {
		int idx = vscroll + i;
		move(i + 1, 0); clrtoeol();
		if (idx >= ntracks) continue;

		if (idx == playing) attron(COLOR_PAIR(C_PLAY) | A_BOLD);
		else if (idx == sel) attron(COLOR_PAIR(C_SEL) | A_BOLD);
		else                 attron(COLOR_PAIR(C_DIM));

		mvhline(i + 1, 0, ' ', cols);
		char prefix = (idx == playing) ? (paused ? '|' : '>') : ' ';
		mvprintw(i + 1, 1, "%c %-*s", prefix, cols - 4, tracks[idx].name);

		attroff(COLOR_PAIR(C_PLAY) | COLOR_PAIR(C_SEL) | COLOR_PAIR(C_DIM) | A_BOLD);
	}

	/* now playing bar */
	attron(COLOR_PAIR(C_BAR));
	mvhline(rows - 2, 0, ' ', cols);
	if (playing >= 0) {
		char elapsed[16];
		fmt_time(elapsed_sec(), elapsed, sizeof(elapsed));
		mvprintw(rows - 2, 1, " %s  %s  %s",
			paused ? "[paused]" : "[playing]",
			elapsed,
			tracks[playing].name);
	} else {
		mvprintw(rows - 2, 1, " [stopped]");
	}
	attroff(COLOR_PAIR(C_BAR));

	/* bottom hint */
	mvhline(rows - 1, 0, ' ', cols);
	attron(COLOR_PAIR(C_DIM));
	mvprintw(rows - 1, 1, " j/k:nav  Enter:play  space:pause  n:next  b:prev  r:repeat  q:quit");
	attroff(COLOR_PAIR(C_DIM));

	refresh();
}

int main(int argc, char *argv[]) {
	const char *dir = argc > 1 ? argv[1] : ".";
	load_dir(dir);

	initscr(); noecho(); cbreak();
	keypad(stdscr, TRUE); curs_set(0);
	nodelay(stdscr, TRUE);
	start_color(); use_default_colors();

	init_pair(C_BAR,  COLOR_WHITE,   COLOR_MAGENTA);
	init_pair(C_SEL,  COLOR_WHITE,   COLOR_MAGENTA);
	init_pair(C_PLAY, COLOR_MAGENTA, -1);
	init_pair(C_DIM,  -1,            -1);

	int ch;
	while (1) {
		/* auto-advance */
		if (playing >= 0 && !paused && check_finished()) {
			int next = playing + 1;
			if (next < ntracks) play_track(next);
			else { playing = -1; ffpid = -1; }
		}

		draw();
		ch = getch();
		switch (ch) {
		case 'q': stop_playback(); endwin(); return 0;
		case 'j': case KEY_DOWN: sel++; break;
		case 'k': case KEY_UP:   sel--; break;
		case 'g': sel = 0; break;
		case 'G': sel = ntracks - 1; break;
		case '\n': case KEY_ENTER: case 'l':
			play_track(sel); break;
		case ' ': toggle_pause(); break;
		case 'n':
			if (playing >= 0 && playing + 1 < ntracks)
				play_track(playing + 1);
			break;
		case 'b':
			if (playing > 0) play_track(playing - 1);
			break;
		case 's': stop_playback(); playing = -1; break;
		case KEY_RESIZE: clear(); break;
		}

		struct timespec ts = {0, 200000000L}; /* 200ms */
		nanosleep(&ts, NULL);
	}
}
