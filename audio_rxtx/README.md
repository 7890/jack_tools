```

jack_audio_send & jack_audio_receive are JACK clients allowing to transmit uncompressed 
native JACK 32 bit float audio data on the network using UDP OSC messages.

All involved JACK servers must share the same sampling rate but can run at different period sizes.

audio_rxtx uses jackd, liblo >= 0.27
(older liblo is prone to server error 9912)

Build & Install on Linux
========================

make && sudo make install

man jack_audio_send
man jack_audio_receive



=====
Usage: jack_audio_send [Options] target_host target_port
Options:
  Display this text:                  --help
  Local port:                  (9990) --lport  <integer>
  Number of capture channels :    (2) --in     <integer>
  Autoconnect ports:            (off) --connect
  JACK client name:            (send) --name   <string>
  JACK server name:         (default) --sname  <string>
  Update info every nth cycle    (99) --update <integer>
  Limit totally sent messages:  (off) --limit  <integer>
  Immediate send, ignore /pause (off) --nopause
  (Use with multiple receivers. Ignore /pause, /deny)
target_host:   <string>
target_port:   <integer>

Example: jack_audio_send --in 8 10.10.10.3 1234


=====
Usage: jack_audio_receive [Options] listening_port
Options:
  Display this text:                 --help
  Number of playback channels:   (2) --out    <integer>
  Autoconnect ports:           (off) --connect
  JACK client name:        (receive) --name   <string>
  JACK server name:        (default) --sname  <string>
  Initial buffer size:(4 mc periods) --pre    <integer>
  Max buffer size (>= init):  (auto) --max    <integer>
  Rebuffer on sender restart:  (off) --rere
  Rebuffer on underflow:       (off) --reuf
  Re-use old data on underflow: (no) --nozero
  Update info every nth cycle   (99) --update <integer>
  Limit processing count:      (off) --limit  <integer>
  Quit on incompatibility:     (off) --close
listening_port:   <integer>

Example: jack_audio_receive --out 8 --connect --pre 200 1234


```
