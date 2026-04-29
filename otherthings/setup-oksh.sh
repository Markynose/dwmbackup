#!/bin/sh
# oksh setup — run after setup.sh as root

echo "==> Setting oksh as shell for mark..."
chsh -s /usr/local/bin/oksh mark

echo "==> Setting up oksh profile..."
cat > /home/mark/.profile << 'EOF'
export PATH="$HOME/.local/bin:$PATH"
export ENV="$HOME/.okshrc"
EOF

cat > /home/mark/.okshrc << 'EOF'
# prompt
PS1='$(whoami)@$(hostname):$(basename $(pwd))$ '

# aliases
alias ls='ls --color=auto'
alias ll='ls -la'
alias grep='grep --color=auto'

chifetch
EOF

chown mark:mark /home/mark/.profile /home/mark/.okshrc

echo "==> Done! mark will now use oksh on next login."
