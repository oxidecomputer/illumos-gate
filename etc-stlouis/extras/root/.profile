export PATH=/usr/bin:/usr/sbin:/sbin:/tmp/bin:/tmp/usr/bin:/tmp/usr/sbin:/tmp/sbin:/tmp/usr/xpg6/bin:/opt/local/bin

# Work around missing terminfo
if [ "$TERM" = "alacritty" ]; then
	TERM=xterm-256color
fi
