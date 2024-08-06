#!bin/bash

AESDSOCKET=/usr/bin/aesdsocket

case "$1" in
	start)
		echo "starting aesdsocket"
		start-stop-daemon -S -n aesdsocket -a /usr/bin/aesdsocket -- -d
		;;
	stop)
		echo "stopping aesdsocket"
		start-stop-daemon -K -n aesdsocket	
		;;
	*)
		echo "You didn't specifie the argument"	
		exit 1
		;;
esac 
exit 0		
