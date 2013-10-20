jack_tools
==========

alternative jack_* helpers (mods of jack_lsp, jack_evmon)

see http://jackaudio.org for more information on the JACK Audio Connection Kit

Install on Linux
----------------
```
  gcc -o jack_xlsp xlsp.c `pkg-config --libs jack`
  gcc -o jack_oscev oscev.c `pkg-config --libs liblo` `pkg-config --libs jack`

  #sudo cp jack_xlsp /usr/bin
  #sudo cp jack_oscev /usr/bin
```

jack_xlsp
---------

output aspects of jack ports as XML

```
Usage: jack_xlsp [options] [filter string]
Dump active Jack ports, and optionally display extra information as XML.
Optionally filter ports which match ALL strings provided after any options.

Display options:
        -s, --server <name>   Connect to the jack server named <name>
        -A, --aliases         List aliases for each port
        -c, --connections     List connections to/from each port
        -l, --latency         Display per-port latency in frames at each port
        -L, --latency         Display total latency in frames at each port
        -p, --properties      Display port properties. Output may include:
                              input|output, can-monitor, physical, terminal

        -t, --type            Display port type
        -h, --help            Display this help message
        --version             Output version information and exit

For more information see http://jackaudio.org/
This is a modified version of <jackd2 source>/example-clients/lsp.c (jack_lsp)

Examples:
  jack_xlsp
  jack_xlsp -AclLpt | xmlstarlet fo
  ... xmlstarlet sel -t -m '//connections' -v '../alias[1]' -o ':' -v 'count(connection)' -nl
  ... xmlstarlet sel -t -m '//port/properties[@physical="1"]' -c "../." -nl
  ... xmlstarlet sel -t -m '//port[starts-with(@name,"fire")]' -c . -nl
  ... xmlstarlet sel -t -m '//port[@name="baudline:in_2"]' -v "properties/@input" -nl

```

jack_oscev
----------

send JACK events as OSC messages

```
syntax: jack_oscev <osc local port> <osc remote host> <osc remote port>

all params are optional. order matters.
default values: 6677 127.0.0.1 6678
example: jack_oscev 9988 10.10.10.42
test on .42: oscdump 6678

messages sent by jack_oscev (example content):
  /oscev/started
  /oscev/client/registered s "meter"
  /oscev/port/registered i 24
  /oscev/port/connected ii 2 24
  /oscev/port/disconnected ii 2 24
  /oscev/port/unregistered i 24
  /oscev/client/unregistered s "meter"

```

