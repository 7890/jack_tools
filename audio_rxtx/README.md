```

jack_audio_send & jack_audio_receive are JACK clients allowing to transmit uncompressed 
native JACK 32 bit float audio data on the network using UDP OSC messages.

All involved JACK servers must share the same sampling rate but can run at different period sizes.

audio_rxtx uses jackd, liblo >= 0.27

Build & Install on Linux
========================

make && sudo make install

man jack_audio_send
man jack_audio_receive



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


```
