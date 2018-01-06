#!/bin/sh

cleanup() {
	rm windowchefrc sxhkdrc
	exit 0
}

trap 'cleanup' INT

D=${D:-80}

Xephyr -screen 1280x720 :$D &
sleep 1

export DISPLAY=:$D
cp examples/sxhkdrc sxhkdrc
cp examples/windowchefrc windowchefrc
sed -i 's/waitron/.\/waitron/g' sxhkdrc windowchefrc
sxhkd -c sxhkdrc &
if [[ "$1" == "wait" ]]; then
    exec xwait
else
    ./windowchef -c windowchefrc
fi
