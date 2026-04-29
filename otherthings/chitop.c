#define _POSIX_C_SOURCE 200809L
#include <ncurses.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/sysinfo.h>

#define MAX_PROCS 1024
#define C_BAR  1
#define C_HDR  2
#define C_HIGH 3
#define C_MED  4

typedef struct {
	int   pid;
	char  name[64];
	long  rss_kb;
	long  cpu_j;
	float cpu;
} Proc;

static Proc procs[MAX_PROCS];
static int  nprocs, sort_by, vscroll;

static int read_procs(Proc *out, int max) {
	DIR *d = opendir("/proc");
	if (!d) return 0;
	int n = 0;
	struct dirent *de;
	while ((de = readdir(d)) && n < max) {
		int pid = atoi(de->d_name);
		if (pid <= 0) continue;

		char path[64];
		snprintf(path, sizeof(path), "/proc/%d/comm", pid);
		FILE *f = fopen(path, "r");
		if (!f) continue;
		char name[64] = {0};
		fgets(name, sizeof(name), f);
		fclose(f);
		name[strcspn(name, "\n")] = 0;

		snprintf(path, sizeof(path), "/proc/%d/stat", pid);
		f = fopen(path, "r");
		if (!f) continue;
		long utime = 0, stime = 0;
		fscanf(f, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %ld %ld",
		       &utime, &stime);
		fclose(f);

		snprintf(path, sizeof(path), "/proc/%d/statm", pid);
		f = fopen(path, "r");
		long rss_pages = 0;
		if (f) { fscanf(f, "%*ld %ld", &rss_pages); fclose(f); }

		out[n].pid    = pid;
		strncpy(out[n].name, name, 63);
		out[n].cpu_j  = utime + stime;
		out[n].rss_kb = rss_pages * (sysconf(_SC_PAGESIZE) / 1024);
		out[n].cpu    = 0.0f;
		n++;
	}
	closedir(d);
	return n;
}

static int cmp_cpu(const void *a, const void *b) {
	float d = ((Proc*)b)->cpu - ((Proc*)a)->cpu;
	return d > 0 ? 1 : d < 0 ? -1 : 0;
}
static int cmp_mem(const void *a, const void *b) {
	return (int)(((Proc*)b)->rss_kb - ((Proc*)a)->rss_kb);
}

static void fmt_mem(long kb, char *buf, size_t n) {
	if      (kb >= 1024*1024) snprintf(buf, n, "%.1fG", (double)kb/1024/1024);
	else if (kb >= 1024)      snprintf(buf, n, "%.1fM", (double)kb/1024);
	else                      snprintf(buf, n, "%ldK",  kb);
}

static void draw(int rows, int cols) {
	/* top bar */
	struct sysinfo si;
	sysinfo(&si);
	long used_mb  = (si.totalram - si.freeram) * si.mem_unit / 1024 / 1024;
	long total_mb = si.totalram * si.mem_unit / 1024 / 1024;

	attron(COLOR_PAIR(C_BAR) | A_BOLD);
	mvhline(0, 0, ' ', cols);
	mvprintw(0, 1, " chitop  mem:%ld/%ldMB  procs:%d  [%s]  c:cpu  m:mem  q:quit",
		used_mb, total_mb, nprocs, sort_by ? "MEM" : "CPU");
	attroff(COLOR_PAIR(C_BAR) | A_BOLD);

	/* column headers */
	attron(COLOR_PAIR(C_HDR) | A_BOLD);
	mvhline(1, 0, ' ', cols);
	mvprintw(1, 1, "%-6s %-24s %6s %8s", "PID", "NAME", "CPU%", "MEM");
	attroff(COLOR_PAIR(C_HDR) | A_BOLD);

	int lh = rows - 2;
	if (vscroll < 0) vscroll = 0;
	if (nprocs > lh && vscroll > nprocs - lh) vscroll = nprocs - lh;

	for (int i = 0; i < lh; i++) {
		int idx = vscroll + i;
		move(i + 2, 0); clrtoeol();
		if (idx >= nprocs) continue;
		Proc *p = &procs[idx];

		char mem[16];
		fmt_mem(p->rss_kb, mem, sizeof(mem));

		if      (p->cpu > 50.0f) attron(COLOR_PAIR(C_HIGH) | A_BOLD);
		else if (p->cpu > 15.0f) attron(COLOR_PAIR(C_MED));

		mvprintw(i + 2, 1, "%-6d %-24s %5.1f%% %8s",
			p->pid, p->name, p->cpu, mem);

		attroff(COLOR_PAIR(C_HIGH) | COLOR_PAIR(C_MED) | A_BOLD);
	}
	refresh();
}

int main(void) {
	initscr(); noecho(); cbreak();
	keypad(stdscr, TRUE); curs_set(0);
	nodelay(stdscr, TRUE);
	start_color(); use_default_colors();

	init_pair(C_BAR,  COLOR_WHITE,   COLOR_MAGENTA);
	init_pair(C_HDR,  COLOR_MAGENTA, -1);
	init_pair(C_HIGH, COLOR_WHITE,   COLOR_RED);
	init_pair(C_MED,  COLOR_YELLOW,  -1);

	long hz = sysconf(_SC_CLK_TCK);
	Proc prev[MAX_PROCS];
	int  nprev = read_procs(prev, MAX_PROCS);
	int  rows, cols;

	while (1) {
		struct timespec ts = {0, 500000000L};
		nanosleep(&ts, NULL);

		nprocs = read_procs(procs, MAX_PROCS);
		getmaxyx(stdscr, rows, cols);

		for (int i = 0; i < nprocs; i++) {
			for (int j = 0; j < nprev; j++) {
				if (procs[i].pid == prev[j].pid) {
					long d = procs[i].cpu_j - prev[j].cpu_j;
					procs[i].cpu = (float)d / (float)hz * 2.0f * 100.0f;
					break;
				}
			}
		}

		qsort(procs, nprocs, sizeof(Proc), sort_by ? cmp_mem : cmp_cpu);
		memcpy(prev, procs, nprocs * sizeof(Proc));
		nprev = nprocs;

		draw(rows, cols);

		int ch = getch();
		switch (ch) {
		case 'q':                endwin(); return 0;
		case 'c': sort_by = 0; vscroll = 0; break;
		case 'm': sort_by = 1; vscroll = 0; break;
		case 'j': case KEY_DOWN: vscroll++; break;
		case 'k': case KEY_UP:   if (vscroll > 0) vscroll--; break;
		case 'g':                vscroll = 0; break;
		case 'G':                vscroll = nprocs; break;
		case KEY_RESIZE:         clear(); break;
		}
	}
}
