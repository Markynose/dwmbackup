# dwm customization project

## Goal
Configure dwm for Mark's Artix Linux (dinit) setup. Build target: ThiccPad (T460, i5-6200U).
Uses Artix Linux (glibc, pacman package manager, dinit init system).

## Machine info
- ThiccPad: 192.168.0.92, user: mark, T460 i5-6200U 16GB
- Build tool: bmake (available via pacman), compiler: clang/cc
- Shell: oksh (/usr/local/bin/oksh) for mark only — never blanket sed /etc/passwd!
- Init: dinit (use `dinitctl` not systemctl); enable services via symlinks in /etc/dinit.d/boot.d/
- mark's groups: wheel, video, network, audio, kvm, plugdev
- doas: `permit nopass :wheel` in /etc/doas.conf (package: opendoas)

## Follow this workflow
edit config.h / patch dwm.c → bmake → test

## dwm.c patch workflow
- fibonacci.c must be `#include`d at the VERY END of dwm.c (after main()), with forward declarations added before config.h
- push patch hunk #1 often fails — add prevtiled/pushdown/pushup declarations manually

## Setup scripts
- `otherthings/setup.sh` — full fresh install setup (run as root after Artix install)
- `otherthings/setup-oksh.sh` — set oksh as mark's shell (run after setup.sh)

## Current state
- dwm: pink theme, Super key (Hyprland-style) keybinds; patches: scratchpad, fibonacci spiral ([@] default layout), push
- st: pink gradient, scrollback patches applied, JetBrainsMono Nerd Font
- dmenu: pink theme, built from source
- dwmstatus: wifi+bars | vol | brightness | battery | time; also handles `vol up/down/mute` and `bright up/down` subcommands
- chifetch: custom fetch tool in otherthings/fetch/
- chifm: custom TUI file manager (otherthings/chifm.c) — vim keys, pink theme, built with cc -lncurses; Super+E
- firefox: browser (Super+B)
- JetBrainsMono Nerd Font installed in ~/.local/share/fonts
- chitools: local management CLI (otherthings/chitools) — run as `doas chitools build all`; doas PATH doesn't include /usr/local/bin, use `doas /usr/local/bin/chitools`
- chikeys: keybind cheatsheet (otherthings/chikeys) — opens in st with less
- chicalc: dmenu bc calculator (otherthings/chicalc)
- chiadvcalc: python3 TUI calculator with readline history (otherthings/chiadvcalc)
- chilauncher: dmenu launcher with flatpak support (otherthings/chilauncher) — replaces dmenu_run; shows [F] prefix for flatpak apps
- chiclip: clipboard history daemon + dmenu picker (otherthings/chiclip) — daemon runs from xinitrc; Super+C to pick
- chiwallpaper: custom wallpaper setter (otherthings/chiwallpaper.c) — bg-fill mode; -g/--gif for animated GIF; needs imlib2-devel libx11-devel
- chiview: minimal image viewer (otherthings/chiview.c) — imlib2+Xlib; zoom (+/-/scroll), pan (drag/hjkl), fit(0), actual(1); built with cc -lX11 -lImlib2
- chivid: video player (otherthings/chivid) — ffplay wrapper; needs `pacman -S ffmpeg`
- chitop: process viewer (otherthings/chitop.c) — /proc-based CPU%+mem, two-snapshot; sort c/m; built with cc -lncurses
- chinotes: quick notes (otherthings/chinotes) — dmenu picker, notes in ~/.local/share/chinotes/notes; Super+N/Super+Shift+N
- dwmstatus: includes CPU temp from /sys/class/thermal; flashes `!!! Xc° !!!` when ≥80°C; shows todo/timer/kblayout
- chitodo: quick todo list (otherthings/chitodo) — dmenu; `chitodo add/done`; shows in dwmstatus when non-empty
- chiman: man page picker via dmenu (otherthings/chiman) — apropos-based; opens in st; Super+Shift+M
- chitimer: countdown timer (otherthings/chitimer) — writes to /tmp/chitimer; shows in dwmstatus; `chitimer <mins>`
- chied: minimal vi-clone editor (otherthings/chied.c) — normal/insert, hjkl, dd/yy/p, :w/:q/:wq; built with cc
- chimusic: TUI music player (otherthings/chimusic.c) — ffplay backend, flac/mp3/ogg; j/k/space/n/b; cc -lncurses
- chiqemu: QEMU TUI manager (otherthings/chiqemu/) — Rust/ratatui; pink theme; VM configs in ~/.local/share/chiqemu/*.json; j/k nav, Enter start, s stop, n new, e edit, d delete, r reload, q quit; requires `doas ln -s /usr/lib/libgcc_s.so.1 /usr/lib/libgcc_s.so` once for Rust linker
- chilock: X11 screen locker (otherthings/chilock.c) — fullscreen pink theme, shadow auth; run as `doas env XAUTHORITY="$XAUTHORITY" chilock`; Super+Shift+L keybind; built with cc -lX11 -lXft -lcrypt + pkgconf xft flags
- chiidle: idle timeout daemon (otherthings/chiidle.c) — locks after 5min idle via XScreenSaver; `chiidle [-t minutes]`; built with cc -lX11 -lXss; starts from xinitrc
- chiresume: suspend/wake locker (otherthings/chiresume) — dbus-monitor script; locks before suspend so screen is locked on wake; starts from xinitrc
- chisniff: ncurses packet sniffer (otherthings/chisniff.c) — normal (IPv4) or 802.11 monitor mode; needs libpcap-devel + iw; built with cc -lncurses -lpcap; normal mode no root needed, monitor needs doas; auto channel-hop (500ms/ch), 1-9 keys lock channel, h toggles hop, p pause, c clear; ASCII runs in 802.11 addr fields decoded inline in INFO column
- chidisp: display configurator (otherthings/chidisp) — xrandr wrapper; mirror/extend/only via dmenu; Super+Shift+D
- chioverview: window overview (otherthings/chioverview.c) — all windows by tag in dmenu; Super+Tab; cc -lX11
- chilayout: sets us+ua layout with Alt+Shift toggle (otherthings/chilayout) — runs setxkbmap at xinitrc startup
- chikblayout: reads active XKB group (otherthings/chikblayout.c) — used by dwmstatus to show en/ua; cc -lX11
- dwm restart patch: `restart()` sets restarting=1, main calls execvp after cleanup; Super+Shift+F4 restarts in-place
- vim: ~/.vimrc with chi colorscheme (~/.vim/colors/chi.vim); plugins at ~/.vim/pack/plugins/start/ (fzf.vim, vim-fugitive, vim-commentary, vim-gutentags, ale)
- Audio: pipewire + wpctl (no alsa-utils; use wpctl for volume control)
- Lid close → suspend via elogind: `HandleLidSwitch=suspend` in /etc/elogind/logind.conf

## Colorscheme (pink gradient)
- bg:     #1e1224
- border: #3d2b45
- fg:     #f0b8d0
- accent: #c9479b (hot pink)
- cursor: #ff79c6 (neon pink)

## Keybinds (Super = Win key)
- Super+Enter: terminal (st)
- Super+R: launcher (dmenu)
- Super+E: file manager (chifm)
- Super+B: browser (firefox)
- Super+Q: close window
- Super+Shift+E: quit dwm
- Super+Shift+R: reboot (doas reboot)
- Super+Shift+F4: restart dwm in-place (windows persist, execvp)
- Super+Shift+P: poweroff (doas)
- Alt+Shift: toggle keyboard layout en↔ua (setxkbmap grp:alt_shift_toggle, not a dwm bind)
- Super+1-9: workspaces
- Super+Shift+1-9: move to workspace
- Super+F: keybind cheatsheet (chikeys)
- Super+Space: toggle floating
- Super+T/V/M: layouts (spiral/float/monocle)
- Super+H/L: resize master
- Super+J/K: focus next/prev
- Super+Shift+J/K: push window down/up in stack
- Super+`: scratchpad (st floating)
- Super+Shift+C: dmenu calculator (chicalc)
- Super+C: clipboard history (chiclip)
- Super+N: chinotes picker (dmenu)
- Super+Shift+N: chinotes add (dmenu prompt)
- Super+Shift+M: man page picker (chiman)
- Super+Shift+D: display config (chidisp)
- Super+Tab: window overview (chioverview)
- Super+Print: screenshot (gnome-screenshot)
- Super+Shift+Print: region screenshot (gnome-screenshot -a)
- Super+P: toggle bar
- XF86 media keys: volume up/down/mute, brightness up/down

## Packages (Artix/pacman names)
- xorg-server, xf86-input-libinput, libxinerama
- libxft, freetype2, pkgconf, ncurses
- clang, bmake, base-devel, git, curl, unzip, firefox, xorg-xinit, openssh, xclip, gnome-screenshot
- pipewire, pipewire-alsa, pipewire-pulse, wireplumber (audio — use wpctl; wireplumber is session manager)
- imlib2, libx11, readline (for chiwallpaper/chifm builds)
- ffmpeg (includes ffplay — package is `ffmpeg`, not `ffmpeg-ffplay`)
- opendoas (package name for doas on Artix)
- elogind-dinit, networkmanager-dinit, openssh-dinit (dinit service wrappers)
- iw, dbus, python, iw

## Gotchas
- Never `sed -i` all of /etc/passwd — use `chsh -s shell username` per user
- oksh builds with GNU make on Artix — `./configure && make && make install`
- `source` doesn't exist in oksh — use `.`
- dmenu SchemeNormHighlight/SchemeSelHighlight need fuzzyhighlight patch — don't add without patching
- st config.h must match the exact version's structure — always base on config.def.h after patching
- dwmstatus / wpctl needs `export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"` at top — dwm spawn env doesn't set it
- Backlight permissions: mark needs `video` group + udev rule (otherthings/fix-backlight.sh); rule can break after reboot if written with newlines — use heredoc
- readline colored prompts: wrap ANSI escapes with `\001`/`\002` or cursor position breaks (spaces before typed text)
- chitools scripts must NOT call doas internally — run the whole script as `doas chitools build ...`
- fibonacci.c include at end of dwm.c, forward declarations before config.h; systray patch has too many conflicts — skip it
- No dunst — user does not want a notification daemon
- Screenshot tool is gnome-screenshot; region: gnome-screenshot -a
- doas has restricted PATH — `doas chitools` fails; use `doas /usr/local/bin/chitools` until fixed
- chifm opens files with $EDITOR (vim); EDITOR=vim set in ~/.profile
- imlib2 animation API: use `imlib_load_image_frame(file, n)` per frame; struct is `Imlib_Frame_Info`; `frame_delay` is ms
- `scroll` conflicts with ncurses built-in `scroll()` — use `vscroll` as variable name in ncurses programs
- `%f` in mvprintw/mvhline format strings is a float specifier — escape as `%%f` for literal `%f`
- oksh history: add `HISTFILE=~/.oksh_history`, `HISTSIZE=1000`, `set -o emacs` to `~/.okshrc`
- Claude Code runs directly on ThiccPad — never SSH to it, run Bash commands locally
- Always use `doas /usr/local/bin/chitools build <target>` to build/install — never run bmake or cp manually
- `popen("r+")` doesn't work — use fork+pipe+exec for bidirectional dmenu communication in C tools
- doas strips XAUTHORITY — X11 tools run via doas fail with "Authorization required"; fix: `SHCMD("doas env XAUTHORITY=\"$XAUTHORITY\" <tool>")` in config.h, or `doas env XAUTHORITY="$XAUTHORITY" <tool>` in scripts
- `grep -v` exits 1 when no lines match — don't chain with `&&` when empty output is valid; use `;` instead
- Binding Shift/Alt as key itself in dwm is unreliable — use `setxkbmap -option grp:alt_shift_toggle` for layout switching
- chitools has `pkill_bin` helper — kills running binary before cp to avoid "Text file busy"
- `setxkbmap -query` shows configured layouts not active group — use chikblayout (XKB Xlib) for active layout in dwmstatus
- dwm restart via execvp preserves all X clients — dwmstatus, chiclip, windows all persist across Super+Shift+F4
- Battery time remaining: dwmstatus reads energy_now/power_now or charge_now/current_now from sysfs; toggled via /tmp/chi_batttime flag file; Super+Shift+B
- Artix dinit services: enable by symlinking to /etc/dinit.d/boot.d/ or `dinitctl enable <service>`
- doas package on Artix is `opendoas`; config at /etc/doas.conf must be chmod 0400
