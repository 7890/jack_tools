#!/bin/bash

#//tb/141010
#//test different JACK period sizes on sender and receiver

OSC_PORT1=9998
OSC_PORT2=9999
#OSC_PORT3=10101

CHANNEL_COUNT=16
OUTPUT_DIR=/tmp/audio_rxtx_test_shots

#AUDIO_RXTX_OPTS="--16"

function checkAvail()
{
	which "$1" >/dev/null 2>&1
	ret=$?
	if [ $ret -ne 0 ]
	then
		print_label "tool \"$1\" not found. please install"
		print_label "note: oschema_validate is part of https://github.com/7890/oschema"
		exit 1
	fi
}

for tool in {xterm,jackd,jack_audio_send,jack_audio_receive,jack_connect,scrot,killall}; \
	do checkAvail "$tool"; done

echo "creating output dir $OUTPUT_DIR (if not existing)"
mkdir -p "$OUTPUT_DIR"

echo "starting dummy jack 'audio_rxtx 1 -p128'"
xterm -e jackd -n audio_rxtx_1 -d dummy -r44100 -p128 &
JACKD_1_PID=$!

echo "starting dummy jack 'audio_rxtx 2 -p512'"
xterm -e jackd -n audio_rxtx_2 -d dummy -r44100 -p512 &
JACKD_2_PID=$!

sleep 1

echo "starting jack_audio_send ($CHANNEL_COUNT channels)"
JACK_DEFAULT_SERVER=audio_rxtx_1 xterm -e jack_audio_send $AUDIO_RXTX_OPTS --lport $OSC_PORT1 "--in" $CHANNEL_COUNT localhost $OSC_PORT2 &
JACK_AUDIO_SEND_PID=$!

echo "starting jack_audio_receive ($CHANNEL_COUNT channels)"
JACK_DEFAULT_SERVER=audio_rxtx_2 xterm -e jack_audio_receive $AUDIO_RXTX_OPTS --out $CHANNEL_COUNT $OSC_PORT2 &
JACK_AUDIO_RECEIVE_PID=$!

sleep 2

#raise window of interest
wmctrl -a "jack_audio_receive"

sleep 0.1

echo "creating screenshot"
scrot --focused '%Y-%m-%d_%H-%M-%S_$wx$h_scrot_p128_p512.png' -e 'mv $f '"$OUTPUT_DIR"

sleep 2

echo "screenshot in $OUTPUT_DIR:"
ls -ltr "$OUTPUT_DIR" | tail -1

sleep 1

echo "shutting down / killing programs"
kill -9 $JACK_AUDIO_SEND_PID
kill -9 $JACK_AUDIO_RECEIVE_PID
kill -9 $JACKD_1_PID
kill -9 $JACKD_2_PID

sleep 1

echo "testing opposite direction"

echo "starting dummy jack 'audio_rxtx 1 -p128'"
xterm -e jackd -n audio_rxtx_1 -d dummy -r44100 -p128 &
JACKD_1_PID=$!

echo "starting dummy jack 'audio_rxtx 2 -p512'"
xterm -e jackd -n audio_rxtx_2 -d dummy -r44100 -p512 &
JACKD_2_PID=$!

sleep 1

echo "starting jack_audio_send ($CHANNEL_COUNT channels)"
JACK_DEFAULT_SERVER=audio_rxtx_2 xterm -e jack_audio_send $AUDIO_RXTX_OPTS --lport $OSC_PORT1 "--in" $CHANNEL_COUNT localhost $OSC_PORT2 &
JACK_AUDIO_SEND_PID=$!

echo "starting jack_audio_receive ($CHANNEL_COUNT channels)"
JACK_DEFAULT_SERVER=audio_rxtx_1 xterm -e jack_audio_receive $AUDIO_RXTX_OPTS --out $CHANNEL_COUNT $OSC_PORT2 &
JACK_AUDIO_RECEIVE_PID=$!

sleep 2

#raise window of interest
wmctrl -a "jack_audio_receive"

sleep 0.1

echo "creating screenshot"
scrot --focused '%Y-%m-%d_%H-%M-%S_$wx$h_scrot_p128_p512.png' -e 'mv $f '"$OUTPUT_DIR"

sleep 2

echo "screenshot in $OUTPUT_DIR:"
ls -ltr "$OUTPUT_DIR" | tail -1

sleep 1

echo "shutting down / killing programs"
kill -9 $JACK_AUDIO_SEND_PID
kill -9 $JACK_AUDIO_RECEIVE_PID
kill -9 $JACKD_1_PID
kill -9 $JACKD_2_PID

echo "done!"
exit 0
