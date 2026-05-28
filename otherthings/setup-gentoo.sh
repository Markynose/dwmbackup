#!/bin/sh
# setup-gentoo.sh — dwm environment setup for Gentoo (femboy, amd64)
# Run as root after stage3 + base system install

set -e

USERNAME="mark"
REPO="https://github.com/Markynose/dwmbackup"
DWMDIR="/home/${USERNAME}/dwm"

# ─── 1. EMERGE DEPENDENCIES ───────────────────────────────────────────────────

echo "==> Syncing portage..."
emerge --sync

echo "==> Enabling guru overlay (for oksh)..."
emerge --noreplace app-eselect/eselect-repository
eselect repository list | grep -q guru || eselect repository enable guru
emerge --sync guru

echo "==> Installing dependencies..."
emerge -av --noreplace \
	x11-base/xorg-server \
	x11-apps/xinit \
	x11-libs/libX11 \
	x11-libs/libXft \
	x11-libs/libXinerama \
	x11-libs/libXrandr \
	x11-libs/libXScrnSaver \
	media-libs/fontconfig \
	media-libs/freetype \
	app-shells/fish \
	dev-vcs/git \
	net-misc/curl \
	app-arch/unzip \
	app-misc/ca-certificates \
	sys-apps/doas \
	dev-lang/rust \
	dev-util/pkgconf \
	x11-misc/xclip \
	x11-apps/xrandr \
	x11-apps/xset \
	x11-misc/xss-lock \
	media-gfx/feh \
	x11-apps/xrdb \
	media-libs/imlib2 \
	net-libs/libpcap \
	media-video/pipewire \
	dev-libs/readline \
	media-video/ffmpeg \
	dev-lang/python

# ─── 2. USER ──────────────────────────────────────────────────────────────────

echo "==> Setting up user ${USERNAME}..."
if ! id "${USERNAME}" >/dev/null 2>&1; then
	useradd -m -G wheel,audio,video,usb,plugdev -s /bin/bash "${USERNAME}"
	echo "Created user ${USERNAME}."
else
	usermod -aG wheel,audio,video,usb,plugdev "${USERNAME}"
fi

echo "==> Configuring doas..."
echo "permit nopass :wheel" > /etc/doas.conf
chmod 0400 /etc/doas.conf

# ─── 3. OKSH ──────────────────────────────────────────────────────────────────

echo "==> Setting oksh as shell for ${USERNAME}..."
if ! grep -q '/usr/bin/fish' /etc/shells; then
	echo /usr/bin/fish >> /etc/shells
fi
chsh -s /usr/bin/fish "${USERNAME}"

cat > /home/${USERNAME}/.profile << 'EOF'
export PATH="$HOME/.local/bin:$PATH"
export 
EOF

cat > /home/${USERNAME}/.okshrc << 'EOF'
PS1='$(whoami)@$(hostname):$(basename $(pwd))$ '
alias ls='ls --color=auto'
alias ll='ls -la'
alias grep='grep --color=auto'
chifetch
EOF

# ─── 4. NERD FONTS ────────────────────────────────────────────────────────────

echo "==> Installing JetBrainsMono Nerd Font..."
mkdir -p /home/${USERNAME}/.local/share/fonts
cd /home/${USERNAME}/.local/share/fonts
curl -OL https://github.com/ryanoasis/nerd-fonts/releases/latest/download/JetBrainsMono.zip
unzip -o JetBrainsMono.zip
fc-cache -fv
cd -

# ─── 5. DWM REPO ──────────────────────────────────────────────────────────────

echo "==> Cloning dwmbackup repo..."
rm -rf "${DWMDIR}"
git clone "${REPO}" "${DWMDIR}"

# ─── 6. BUILD DWM + ST + DMENU ────────────────────────────────────────────────

echo "==> Building dwm..."
cd "${DWMDIR}"
make && make install
cd -

echo "==> Building st..."
cd "${DWMDIR}/otherthings/st"
make && make install
cd -

echo "==> Building dmenu..."
cd "${DWMDIR}/otherthings/dmenu"
make && make install
cd -

# ─── 7. CHITOOLS ──────────────────────────────────────────────────────────────

echo "==> Building chitools..."
CHITOOLS_DIR="${DWMDIR}/otherthings/chitools"

if [ -d "${CHITOOLS_DIR}" ]; then
	cd "${CHITOOLS_DIR}"

	# Build any .c tools
	for f in *.c; do
		[ -f "$f" ] || continue
		bin="${f%.c}"
		echo "  -> Compiling ${f}..."
		gcc -O2 -o "/usr/local/bin/${bin}" "${f}"
	done

	# Build Rust tool if Cargo.toml present
	if [ -f Cargo.toml ]; then
		echo "  -> Building Rust tool..."
		# build as mark to avoid root cargo issues
		su - "${USERNAME}" -c "cd ${CHITOOLS_DIR} && cargo build --release"
		# install the binary — find it under target/release/
		RUST_BIN=$(cargo metadata --no-deps --format-version 1 2>/dev/null \
			| grep '"name"' | head -1 | sed 's/.*: "\(.*\)".*/\1/' || true)
		if [ -n "${RUST_BIN}" ] && [ -f "${CHITOOLS_DIR}/target/release/${RUST_BIN}" ]; then
			cp "${CHITOOLS_DIR}/target/release/${RUST_BIN}" /usr/local/bin/
		else
			# fallback: copy any binary in target/release that isn't a .d/.rlib
			find "${CHITOOLS_DIR}/target/release" -maxdepth 1 -type f -executable \
				| while read -r b; do cp "$b" /usr/local/bin/; done
		fi
	fi

	# If chitools is a build orchestrator binary, run it
	if [ -f /usr/local/bin/chitools ]; then
		echo "  -> Running chitools build all..."
		/usr/local/bin/chitools build all
	fi

	cd -
else
	echo "  [!] ${CHITOOLS_DIR} not found, skipping chitools build."
fi

# ─── 8. XINITRC ───────────────────────────────────────────────────────────────

echo "==> Writing .xinitrc..."
cat > /home/${USERNAME}/.xinitrc << 'EOF'
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

chilayout &
dwmstatus &
chiclip daemon &
chiidle &
chiresume &

if [ -f ~/.wallpaper ]; then
  case "$(file -b ~/.wallpaper)" in
  GIF*) chiwallpaper -g ~/.wallpaper &;;
  *)    chiwallpaper ~/.wallpaper &;;
  esac
fi

eval "$(ssh-agent -s)"
exec dwm
EOF

# ─── 9. PERMISSIONS ───────────────────────────────────────────────────────────

echo "==> Fixing ownership..."
chown -R ${USERNAME}:${USERNAME} /home/${USERNAME}
chmod 755 /home/${USERNAME}

echo ""
echo "==> Done! Switch to ${USERNAME}, run 'startx', and see how it flies."
