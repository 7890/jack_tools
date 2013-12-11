```

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


```
