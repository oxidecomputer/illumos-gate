PATH=/usr/bin:/usr/sbin:/sbin
PATH+=:/usr/lib/pci:/usr/platform/oxide/bin
PATH+=:/tmp/bin:/tmp/usr/bin:/tmp/usr/sbin:/tmp/sbin:/tmp/usr/xpg6/bin

# Work around missing terminfo
case "$TERM" in
	alacritty|xterm-ghostty)	TERM=xterm-256color ;;
	*)
esac

