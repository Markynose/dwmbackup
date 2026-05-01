# chi desktop project — historical reference

## Status
This repo is the completed ThiccPad (Artix/dinit) setup, kept as reference for porting the chi* toolchain to Hyprland on Arch (femboy, 192.168.0.199).
The WM journey: dwm → sxwm → Hyprland.

## Machine info (ThiccPad — source)
- ThiccPad: 192.168.0.92, user: mark, T460 i5-6200U 16GB, Artix Linux (dinit)
- Build tool: bmake, compiler: clang/cc
- Shell: oksh (/usr/local/bin/oksh) for mark only
- Init: dinit (`dinitctl`); services symlinked in /etc/dinit.d/boot.d/
- doas: `permit nopass :wheel` in /etc/doas.conf (package: opendoas)

## Machine info (femboy — destination)
- femboy: 192.168.0.199, user: mark, openSUSE → migrating to Arch + Hyprland
- chi* tools are being ported here; .claude memory at /home/mark/bba/.claude

## WM history
- **dwm**: pink theme, fibonacci spiral, push/scratchpad patches; `config.h` keybinds
- **sxwm**: config at `~/.config/sxwm/sxwmrc`; chibar as status bar (reads dwmstatus via WM_NAME); keybinds ported 1:1
- **Hyprland** (next): chi* tools compatible; dmenu → rofi planned

## Setup scripts
- `otherthings/setup.sh` — full fresh install (run as root after Artix install)
- `otherthings/setup-oksh.sh` — set oksh as mark's shell

## sxwm config notes
- Config: `~/.config/sxwm/sxwmrc` — bind/call/scratchpad/workspace syntax
- sxwm spawns via execvp (no shell) — no `$VAR` expansion in bind strings; wrap in `sh -c '...'` if needed
- chibar reads WM_NAME from root (dwmstatus output) for status; _NET_CURRENT_DESKTOP for workspaces
- `mod + ctrl + r` — reload sxwm config (was mod+r in default; moved to avoid chilauncher conflict)
- Scratchpad: `mod+\`` creates pad 1, `mod+ctrl+\`` toggles — must create before toggling

## chi* tools (all in otherthings/, installed to /usr/local/bin via chitools)
- **dwmstatus**: wifi+bars | vol | brightness | battery | CPU temp | todo | timer | kblayout; subcommands: `vol up/down/mute`, `bright up/down`, `batttime-toggle`; needs `XDG_RUNTIME_DIR` exported
- **chibar**: X11 status bar for sxwm (otherthings/chibar.c) — workspaces + window title + dwmstatus output; dock window via _NET_WM_STRUT_PARTIAL; cc -lX11 -lXft
- **chifetch**: custom fetch (otherthings/fetch/)
- **chifm**: TUI file manager (chifm.c) — vim keys, pink; cc -lncurses; opens files with $EDITOR
- **chitools**: management CLI (otherthings/chitools) — `doas /usr/local/bin/chitools build <target>`; has pkill_bin helper
- **chikeys**: keybind cheatsheet — opens in st with less
- **chicalc**: dmenu bc calculator
- **chiadvcalc**: python3 TUI calculator with readline history
- **chilauncher**: dmenu launcher with flatpak support — [F] prefix for flatpak apps; caches map in ~/.cache/chilauncher/
- **chiclip**: clipboard daemon + dmenu picker — daemon started from xinitrc; uses xclip
- **chiwallpaper**: wallpaper setter (chiwallpaper.c) — bg-fill; -g/--gif for GIFs; cc -lX11 -lImlib2
- **chiview**: image viewer (chiview.c) — zoom/pan/fit; cc -lX11 -lImlib2
- **chivid**: video player — ffplay wrapper
- **chitop**: process viewer (chitop.c) — /proc CPU%+mem, two-snapshot; cc -lncurses
- **chinotes**: quick notes — dmenu picker; notes in ~/.local/share/chinotes/notes
- **chitodo**: todo list — dmenu; shows in dwmstatus when non-empty
- **chiman**: man page picker — apropos-based, opens in st
- **chitimer**: countdown timer — writes to /tmp/chitimer; shows in dwmstatus
- **chied**: minimal vi-clone (chied.c) — normal/insert, hjkl, dd/yy/p, :w/:q/:wq; cc
- **chimusic**: TUI music player (chimusic.c) — ffplay backend; cc -lncurses
- **chiqemu**: QEMU TUI manager (chiqemu/) — Rust/ratatui; VM configs in ~/.local/share/chiqemu/*.json
- **chilock**: X11 locker (chilock.c) — pink fullscreen, shadow auth; `doas env XAUTHORITY="$XAUTHORITY" chilock`; cc -lX11 -lXft -lcrypt
- **chiidle**: idle locker daemon (chiidle.c) — XScreenSaver, 5min default; cc -lX11 -lXss
- **chiresume**: suspend/wake locker — dbus-monitor script; started from xinitrc
- **chisniff**: ncurses packet sniffer (chisniff.c) — IPv4 + 802.11 monitor; cc -lncurses -lpcap
- **chidisp**: display configurator — xrandr wrapper; mirror/extend/only via dmenu
- **chioverview**: window overview (chioverview.c) — all windows by tag in dmenu; cc -lX11
- **chilayout**: sets us+ua XKB layout with Alt+Shift toggle; runs at xinitrc startup
- **chikblayout**: reads active XKB group (chikblayout.c) — for dwmstatus en/ua indicator; cc -lX11
- **chiproc**: dmenu process killer — ps list → kill signal picker
- **chirun**: run command in st
- **chibar**: sxwm status bar (chibar.c) — see above

## Colorscheme (pink gradient)
- bg:     #1e1224
- border: #3d2b45
- fg:     #f0b8d0
- accent: #c9479b (hot pink)
- cursor: #ff79c6 (neon pink)

## Keybinds (sxwm — Super = Win key)
- Super+Enter: st
- Super+R: chilauncher
- Super+E: chifm
- Super+B: firefox
- Super+Q: close window
- Super+Shift+E: quit sxwm
- Super+Shift+R: reboot
- Super+Shift+P: poweroff
- Super+Shift+L: chilock
- Super+Shift+B: dwmstatus batttime-toggle
- Super+Shift+X: chiproc
- Super+;: chirun
- Super+1-9 / Super+Shift+1-9: workspaces
- Super+F: chikeys cheatsheet
- Super+Space: toggle floating
- Super+Shift+Space: global floating
- Super+Shift+F: fullscreen
- Super+M: toggle monocle
- Super+Ctrl+R: reload sxwm config
- Super+H/L: resize master
- Super+J/K: focus next/prev
- Super+Shift+J/K: push in stack
- Super+`: create scratchpad 1
- Super+Ctrl+`: toggle scratchpad 1
- Super+Shift+C: chicalc
- Super+C: chiclip
- Super+N: chinotes
- Super+Shift+N: chinotes add
- Super+Shift+M: chiman
- Super+Shift+D: chidisp
- Super+Tab: chioverview
- Super+Print: gnome-screenshot
- Super+Shift+Print: gnome-screenshot -a
- Alt+Shift: toggle kb layout en↔ua
- XF86 media keys: vol up/down/mute, brightness up/down

## Packages (Artix/pacman names)
- xorg-server, xf86-input-libinput, libxinerama
- libxft, freetype2, pkgconf, ncurses
- clang, bmake, base-devel, git, curl, unzip, firefox, xorg-xinit, openssh, xclip, gnome-screenshot
- pipewire, pipewire-alsa, pipewire-pulse, wireplumber
- imlib2, libx11, readline
- ffmpeg (includes ffplay)
- opendoas
- elogind-dinit, networkmanager-dinit, openssh-dinit
- iw, dbus, python

## Gotchas
- Never `sed -i` all of /etc/passwd — use `chsh -s shell username` per user
- `source` doesn't exist in oksh — use `.`
- dmenu SchemeNormHighlight/SchemeSelHighlight need fuzzyhighlight patch
- dwmstatus / wpctl needs `export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"` at top
- Backlight permissions: mark needs `video` group + udev rule (otherthings/fix-backlight.sh)
- readline colored prompts: wrap ANSI escapes with `\001`/`\002`
- chitools scripts must NOT call doas internally
- No dunst — user does not want a notification daemon
- doas strips XAUTHORITY — fix: `doas env XAUTHORITY="$XAUTHORITY" <tool>`
- `grep -v` exits 1 when no lines match — use `;` not `&&` when empty output is valid
- chitools has `pkill_bin` helper — kills running binary before cp to avoid "Text file busy"
- `setxkbmap -query` shows configured layouts not active group — use chikblayout for active layout
- Battery time remaining: dwmstatus reads energy_now/power_now or charge_now/current_now; toggled via /tmp/chi_batttime
- `popen("r+")` doesn't work — use fork+pipe+exec for bidirectional dmenu comms in C
- imlib2 animation: `imlib_load_image_frame(file, n)` per frame; struct `Imlib_Frame_Info`; `frame_delay` is ms
- `scroll` conflicts with ncurses `scroll()` — use `vscroll`
- `%f` in mvprintw is float — escape as `%%f` for literal
- sxwm bind commands run via execvp, no shell — $VARS not expanded; use `sh -c '...'` wrapper if needed
- chiqemu requires `doas ln -s /usr/lib/libgcc_s.so.1 /usr/lib/libgcc_s.so` once for Rust linker
- Lid close → suspend: `HandleLidSwitch=suspend` in /etc/elogind/logind.conf
