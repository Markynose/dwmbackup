#!/bin/sh
# build-chi.sh — build and install all chi* tools for Gentoo (femboy)
# Run as root

set -e

DWM_CFG="/home/mark/dwm"
OT="${DWM_CFG}/otherthings"

RED='\033[1;31m'
GRN='\033[1;32m'
PNK='\033[1;35m'
RST='\033[0m'

die() { printf "${RED}error:${RST} %s\n" "$*" >&2; exit 1; }
ok()  { printf "${GRN}ok:${RST} %s\n" "$*"; }
hdr() { printf "${PNK}==> %s${RST}\n" "$*"; }
pkill_bin() { pkill "$1" 2>/dev/null || true; }

[ "$(id -u)" -eq 0 ] || die "run as root"

# ─── DWM ──────────────────────────────────────────────────────────────────────
hdr "Building dwm..."
cd "${DWM_CFG}"

# download and apply patches if not already patched
if ! grep -q 'spiral' dwm.c 2>/dev/null; then
	curl -O https://dwm.suckless.org/patches/fibonacci/dwm-fibonacci-20200418-76465d9.diff
	curl -O https://dwm.suckless.org/patches/scratchpad/dwm-scratchpad-20221102-ba56fe9.diff
	curl -O https://dwm.suckless.org/patches/push/dwm-push-20201112-61bb8b2.diff
	curl -O https://dwm.suckless.org/patches/restartsig/dwm-restartsig-20180523-6.2.diff
	patch -p1 < dwm-fibonacci-20200418-76465d9.diff   || true
	patch -p1 < dwm-scratchpad-20221102-ba56fe9.diff  || true
	patch -p1 < dwm-push-20201112-61bb8b2.diff        || true
	patch -p1 < dwm-restartsig-20180523-6.2.diff      || true
fi

make && make install && ok "dwm installed"

# ─── ST ───────────────────────────────────────────────────────────────────────
hdr "Building st..."
ST_SRC="/home/mark/st"
rm -rf "${ST_SRC}"
git clone https://git.suckless.org/st "${ST_SRC}"
cd "${ST_SRC}"
curl -O https://st.suckless.org/patches/scrollback/st-scrollback-0.9.2.diff
patch -p1 < st-scrollback-0.9.2.diff || true
cp "${OT}/st/config.h" "${ST_SRC}/config.h"
make && make install && ok "st installed"

# ─── DMENU ────────────────────────────────────────────────────────────────────
hdr "Building dmenu..."
DMENU_SRC="/home/mark/dmenu"
rm -rf "${DMENU_SRC}"
git clone https://git.suckless.org/dmenu "${DMENU_SRC}"
cp "${OT}/dmenu/config.h" "${DMENU_SRC}/config.h"
cd "${DMENU_SRC}" && make && make install && ok "dmenu installed"

# ─── CHITOOLS ITSELF ──────────────────────────────────────────────────────────
hdr "Installing chitools..."
cp "${OT}/chitools" /usr/local/bin/chitools
chmod +x /usr/local/bin/chitools
ok "chitools installed"

# ─── C TOOLS ──────────────────────────────────────────────────────────────────
hdr "Building C tools..."

cd "${OT}/fetch" && make && make install && ok "chifetch"
cc -O2 -Wall -o /usr/local/bin/chiview     "${OT}/chiview.c"     -lX11 -lImlib2  && ok "chiview"
cc -O2 -Wall -o /usr/local/bin/chitop      "${OT}/chitop.c"      -lncurses        && ok "chitop"
cc -O2 -Wall -o /usr/local/bin/chifm       "${OT}/chifm.c"       -lncurses        && ok "chifm"
cc -O2 -Wall -o /usr/local/bin/chikblayout "${OT}/chikblayout.c" -lX11            && ok "chikblayout"
cc -O2 -Wall -o /usr/local/bin/chioverview "${OT}/chioverview.c" -lX11            && ok "chioverview"
cc -O2 -Wall -o /usr/local/bin/chimusic    "${OT}/chimusic.c"    -lncurses        && ok "chimusic"
cc -O2 -Wall -o /usr/local/bin/chied       "${OT}/chied.c"                        && ok "chied"
cc -O2 -Wall -o /usr/local/bin/chiwallpaper "${OT}/chiwallpaper.c" -lX11 -lImlib2 && ok "chiwallpaper"
cc -O2 -Wall -o /usr/local/bin/chilock     "${OT}/chilock.c"     -lX11 -lXft -lcrypt && ok "chilock"
cc -O2 -Wall -o /usr/local/bin/chiidle     "${OT}/chiidle.c"     -lX11 -lXss     && ok "chiidle"
cc -O2 -Wall -o /usr/local/bin/chisniff    "${OT}/chisniff.c"    -lncurses -lpcap && ok "chisniff"
cc -O2 -Wall -o /usr/local/bin/chibar      "${OT}/chibar.c"      -lX11 -lXft     && ok "chibar"

# ─── SHELL SCRIPTS ────────────────────────────────────────────────────────────
hdr "Installing shell scripts..."
for f in dwmstatus chikeys chicalc chiadvcalc chivid chilauncher chiclip \
          chitodo chiman chitimer chilayout chidisp chiresume chirun chiproc; do
	[ -f "${OT}/${f}" ] || { printf "  skip: ${f} not found\n"; continue; }
	cp "${OT}/${f}" /usr/local/bin/${f}
	chmod +x /usr/local/bin/${f}
	ok "${f}"
done

# ─── CHIQEMU (RUST) ───────────────────────────────────────────────────────────
hdr "Building chiqemu (Rust)..."
if [ -d "${OT}/chiqemu" ]; then
	pkill_bin chiqemu
	cd "${OT}/chiqemu"
	su mark -c 'cd '"${OT}/chiqemu"' && cargo build --release'
	cp "${OT}/chiqemu/target/release/chiqemu" /usr/local/bin/chiqemu && ok "chiqemu"
else
	printf "  skip: chiqemu dir not found\n"
fi

echo ""
ok "All chi* tools built and installed."
