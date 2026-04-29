#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <pwd.h>

#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define PINK    "\033[38;2;201;71;155m"
#define LPINK   "\033[38;2;240;184;208m"
#define NPINK   "\033[38;2;255;121;198m"
#define DIM     "\033[38;2;61;43;69m"

static void get_os(char *buf, size_t n)
{
	FILE *f = fopen("/etc/os-release", "r");
	if (!f) { snprintf(buf, n, "unknown"); return; }
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
			char *v = line + 12;
			if (*v == '"') v++;
			v[strcspn(v, "\"\n")] = 0;
			snprintf(buf, n, "%s", v);
			fclose(f);
			return;
		}
	}
	fclose(f);
	snprintf(buf, n, "unknown");
}

static void get_cpu(char *buf, size_t n)
{
	FILE *f = fopen("/proc/cpuinfo", "r");
	if (!f) { snprintf(buf, n, "unknown"); return; }
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "model name", 10) == 0) {
			char *v = strchr(line, ':');
			if (!v) continue;
			v += 2;
			v[strcspn(v, "\n")] = 0;
			snprintf(buf, n, "%s", v);
			fclose(f);
			return;
		}
	}
	fclose(f);
	snprintf(buf, n, "unknown");
}

static void get_mem(char *buf, size_t n)
{
	struct sysinfo si;
	if (sysinfo(&si) != 0) { snprintf(buf, n, "unknown"); return; }
	long used  = (si.totalram - si.freeram - si.bufferram) * si.mem_unit / 1024 / 1024;
	long total = si.totalram * si.mem_unit / 1024 / 1024;
	snprintf(buf, n, "%ld / %ld MB", used, total);
}

static void get_uptime(char *buf, size_t n)
{
	FILE *f = fopen("/proc/uptime", "r");
	if (!f) { snprintf(buf, n, "unknown"); return; }
	double up;
	fscanf(f, "%lf", &up);
	fclose(f);
	int h = (int)up / 3600;
	int m = ((int)up % 3600) / 60;
	if (h > 0)
		snprintf(buf, n, "%dh %dm", h, m);
	else
		snprintf(buf, n, "%dm", m);
}

static void get_disk(char *buf, size_t n)
{
	struct statvfs st;
	if (statvfs("/", &st) != 0) { snprintf(buf, n, "unknown"); return; }
	unsigned long used  = (st.f_blocks - st.f_bfree) * st.f_frsize / 1024 / 1024 / 1024;
	unsigned long total = st.f_blocks * st.f_frsize / 1024 / 1024 / 1024;
	snprintf(buf, n, "%lu / %lu GB", used, total);
}

static void get_bat(char *buf, size_t n)
{
	const char *bats[] = { "BAT0", "BAT1", NULL };
	int total = 0, count = 0;
	char charging = ' ';
	char path[128], tmp[32];
	for (int i = 0; bats[i]; i++) {
		snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity", bats[i]);
		FILE *f = fopen(path, "r");
		if (!f) continue;
		int cap = 0;
		fscanf(f, "%d", &cap);
		fclose(f);
		total += cap;
		count++;
		snprintf(path, sizeof(path), "/sys/class/power_supply/%s/status", bats[i]);
		f = fopen(path, "r");
		if (f) {
			fgets(tmp, sizeof(tmp), f);
			fclose(f);
			if (strncmp(tmp, "Charging", 8) == 0) charging = '+';
		}
	}
	if (count == 0) { snprintf(buf, n, "n/a"); return; }
	snprintf(buf, n, "%d%%%c", total / count, charging == '+' ? '+' : '\0');
	if (charging != '+') buf[strlen(buf) - 1] = '\0'; /* remove trailing null char */
}

static void get_pkgs(char *buf, size_t n)
{
	FILE *f = popen("apk info 2>/dev/null | wc -l", "r");
	if (!f) { snprintf(buf, n, "unknown"); return; }
	int count = 0;
	fscanf(f, "%d", &count);
	pclose(f);
	snprintf(buf, n, "%d (apk)", count);
}

static void get_res(char *buf, size_t n)
{
	FILE *f = popen("xrandr 2>/dev/null | awk '/\\*/{print $1; exit}'", "r");
	if (!f) { snprintf(buf, n, "unknown"); return; }
	if (!fgets(buf, (int)n, f) || buf[0] == '\0')
		snprintf(buf, n, "unknown");
	else
		buf[strcspn(buf, "\n")] = 0;
	pclose(f);
}

static void row(const char *label, const char *value)
{
	printf("  %s%-10s%s  %s%s%s\n",
		PINK, label, RESET,
		LPINK, value, RESET);
}

int main(void)
{
	struct utsname u;
	uname(&u);

	char os[128], cpu[128], mem[64], uptime[32];
	char host[64], shell[64], user[64];
	char disk[32], bat[16], pkgs[32], res[32];

	get_os(os, sizeof(os));
	get_cpu(cpu, sizeof(cpu));
	get_mem(mem, sizeof(mem));
	get_uptime(uptime, sizeof(uptime));
	get_disk(disk, sizeof(disk));
	get_bat(bat, sizeof(bat));
	get_pkgs(pkgs, sizeof(pkgs));
	get_res(res, sizeof(res));

	gethostname(host, sizeof(host));

	struct passwd *pw = getpwuid(getuid());
	snprintf(user, sizeof(user), "%s", pw ? pw->pw_name : "unknown");

	char *sh = getenv("SHELL");
	if (sh) {
		char *base = strrchr(sh, '/');
		snprintf(shell, sizeof(shell), "%s", base ? base + 1 : sh);
	} else {
		snprintf(shell, sizeof(shell), "unknown");
	}

	printf("\n");
	printf("  %s%s%s@%s%s%s\n", NPINK, user, DIM, NPINK, host, RESET);
	printf("  %s────────────────────────%s\n", DIM, RESET);
	row("os",      os);
	row("kernel",  u.release);
	row("uptime",  uptime);
	row("cpu",     cpu);
	row("memory",  mem);
	row("disk",    disk);
	row("battery", bat);
	row("pkgs",    pkgs);
	row("res",     res);
	row("shell",   shell);
	row("wm",      "dwm");
	printf("  %s────────────────────────%s\n", DIM, RESET);

	printf("  ");
	const char *swatches[] = {
		"\033[38;2;30;18;36m",
		"\033[38;2;138;74;158m",
		"\033[38;2;176;96;144m",
		"\033[38;2;201;71;155m",
		"\033[38;2;208;64;160m",
		"\033[38;2;184;112;200m",
		"\033[38;2;240;184;208m",
		"\033[38;2;255;121;198m",
	};
	for (int i = 0; i < 8; i++)
		printf("%s●%s ", swatches[i], RESET);
	printf("\n\n");

	return 0;
}
