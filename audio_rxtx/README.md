```

uses liblo >= 0.27

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
Nevertheless it works quite happily to a certain degree.

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

==============

Usage: jack_audio_receive <Options> <Listening port>.
Options:
  Display this text:                 --help
  Number of playback channels:   (2) --out <number>
  Autoconnect ports:           (off) --connect
  Jack client name:      (prg. name) --name <string>
  Limit processing count:      (off) --limit <number>
Listeting port:   <integer>

Example: jack_audio_receive --in 8 --connect 1234

Absolutely no resampling involved for now.

Experimental buffer control via OSC:
Send: /buffer i <target buffer fill>
to receiver to either fill (playback will pause)
or drop (lost audio samples) the buffer to match desired 
fill level

```
