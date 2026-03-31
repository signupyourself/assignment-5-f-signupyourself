#! /bin/sh

case "$1" in
 start)
	echo "Starting socketserver"
	start-stop-daemon -S -n socketserver -a /bin/socketserver
	;;
 stop)
	echo "Stopping socketserver"
	start-stop-daemon -K -n  socketserver
	;;
 *)
	echo "Usage : $0 start|stop"  
 exit 1
esac
 exit 0
