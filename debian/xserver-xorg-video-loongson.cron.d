#
# Regular cron jobs for the xserver-xorg-video-loongson package
#
0 4	* * *	root	[ -x /usr/bin/xserver-xorg-video-loongson_maintenance ] && /usr/bin/xserver-xorg-video-loongson_maintenance
