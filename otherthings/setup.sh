#!/bin/sh
# setup.sh — fresh Slackware install tuning for ThinkPad T460
# Pulls your monorepo and hands execution off to chitools

set -e

echo "==> Configuring native Slackware init services..."
chmod +x /etc/rc.d/rc.messagebus      # D-Bus daemon
chmod +x /etc/rc.d/rc.networkmanager  # NetworkManager daemon
chmod +x /etc/rc.d/rc.sshd            # OpenSSH daemon

echo "==> Applying T460 hardware fixes..."
echo "options snd_intel_dspcfg dsp_driver=1" > /etc/modprobe.d/snd-hda.conf

echo "==> Fixing backlight permissions..."
cat > /etc/udev/rules.d/90-backlight.rules << 'EOF'
ACTION=="add", SUBSYSTEM=="backlight", RUN+="/bin/chgrp video /sys/class/backlight/%k/brightness"
ACTION=="add", SUBSYSTEM=="backlight", RUN+="/bin/chmod g+w /sys/class/backlight/%k/brightness"
EOF

# Ensure user 'mark' is added to essential subsystem groups
if ! id "mark" >/dev/null 2>&1; then
	useradd -m -G wheel,audio,video,power,plugdev -s /bin/bash mark
	echo "Created user mark."
else
	usermod -aG wheel,audio,video,power,plugdev mark
fi

echo "==> Setting up doas..."
mkdir -p /etc
echo "permit nopass :wheel" > /etc/doas.conf
chmod 0400 /etc/doas.conf

echo "==> Installing oksh from source..."
rm -rf /tmp/oksh
git clone https://github.com/ibara/oksh.git /tmp/oksh
cd /tmp/oksh
./configure && make && make install
if ! grep -q '/usr/local/bin/oksh' /etc/shells; then
	echo /usr/local/bin/oksh >> /etc/shells
fi
chsh -s /usr/local/bin/oksh mark
cd -

echo "==> Installing Nerd Fonts..."
mkdir -p /home/mark/.local/share/fonts
cd /home/mark/.local/share/fonts
curl -OL https://github.com/ryanoasis/nerd-fonts/releases/latest/download/JetBrainsMono.zip
unzip -o JetBrainsMono.zip
fc-cache -fv
cd -

# Helper to inject clean 64-bit multi-arch paths before chitools builds anything
fix_suckless_config() {
	if [ -f config.mk ]; then
		sed -i 's|/usr/X11R6/include|/usr/include/X11|g' config.mk
		sed -i 's|/usr/X11R6/lib|/usr/lib64/X11|g' config.mk
		sed -i 's|/usr/local/include|/usr/include|g' config.mk
		sed -i 's|/usr/local/lib|/usr/lib64|g' config.mk
	fi
}

echo "==> Syncing your custom repository..."
rm -rf /home/mark/dwm
git clone https://github.com/Markynose/dwmbackup /home/mark/dwm

echo "==> Fixing paths inside environment makefiles..."
# Recursively look for any config.mk files in your repo and fix X11 paths for Slackware 64-bit
find /home/mark/dwm -name "config.mk" | while read -r cfile; do
	cd "$(dirname "$cfile")"
	fix_suckless_config
	cd - > /dev/null
done

echo "==> Compiling base user interface..."
cd /home/mark/dwm
make && make install
cd -

cd /home/mark/dwm/otherthings/st
make && make install
cd -

cd /home/mark/dwm/otherthings/dmenu
make && make install
cd -

echo "==> Handing orchestrator installation off to chitools..."
if [ -f /home/mark/dwm/otherthings/chitools ]; then
	# Copy and provision your custom orchestration executable
	cp /home/mark/dwm/otherthings/chitools /usr/local/bin/chitools
	chmod +x /usr/local/bin/chitools
	
	echo "==> Driving internal framework compilation via chitools..."
	/usr/local/bin/chitools build all
fi

echo "==> Generating clean .xinitrc framework..."
cat > /home/mark/.xinitrc << 'EOF'
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

echo "==> Standardizing local directory privileges..."
chown -R mark:mark /home/mark
chmod 755 /home/mark

echo "==> Everything is built and synced! Drop out of root, type 'startx', and see how it flies."
