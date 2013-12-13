```

audio_rxtx uses liblo >= 0.27

jack_audio_send & jack_audio_receive are jack clients
allowing real-time uncompressed/unchanged ~native JACK 
audio data transfer over the network using OSC messages. 
One blob per channel and period, one "multi-channel" period
per message. All exchanging systems must share the same 
sampling rate, period size and bytes per sample.

Please don't compare these tools to jacktrip, netjack and 
similar. This is mostly a crude prototype not caring about 
too much more than pumping out messages and hoping they will 
arrive at the receiver. I know people who consider OSC 
unreasonable for doing that :) Nevertheless it works quite 
happily to a certain degree.


=====
Usage: jack_audio_send <Options> <Receiver host> <Receiver port>.
Options:
  Display this text:                 --help
  Local port:                 (9990) --lport <number>
  Number of capture channels:    (2) --in <number>
  Autoconnect ports:           (off) --connect
  Jack client name:      (prg. name) --name <string>
  Update info every nth cycle   (99) --update <number>
  Limit totally sent messages: (off) --limit <number>
Receiver host:   <string>
Receiver port:   <number>

Example: jack_audio_send --in 8 10.10.10.3 1234

To send to multiple hosts, use the broadcast address of your 
subnet, for instance:
jack_audio_send 10.10.10.255 1234
(This is not multicast)

jack_audio_send Example Output
___________
sending from osc port: 3333
target host:port: 10.10.10.111:4444
sample rate: 44100
bytes per sample: 4
period size: 64 samples (1.45 ms, 256 bytes)
channels (capture): 4
max. multi-channel period size: 1024 bytes
message rate: 689.1 packets/s
message length: 1084 bytes
transfer length: 1126 bytes (9.1 % overhead)
expected network data rate: 6207.1 kbit/s

# 140862 (00:03:24) xruns: 0 bytes tx: 158610752 p: 0.3
___________

Legend
#: sequential message number since start of program
(HH:MM:SS): time corresponding to message number
xruns: local xrun counter
bytes tx: expected total network traffic sum at receiver
p: how much of the available process cycle time was used to do the work (1=100%)

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
  Initial buffer size:(2 mc periods) --pre <number>
  Re-use old data on underflow: (no) --nozero
  Update info every nth cycle   (99) --update <number>
  Limit processing count:      (off) --limit <number>
Listening port:   <number>

Example: jack_audio_receive --in 8 --connect --pre 200 1234

jack_audio_receive Example Output
___________
listening on osc port: 6666
sample rate: 44100
bytes per sample: 4
period size: 64 samples (1.45 ms, 256 bytes)
channels (playback): 2
max. multi-channel period size: 512 bytes
underflow strategy: fill with zero (silence)
initial buffer: 4 mc periods (0.0058 sec)
ringbuffer: 20480 bytes

# 196273 i: 4 f: 5.5 b: 2816 s: 0.0080 i: 1.45 r: 0 l: 0 d: 4 p: 0.2
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
d: dropped multi-channel periods (buffer underflow)
p: how much of the available process cycle time was used to do the work (1=100%)

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
