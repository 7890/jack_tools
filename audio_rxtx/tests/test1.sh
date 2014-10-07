#!/bin/bash

#//tb/141007
#test correct channel mapping (sender channel n == receiver channel n)
#repeat multiple times
#for i in {1..10}; do ./test1.sh; sleep 3; done

CHANNEL_COUNT=80
OUTPUT_DIR=/tmp/audio_rxtx_test_shots

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

for tool in {xterm,jackd,jack_gen,jack_osc_bridge_in,jalv.gtk,jack_audio_send,jack_audio_receive,oscsend,jack_connect,scrot,killall,lv2ls}; \
	do checkAvail "$tool"; done

lv2ls | grep "http://gareus.org/oss/lv2/sisco#Stereo_gtk" >/dev/null
ret=$?

if [ $ret -eq 1 ]
then
	echo "lv2 plugin 'http://gareus.org/oss/lv2/sisco#Stereo_gtk' not found. please install"
fi

echo "creating output dir $OUTPUT_DIR (if not existing)"
mkdir -p "$OUTPUT_DIR"

echo "starting dummy jack 'audio_rxtx'"
xterm -e jackd -n audio_rxtx -d dummy -r44100 -p128 &
JACKD_PID=$!

sleep 1

echo "starting jack_gen"
JACK_DEFAULT_SERVER=audio_rxtx xterm -e jack_gen &
JACK_GEN_PID=$!

echo "starting jack_osc_bridge_in"
JACK_DEFAULT_SERVER=audio_rxtx xterm -e jack_osc_bridge_in &
JACK_OSC_BRIDGE_IN_PID=$!

echo "starting sisco"
JACK_DEFAULT_SERVER=audio_rxtx xterm -e jalv.gtk "http://gareus.org/oss/lv2/sisco#Stereo_gtk" &
JALV_PID=$!

echo "starting jack_audio_send ($CHANNEL_COUNT channels)"
JACK_DEFAULT_SERVER=audio_rxtx xterm -e jack_audio_send --in $CHANNEL_COUNT localhost 1234 &
JACK_AUDIO_SEND_PID=$!

echo "starting jack_audio_receive ($CHANNEL_COUNT channels)"
JACK_DEFAULT_SERVER=audio_rxtx xterm -e jack_audio_receive --out $CHANNEL_COUNT 1234 &
JACK_AUDIO_RECEIVE_PID=$!

sleep 1

echo "creating JACK connections"
JACK_DEFAULT_SERVER=audio_rxtx jack_connect "osc_bridge_in_3344:out" "gen:in"

sleep 0.5

echo "setting gen shape sine"
oscsend localhost 3344 /shape/sine

JACK_DEFAULT_SERVER=audio_rxtx jack_connect "gen:out" "Simple Scope (Stereo) GTK:in1"
JACK_DEFAULT_SERVER=audio_rxtx jack_connect "gen:out" "send:input_${CHANNEL_COUNT}"
JACK_DEFAULT_SERVER=audio_rxtx jack_connect "receive:output_${CHANNEL_COUNT}" "Simple Scope (Stereo) GTK:in2"

sleep 2

echo "creating screenshot"
scrot --focused '%Y-%m-%d_%H-%M-%S_$wx$h_scrot.png' -e 'mv $f '"$OUTPUT_DIR"

sleep 2

echo "screenshot in $OUTPUT_DIR:"
ls -ltr "$OUTPUT_DIR" | tail -1

echo "shutting down / killing programs"
kill -9 $JACK_GEN_PID
kill -9 $JALV_PID
kill -9 $JACK_AUDIO_SEND_PID
kill -9 $JACK_AUDIO_RECEIVE_PID
kill -9 $JACK_OSC_BRIDGE_IN_PID
kill -9 $JACKD_PID

echo "done!"
exit 0
