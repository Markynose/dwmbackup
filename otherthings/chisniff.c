/* chisniff.c — ncurses packet sniffer; normal (IP) or 802.11 monitor mode */
#define _BSD_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <pcap.h>
#include <ncurses.h>

#define MAX_PKTS   1000
#define MAX_IFACES 16
#define IFACE_LEN  32
#define ADDR_LEN   20
#define INFO_LEN   60

typedef enum { MODE_NORMAL, MODE_MONITOR } CaptureMode;

typedef struct {
	char time[10];
	char src[ADDR_LEN];
	char dst[ADDR_LEN];
	char proto[8];
	int  len;
	char info[INFO_LEN];
} Pkt;

static Pkt       pkts[MAX_PKTS];
static int       npkts      = 0;
static int       vscroll    = 0;
static int       paused     = 0;
static int       autofollow = 1;
static int       running    = 1;
static pcap_t   *handle     = NULL;
static CaptureMode capmode;
static char      cur_if[IFACE_LEN];
static int       cur_chan   = 1;
static int       chan_hop   = 1; /* auto channel hop in monitor mode */

static int chans[] = {1,6,11,2,7,3,8,4,9,5,10,12,13};
#define NCHAN ((int)(sizeof(chans)/sizeof(chans[0])))

enum { CP_NORM=1, CP_HEAD, CP_ACC, CP_DIM, CP_TCP, CP_UDP, CP_ICMP, CP_MGMT };

static void init_colors(void) {
	start_color();
	use_default_colors();
	init_pair(CP_NORM, COLOR_WHITE,   -1);
	init_pair(CP_HEAD, COLOR_MAGENTA, -1);
	init_pair(CP_ACC,  COLOR_MAGENTA, -1);
	init_pair(CP_DIM,  COLOR_WHITE,   -1);
	init_pair(CP_TCP,  COLOR_CYAN,    -1);
	init_pair(CP_UDP,  COLOR_GREEN,   -1);
	init_pair(CP_ICMP, COLOR_YELLOW,  -1);
	init_pair(CP_MGMT, COLOR_MAGENTA, -1);
}

static void add_pkt(const char *src, const char *dst,
                    const char *proto, int len, const char *info) {
	if (paused) return;
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	Pkt *p = &pkts[npkts % MAX_PKTS];
	strftime(p->time, sizeof(p->time), "%H:%M:%S", tm);
	snprintf(p->src,   ADDR_LEN,        "%s", src);
	snprintf(p->dst,   ADDR_LEN,        "%s", dst);
	snprintf(p->proto, sizeof(p->proto),"%s", proto);
	p->len = len;
	snprintf(p->info,  INFO_LEN,        "%s", info);
	npkts++;
}

/* --- Normal mode: Ethernet → IPv4 → TCP/UDP/ICMP --- */
static void cb_normal(u_char *u, const struct pcap_pkthdr *h, const u_char *pkt) {
	(void)u;
	if (h->caplen < 14) return;
	uint16_t etype = (uint16_t)((pkt[12] << 8) | pkt[13]);
	if (etype != 0x0800) return; /* IPv4 only */

	const u_char *ip = pkt + 14;
	int rem = (int)h->caplen - 14;
	if (rem < 20) return;

	int hl = (ip[0] & 0xf) * 4;
	if (rem < hl) return;
	uint8_t  pn   = ip[9];
	uint16_t tlen = (uint16_t)((ip[2] << 8) | ip[3]);
	uint32_t sa, da;
	memcpy(&sa, ip + 12, 4);
	memcpy(&da, ip + 16, 4);
	char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &sa, src, sizeof(src));
	inet_ntop(AF_INET, &da, dst, sizeof(dst));

	const u_char *pay = ip + hl;
	int prem = rem - hl;
	char proto[8], info[INFO_LEN] = "";

	if (pn == 6 && prem >= 20) {          /* TCP */
		snprintf(proto, sizeof(proto), "TCP");
		uint16_t sp = (uint16_t)((pay[0]<<8)|pay[1]);
		uint16_t dp = (uint16_t)((pay[2]<<8)|pay[3]);
		uint8_t  fl = pay[13];
		char fs[8] = "";
		if (fl & 0x02) strcat(fs, "S");
		if (fl & 0x10) strcat(fs, "A");
		if (fl & 0x08) strcat(fs, "P");
		if (fl & 0x01) strcat(fs, "F");
		if (fl & 0x04) strcat(fs, "R");
		snprintf(info, INFO_LEN, "%u->%u [%s]", sp, dp, fs);
	} else if (pn == 17 && prem >= 8) {   /* UDP */
		snprintf(proto, sizeof(proto), "UDP");
		uint16_t sp = (uint16_t)((pay[0]<<8)|pay[1]);
		uint16_t dp = (uint16_t)((pay[2]<<8)|pay[3]);
		snprintf(info, INFO_LEN, "%u->%u", sp, dp);
	} else if (pn == 1 && prem >= 4) {    /* ICMP */
		snprintf(proto, sizeof(proto), "ICMP");
		snprintf(info, INFO_LEN, "type=%u code=%u", pay[0], pay[1]);
	} else {
		snprintf(proto, sizeof(proto), "IP/%u", pn);
	}
	add_pkt(src, dst, proto, tlen, info);
}

/* --- Monitor mode: radiotap + 802.11 --- */
static void mac_str(const u_char *m, char *out, int sz) {
	snprintf(out, sz, "%02x:%02x:%02x:%02x:%02x:%02x",
	         m[0], m[1], m[2], m[3], m[4], m[5]);
}

/* Scan a byte window for ASCII runs; append any run >= minrun chars to info */
static void ascii_hint(const u_char *m, int len, int minrun, char *info) {
	char run[64];
	int  ri = 0;
	for (int i = 0; i <= len; i++) {
		if (i < len && m[i] >= 32 && m[i] < 127) {
			if (ri < (int)sizeof(run) - 1)
				run[ri++] = (char)m[i];
		} else {
			if (ri >= minrun) {
				run[ri] = '\0';
				int cur = (int)strlen(info);
				snprintf(info + cur, INFO_LEN - cur, " [\"%s\"]", run);
			}
			ri = 0;
		}
	}
}

static const char *mgmt_sub[] = {
	"AReq","ARes","RReq","RRes","PRReq","PRRes","TmAdv","?",
	"Bcn","ATIM","DiAs","Auth","DeAuth","Act","ActNA","?"
};

static void parse_ssid_tags(const u_char *tags, int trem,
                             const char *label, char *info) {
	while (trem >= 2) {
		uint8_t id = tags[0], tl = tags[1];
		if (trem < 2 + tl) break;
		if (id == 0) {
			char ssid[33] = {0};
			if (tl > 0) memcpy(ssid, tags + 2, tl < 32 ? tl : 32);
			snprintf(info, INFO_LEN, "%s SSID=\"%s\"",
			         label, tl ? ssid : "(any)");
			return;
		}
		tags += 2 + tl;
		trem -= 2 + tl;
	}
}

static void cb_monitor(u_char *u, const struct pcap_pkthdr *h, const u_char *pkt) {
	(void)u;
	if (h->caplen < 4) return;

	/* Radiotap header: version(1) pad(1) len(2-LE) */
	uint16_t rtlen;
	memcpy(&rtlen, pkt + 2, 2); /* x86 is LE, matches 802.11 on-wire LE */
	if (rtlen > h->caplen) return;

	const u_char *f = pkt + rtlen;
	int rem = (int)h->caplen - rtlen;
	if (rem < 10) return;

	int type = (f[0] >> 2) & 0x3;
	int sub  = (f[0] >> 4) & 0xf;
	char src[18] = "?", dst[18] = "?";
	char proto[8], info[INFO_LEN] = "";

	/* addr1=dst@4, addr2=src@10 (most frame types) */
	if (rem >= 16) {
		mac_str(f + 4,  dst, sizeof(dst));
		mac_str(f + 10, src, sizeof(src));
	}

	if (type == 0) {  /* Management */
		snprintf(proto, sizeof(proto), "MGMT");
		const char *sname = (sub < 16) ? mgmt_sub[sub] : "?";
		snprintf(info, INFO_LEN, "%s", sname);

		/* Beacon/Probe Response: 24B header + 12B fixed params = 36 */
		if ((sub == 8 || sub == 5) && rem > 36)
			parse_ssid_tags(f + 36, rem - 36, sname, info);
		/* Probe Request / Assoc Request: 24B header + 4B fixed = 28 */
		else if ((sub == 4 || sub == 0) && rem > 28)
			parse_ssid_tags(f + 28, rem - 28, sname, info);
		/* Auth / Assoc Resp: just show subtype */

	} else if (type == 1) {  /* Control */
		snprintf(proto, sizeof(proto), "CTRL");
		snprintf(info, INFO_LEN, "sub=%u", sub);
	} else {                 /* Data */
		snprintf(proto, sizeof(proto), "DATA");
		snprintf(info, INFO_LEN, "sub=%u", sub);
	}
	if (rem >= 22) /* addr1+addr2+addr3 = 18 bytes starting at offset 4 */
		ascii_hint(f + 4, 18, 4, info);
	add_pkt(src, dst, proto, (int)h->len, info);
}

/* --- UI --- */
static void draw(void) {
	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	erase();

	attron(COLOR_PAIR(CP_HEAD) | A_BOLD);
	mvprintw(0, 0, " chisniff ");
	attroff(A_BOLD);
	if (capmode == MODE_MONITOR)
		printw("- %s [MONITOR ch%d%s] - %d pkts%s",
		       cur_if, cur_chan, chan_hop ? " hop" : "",
		       npkts, paused ? "  [PAUSED]" : "");
	else
		printw("- %s [NORMAL] - %d pkts%s",
		       cur_if, npkts, paused ? "  [PAUSED]" : "");
	attroff(COLOR_PAIR(CP_HEAD));

	attron(COLOR_PAIR(CP_ACC) | A_BOLD);
	mvprintw(1, 0, "%-9s %-18s %-18s %-6s %5s  %s",
	         "TIME", "SRC", "DST", "PROTO", "LEN", "INFO");
	attroff(COLOR_PAIR(CP_ACC) | A_BOLD);

	attron(COLOR_PAIR(CP_DIM) | A_DIM);
	mvhline(2, 0, ACS_HLINE, cols);
	attroff(COLOR_PAIR(CP_DIM) | A_DIM);

	int lr  = rows - 4;
	int tot = npkts < MAX_PKTS ? npkts : MAX_PKTS;
	if (autofollow && !paused && tot > lr) vscroll = tot - lr;
	if (vscroll > tot - lr && tot > lr) vscroll = tot - lr;
	if (vscroll < 0) vscroll = 0;

	for (int i = 0; i < lr && i + vscroll < tot; i++) {
		int idx = (npkts <= MAX_PKTS) ? vscroll + i
		          : (npkts - tot + vscroll + i) % MAX_PKTS;
		Pkt *p = &pkts[idx];
		int cp = CP_NORM;
		if      (!strcmp(p->proto, "TCP"))  cp = CP_TCP;
		else if (!strcmp(p->proto, "UDP"))  cp = CP_UDP;
		else if (!strcmp(p->proto, "ICMP")) cp = CP_ICMP;
		else if (!strcmp(p->proto, "MGMT")) cp = CP_MGMT;

		attron(COLOR_PAIR(cp));
		mvprintw(3 + i, 0, "%-9s %-18s %-18s %-6s %5d  %-.*s",
		         p->time, p->src, p->dst, p->proto, p->len,
		         cols - 62 > 0 ? cols - 62 : 1, p->info);
		attroff(COLOR_PAIR(cp));
	}

	attron(COLOR_PAIR(CP_DIM) | A_DIM);
	if (capmode == MODE_MONITOR)
		mvprintw(rows - 1, 0,
		         " q:quit  p:pause  c:clear  j/k:scroll  g/G:top/end"
		         "  h:hop(%s)  1-9:lock ch%s",
		         chan_hop ? "on" : "off", autofollow ? "  [follow]" : "");
	else
		mvprintw(rows - 1, 0,
		         " q:quit  p:pause  c:clear  j/k:scroll  g/G:top/end%s",
		         autofollow ? "  [follow]" : "");
	attroff(COLOR_PAIR(CP_DIM) | A_DIM);
	refresh();
}

static int do_menu(const char **items, int n, const char *title) {
	int sel = 0;
	for (;;) {
		int rows, cols;
		getmaxyx(stdscr, rows, cols);
		(void)cols;
		erase();
		attron(COLOR_PAIR(CP_HEAD) | A_BOLD);
		mvprintw(1, 2, "chisniff  -- %s", title);
		attroff(COLOR_PAIR(CP_HEAD) | A_BOLD);
		for (int i = 0; i < n; i++) {
			int attr = (i == sel)
			           ? (COLOR_PAIR(CP_ACC) | A_REVERSE | A_BOLD)
			           : COLOR_PAIR(CP_NORM);
			attron(attr);
			mvprintw(3 + i, 4, "  %s  ", items[i]);
			attroff(attr);
		}
		attron(COLOR_PAIR(CP_DIM) | A_DIM);
		mvprintw(rows - 1, 2, " j/k:navigate  Enter:select  q:quit ");
		attroff(COLOR_PAIR(CP_DIM) | A_DIM);
		refresh();

		int ch = getch();
		if (ch == 'q')                     return -1;
		if (ch == 'j' || ch == KEY_DOWN)   sel = (sel + 1) % n;
		if (ch == 'k' || ch == KEY_UP)     sel = (sel - 1 + n) % n;
		if (ch == '\n' || ch == KEY_ENTER) return sel;
	}
}

static void run_cmd(const char *fmt, const char *arg) {
	char cmd[160];
	snprintf(cmd, sizeof(cmd), fmt, arg);
	system(cmd);
}

static void set_channel(int ch) {
	char cmd[128];
	snprintf(cmd, sizeof(cmd),
	         "iw dev %s set channel %d 2>/dev/null", cur_if, ch);
	system(cmd);
	cur_chan = ch;
}

static void set_monitor_mode(const char *iface) {
	run_cmd("ip link set %s down 2>/dev/null",          iface);
	run_cmd("iw dev %s set type monitor 2>/dev/null",   iface);
	run_cmd("ip link set %s up 2>/dev/null",            iface);
}

static void restore_managed_mode(const char *iface) {
	run_cmd("ip link set %s down 2>/dev/null",          iface);
	run_cmd("iw dev %s set type managed 2>/dev/null",   iface);
	run_cmd("ip link set %s up 2>/dev/null",            iface);
}

static void cleanup(void) {
	if (handle) { pcap_breakloop(handle); pcap_close(handle); handle = NULL; }
	endwin();
	if (capmode == MODE_MONITOR) {
		restore_managed_mode(cur_if);
		fprintf(stderr, "Restored %s to managed mode.\n", cur_if);
	}
}

static void sighand(int s) { (void)s; running = 0; }

int main(int argc, char *argv[]) {
	char iface_arg[IFACE_LEN] = "";
	int  mode_arg = -1;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-i") && i + 1 < argc)
			snprintf(iface_arg, IFACE_LEN, "%s", argv[++i]);
		else if (!strcmp(argv[i], "-m")) mode_arg = MODE_MONITOR;
		else if (!strcmp(argv[i], "-n")) mode_arg = MODE_NORMAL;
		else if (!strcmp(argv[i], "-h")) {
			printf("usage: chisniff [-i iface] [-m|-n]\n"
			       "  -m  monitor mode (needs doas)\n"
			       "  -n  normal mode\n");
			return 0;
		}
	}

	signal(SIGINT,  sighand);
	signal(SIGTERM, sighand);

	initscr(); cbreak(); noecho();
	keypad(stdscr, TRUE); curs_set(0);
	init_colors();

	/* Interface selection */
	if (!iface_arg[0]) {
		char errbuf[PCAP_ERRBUF_SIZE];
		pcap_if_t *devs;
		if (pcap_findalldevs(&devs, errbuf) != 0) {
			endwin();
			fprintf(stderr, "pcap_findalldevs: %s\n", errbuf);
			return 1;
		}
		char       names[MAX_IFACES][IFACE_LEN];
		const char *ptrs[MAX_IFACES];
		int nif = 0;
		for (pcap_if_t *d = devs; d && nif < MAX_IFACES; d = d->next) {
			snprintf(names[nif], IFACE_LEN, "%s", d->name);
			ptrs[nif] = names[nif];
			nif++;
		}
		pcap_freealldevs(devs);
		int sel = do_menu(ptrs, nif, "- select interface");
		if (sel < 0) { endwin(); return 0; }
		snprintf(iface_arg, IFACE_LEN, "%s", ptrs[sel]);
	}
	snprintf(cur_if, IFACE_LEN, "%s", iface_arg);

	/* Mode selection */
	if (mode_arg < 0) {
		const char *modes[] = {
			"Normal  - capture own traffic (IPv4)",
			"Monitor - capture all 802.11 frames (needs doas)"
		};
		int sel = do_menu(modes, 2, "- select mode");
		if (sel < 0) { endwin(); return 0; }
		mode_arg = sel;
	}
	capmode = (CaptureMode)mode_arg;

	if (capmode == MODE_MONITOR && getuid() != 0) {
		endwin();
		fprintf(stderr, "Monitor mode requires root. Run: doas chisniff\n");
		return 1;
	}

	/* Set monitor mode (needs endwin/refresh to avoid terminal corruption) */
	if (capmode == MODE_MONITOR) {
		endwin();
		printf("Setting %s to monitor mode...\n", cur_if);
		fflush(stdout);
		set_monitor_mode(cur_if);
		sleep(1); /* wait for interface to come up */
		set_channel(chans[0]);
		refresh();
	}

	char errbuf[PCAP_ERRBUF_SIZE];
	handle = pcap_open_live(cur_if, 65535, 1, 1, errbuf);
	if (!handle) {
		if (capmode == MODE_MONITOR) restore_managed_mode(cur_if);
		endwin();
		fprintf(stderr, "pcap_open_live: %s\n", errbuf);
		return 1;
	}

	int dlt = pcap_datalink(handle);
	pcap_handler cb;
	if (capmode == MODE_MONITOR) {
		if (dlt != DLT_IEEE802_11_RADIO && dlt != DLT_IEEE802_11) {
			cleanup();
			fprintf(stderr, "Unexpected DLT %d for monitor mode\n", dlt);
			return 1;
		}
		cb = cb_monitor;
	} else {
		cb = cb_normal;
	}

	pcap_setnonblock(handle, 1, errbuf);
	nodelay(stdscr, TRUE);

	struct timespec slp = {0, 30000000L}; /* 30ms */
	int chan_idx   = 0;
	int hop_ticks  = 0;
	int hop_every  = 17; /* ~500ms at 30ms/tick */

	while (running) {
		pcap_dispatch(handle, 20, cb, NULL);

		/* Channel hop in monitor mode */
		if (capmode == MODE_MONITOR && chan_hop && !paused) {
			if (++hop_ticks >= hop_every) {
				hop_ticks = 0;
				chan_idx = (chan_idx + 1) % NCHAN;
				set_channel(chans[chan_idx]);
			}
		}

		int rows, cols;
		getmaxyx(stdscr, rows, cols);
		int lr  = rows - 4;
		int tot = npkts < MAX_PKTS ? npkts : MAX_PKTS;

		draw();

		int ch = getch();
		switch (ch) {
		case 'q': running = 0; break;
		case 'p': paused = !paused; break;
		case 'c': npkts = 0; vscroll = 0; break;
		case 'h':
			if (capmode == MODE_MONITOR) chan_hop = !chan_hop;
			break;
		default:
			if (capmode == MODE_MONITOR && ch >= '1' && ch <= '9') {
				int target = ch - '0';
				set_channel(target);
				/* find in hop list so hopping resumes from here */
				for (int i = 0; i < NCHAN; i++)
					if (chans[i] == target) { chan_idx = i; break; }
				chan_hop = 0; /* lock to chosen channel */
				hop_ticks = 0;
			}
			break;
		case 'j': case KEY_DOWN:
			autofollow = 0;
			if (vscroll < tot - lr) vscroll++;
			break;
		case 'k': case KEY_UP:
			autofollow = 0;
			if (vscroll > 0) vscroll--;
			break;
		case KEY_NPAGE:
			autofollow = 0;
			vscroll += lr;
			if (vscroll > tot - lr) vscroll = tot > lr ? tot - lr : 0;
			break;
		case KEY_PPAGE:
			autofollow = 0;
			vscroll -= lr;
			if (vscroll < 0) vscroll = 0;
			break;
		case 'G':
			autofollow = 1;
			break;
		case 'g':
			autofollow = 0;
			vscroll = 0;
			break;
		}
		(void)cols;
		nanosleep(&slp, NULL);
	}

	cleanup();
	return 0;
}
