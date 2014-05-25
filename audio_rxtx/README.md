```

audio_rxtx uses liblo >= 0.27

** not for production use -- experimental **

jack_audio_send & jack_audio_receive are jack clients
allowing uncompressed/unchanged ~native JACK 
audio data transfer over a LAN using OSC messages. 
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
  Jack client name:           (send) --name <string>
  Update info every nth cycle   (99) --update <number>
  Limit totally sent messages: (off) --limit <number>
  Immediate send, ignore /pause (off) --nopause
  (Use with multiple receivers. Ignore /pause, /deny)
Receiver host:   <string>
Receiver port:   <number>

Example: jack_audio_send --in 8 10.10.10.3 1234

To send to multiple hosts, use the broadcast address of your 
subnet, for instance:
jack_audio_send 10.10.10.255 1234
(This is not multicast)

jack_audio_send Example Output
___________
sending from osc port: 4433
target host:port: localhost:4444
sample rate: 44100
bytes per sample: 4
period size: 256 samples (5.805 ms, 1024 bytes)
channels (capture): 4
immediate send, no pause or shutdown: no
multi-channel period size: 4096 bytes
message rate: 172.3 packets/s
message length: 4156 bytes
transfer length: 4198 bytes (2.4 % overhead)
expected network data rate: 5785.4 kbit/s (0.72 mb/s)

# 15093 (00:01:27) xruns: 0 tx: 63360554 bytes (63.36 mb) p: 0.1
___________

Legend
#: sequential message number since start of program
(HH:MM:SS): time corresponding to message number
xruns: local xrun counter
tx: expected total network traffic sum
p: how much of the available process cycle time was used to do the work (1=100%)

jack_audio_sender states:
-offering audio to given host
-received /accept transmission (if offered audio was compatible)
-received /deny transmission (if offered audio was incompatible)
 *** quit
-sending /audio to receiver (one message = one multi-channel period)
-receiveing pause transmission -> offering again

jack_audio_sender states (--nopause):
-sending /audio to receiver (one message = one multi-channel period)

jack_audio_send has no buffer. in every cycle, a message
is sent with all channels as blobs.


=====
Usage: jack_audio_receive <Options> <Listening port>.
Options:
  Display this text:                 --help
  Number of playback channels:   (2) --out <number>
  Autoconnect ports:           (off) --connect
  Jack client name:        (receive) --name <string>
  Initial buffer size:(4 mc periods) --pre <number>
  Max buffer size >= init:    (auto) --max <number>
  Rebuffer on sender restart:  (off) --rere
  Rebuffer on underflow:       (off) --reuf
  Re-use old data on underflow: (no) --nozero
  Update info every nth cycle   (99) --update <number>
  Limit processing count:      (off) --limit <number>
  Quit on incompatibility:     (off) --close

Listening port:   <number>

Example: jack_audio_receive --in 8 --connect --pre 200 1234

jack_audio_receive Example Output
___________
listening on osc port: 4444
sample rate: 44100
bytes per sample: 4
period size: 256 samples (5.805 ms, 1024 bytes)
channels (playback): 2
multi-channel period size: 2048 bytes
underflow strategy: fill with zero (silence)
rebuffer on sender restart: no
rebuffer on underflow: yes
shutdown receiver when incompatible data received: no
initial buffer size: 4 mc periods (23.220 ms, 8192 bytes, 0.01 mb)
allocated buffer size: 91 mc periods (528.254 ms, 186368 bytes, 0.19 mb)

# 1384 i: 4 f: 4.5 b: 9216 s: 0.0261 i: 5.82 r: 0 l: 0 d: 0 o: 0 p: 0.1
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
o: buffer overflows (lost audio)
p: how much of the available process cycle time was used to do the work (1=100%)

jack_audio_receive states:
-waiting for audio (if no sender is currently active)

if sender was started without --nopause:
-receiving audio /offer from sender
-accepting transmission (if offered audio was compatible)
-denying transmission (if offered audio was incompatible)

-buffering (for the given --pre size in periods)
-playing (read from buffer, pass to jack)
-buffer underflow (not enough data to read)
 -rebuffer (if --reuf set)
-buffer overflow (buffer full, can't add more data)
-rebuffer on sender restart (if --rere set)
-shutting down, not accepting data (-if --close set)

Absolutely no resampling involved for now.

Experimental buffer control via OSC
Send: /buffer ii <buffer pre-fill> <buffer size max>
to receiver to either fill (playback will pause)
or drop (lost audio samples) the buffer to match desired 
fill level
if <buffer size max> is not the same as --mbuff / auto,
a new buffer will be created and filled with <buffer pre-fill>

```
