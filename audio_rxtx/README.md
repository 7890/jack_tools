```

audio_rxtx uses liblo >= 0.27

jack_audio_send & jack_audio_receive are jack clients
allowing real-time uncompressed/unchanged ~native JACK 
audio data transfer over the network using OSC messages. 
One blob per channel and period, one "multi-channel" period
per message. All exchanging systems must share the same 
sampling rate, period size and bytes per sample.

Please compare these tools to jack.trip, netjack and 
similar while keeping in mind this is mostly a crude 
prototype not caring about too much more than pumping out 
messages and hoping they will arrive at the receiver. 
I know people who consider OSC unreasonable for doing that :)
Nevertheless it works quite happily to a certain degree.


=====
Usage: jack_audio_send <Options> <Receiver host> <Receiver port>.
Options:
  Display this text:                 --help
  Local port:                 (9990) --lport <number>
  Number of capture channels:    (2) --in <number>
  Autoconnect ports:           (off) --connect
  Jack client name:      (prg. name) --name <string>
  Limit totally sent messages: (off) --limit <number>
Receiver host:   <string>
Receiver port:   <integer>

Example: jack_audio_send --in 8 10.10.10.3 1234

To send to multiple hosts, use the broadcast address of your 
subnet, for instance:
jack_audio_send 10.10.10.255 1234
(This is not multicast)

jack_audio_receive Example Output
___________
sending from osc port: 9990
target host:port: localhost:6644
sample rate: 44100
bytes per sample: 4
period size: 128 (0.0029 sec)
channels (capture): 2
message rate: 344.5 packets/s
message length: 1072 bytes
transfer length: 1114 bytes
expected network data rate: 3070.5 kbit/s

# 3452 (00:00:10) xruns: 0 bytes tx: 3845668
___________

Legend
#: sequential message number since start of program
(HH:MM:SS): time corresponding to message number
xruns: local xrun counter
bytes tx: expected total network traffic sum at receiver

jack_audio_sender states:
-offering audio to given host
-receiving accepted transmission (if offered audio was compatible)
-receiveing denying transmission (if offered audio was incompatible)
 *** quit
-sending /audio to receiver (one message = one multi-channel period)
-receiveing pause transmission -> offering again

jack_audio_send has no buffer. in every cycle, a message
is sent with all channels as blobs.


=====
Usage: jack_audio_receive <Options> <Listening port>.
Options:
  Display this text:                 --help
  Number of playback channels:   (2) --out <number>
  Autoconnect ports:           (off) --connect
  Jack client name:      (prg. name) --name <string>
  Initial buffe sizer:     (periods) --pre <number>
  Limit processing count:      (off) --limit <number>
Listening port:   <number>

Example: jack_audio_receive --in 8 --connect --pre 200 1234

jack_audio_receive Example Output
___________
listening on osc port: 6644
sample rate: 44100
bytes per sample: 4
period size: 128 (2.90 ms)
channels (playback): 2
initial buffer (periods): 1000 (2.9025 sec)
ringbuffer (bytes): 10240000

# 4478 i: 2 f: 1000.5 b: 1024512 s: 2.9039 i: 2.90 r: 0 l: 0 u: 0
___________

Legend
#: id given by sender, sequence since start of sender
i: input channel count (can dynamically change)
f: buffer fill level: periods (for all channels)
b: buffer fill level: bytes
s: buffer fill level: seconds
i: average time between messages: milliseconds
r: remote xrun counter
l: local xrun counter
u: buffer underflow counter

jack_audio_receive states:
-waiting for audio (if no sender is currently active)
-receiving audio /offer from sender
-accepting transmission (if offered audio was compatible)
-denying transmission (if offered audio was incompatible)
-buffering (for the given --pre size in periods)
-playing (read from buffer, pass to jack)
-buffer underflow (not enough data to read)
-buffer overflow (buffer full, can't add more data)
-shutting down (not accepting data/control, clean up)

Absolutely no resampling involved for now.

Experimental buffer control via OSC
Send: /buffer i <target buffer fill>
to receiver to either fill (playback will pause)
or drop (lost audio samples) the buffer to match desired 
fill level

```
