#!/bin/sh
# setup.sh — fresh Artix Linux (dinit) install on ThiccPad T460
# run as root after Artix install

set -e

echo "==> Syncing and installing dependencies..."
pacman -Syu --noconfirm
pacman -S --noconfirm --needed \
	base-devel git curl unzip pkgconf \
	clang bmake \
	ncurses libxft libxinerama libx11 \
	freetype2 imlib2 readline \
	libpcap libxscrnsaver \
	xorg-xinit xorg-server xf86-input-libinput xclip \
	pipewire pipewire-alsa pipewire-pulse wireplumber \
	firefox gnome-screenshot \
	openssh python iw dbus \
	opendoas elogind networkmanager \
	elogind-dinit networkmanager-dinit openssh-dinit

echo "==> Enabling dinit services..."
ln -sf /etc/dinit.d/dbus        /etc/dinit.d/boot.d/dbus
ln -sf /etc/dinit.d/elogind     /etc/dinit.d/boot.d/elogind
ln -sf /etc/dinit.d/NetworkManager /etc/dinit.d/boot.d/NetworkManager

echo "==> Applying T460 hardware fixes..."
# Skylake: SOF driver takes priority but fails; force legacy HDA
echo "options snd_intel_dspcfg dsp_driver=1" > /etc/modprobe.d/snd-hda.conf

echo "==> Fixing backlight permissions..."
cat > /etc/udev/rules.d/90-backlight.rules << 'EOF'
ACTION=="add", SUBSYSTEM=="backlight", RUN+="/bin/chgrp video /sys/class/backlight/%k/brightness"
ACTION=="add", SUBSYSTEM=="backlight", RUN+="/bin/chmod g+w /sys/class/backlight/%k/brightness"
EOF
usermod -aG video,wheel,network,audio,kvm,plugdev mark

echo "==> Setting up doas..."
echo "permit nopass :wheel" > /etc/doas.conf
chmod 0400 /etc/doas.conf

echo "==> Installing oksh..."
rm -rf /tmp/oksh
git clone https://github.com/ibara/oksh.git /tmp/oksh
cd /tmp/oksh
./configure && make && make install
echo /usr/local/bin/oksh >> /etc/shells
chsh -s /usr/local/bin/oksh mark
cd -

echo "==> Installing Nerd Fonts..."
mkdir -p /home/mark/.local/share/fonts
cd /home/mark/.local/share/fonts
curl -OL https://github.com/ryanoasis/nerd-fonts/releases/latest/download/JetBrainsMono.zip
unzip -o JetBrainsMono.zip
fc-cache -fv
cd -

echo "==> Cloning dwm..."
rm -rf /home/mark/dwm-src
git clone https://git.suckless.org/dwm /home/mark/dwm-src
cd /home/mark/dwm-src
cp /home/mark/dwm/config.h .
bmake && bmake install
cd -

echo "==> Cloning st..."
rm -rf /home/mark/st
git clone https://git.suckless.org/st /home/mark/st
cd /home/mark/st
curl -O https://st.suckless.org/patches/scrollback/st-scrollback-0.9.2.diff
curl -O https://st.suckless.org/patches/scrollback/st-scrollback-mouse-0.9.2.diff
patch -p1 < st-scrollback-0.9.2.diff
patch -p1 < st-scrollback-mouse-0.9.2.diff
cp /home/mark/dwm/otherthings/st/config.h .
bmake && bmake install
cd -

echo "==> Cloning dmenu..."
rm -rf /home/mark/dmenu
git clone https://git.suckless.org/dmenu /home/mark/dmenu
cd /home/mark/dmenu
cp /home/mark/dwm/otherthings/dmenu/config.h .
bmake && bmake install
cd -

echo "==> Installing chitools..."
cp /home/mark/dwm/otherthings/chitools /usr/local/bin/chitools
chmod +x /usr/local/bin/chitools

echo "==> Building all chi tools..."
/usr/local/bin/chitools build all

echo "==> Setting up xinitrc..."
cat > /home/mark/.xinitrc << 'EOF'
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
pipewire &
wireplumber &
pipewire-pulse &
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

echo "==> Setting ownership..."
chown -R mark:mark /home/mark
chmod 755 /home/mark

echo "==> Done! Log in as mark and run: startx"
