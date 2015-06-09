INSTALLPATH=/usr/local/bin/

all:	jack_play

jack_play:	jack_play.c
	gcc -std=gnu99 -w jack_play.c -o jack_play `pkg-config --libs jack sndfile` -lpthread -lm

install:
	install -m755 jack_play $(INSTALLPATH)

clean:
	rm jack_play
