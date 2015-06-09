CC ?= gcc
CFLAGS ?= -std=gnu99 -w -D_GNU_SOURCE=1 -DUSE_WEAK_JACK=1

INSTALLPATH=/usr/local/bin/

#gcc -std=gnu99 -w jack_play.c -o jack_play `pkg-config --libs jack sndfile` -lpthread -lm

all:	jack_play

jack_play:	jack_play.c weak_libjack.c
	$(CC) -c -o weak_libjack.o weak_libjack.c $(CFLAGS) `pkg-config --cflags jack` -DNDEBUG
	$(CC) -c -o jack_play.o jack_play.c $(CFLAGS) `pkg-config --libs sndfile`
	$(CC) -o jack_play jack_play.o weak_libjack.o $(CFLAGS) `pkg-config --libs sndfile` -lpthread -lm -ldl

install:
	install -m755 jack_play $(INSTALLPATH)

clean:
	rm jack_play
	rm jack_play.o
	rm weak_libjack.o
