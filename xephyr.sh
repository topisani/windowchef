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
export PATH="$PWD:$PATH"

cp examples/sxhkdrc sxhkdrc
if [[ $# != 0 ]]; then
    exec "$@"
else
    sxhkd -c sxhkdrc &
    ./windowchef -c windowchefrc
fi
