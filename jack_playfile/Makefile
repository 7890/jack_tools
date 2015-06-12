CC ?= gcc
CFLAGS ?= -std=gnu99 -w -D_GNU_SOURCE=1 -DUSE_WEAK_JACK=1 -w

SRC ?= src
BUILD ?= build

INSTALLPATH=/usr/local/bin/

#gcc -o register_jack_ports2 register_jack_ports2.c `pkg-config --cflags --libs jack sndfile` -pthread -w

all:	jack_playfile

jack_playfile:	$(SRC)/jack_playfile.c $(SRC)/weak_libjack.c
	mkdir -p $(BUILD)
	$(CC) -c -o $(BUILD)/weak_libjack.o $(SRC)/weak_libjack.c $(CFLAGS) `pkg-config --cflags --libs jack` -DNDEBUG
	$(CC) -c -o $(BUILD)/jack_playfile.o $(SRC)/jack_playfile.c $(CFLAGS) `pkg-config --cflags --libs sndfile`
	$(CC) -o jack_playfile $(BUILD)/jack_playfile.o $(BUILD)/weak_libjack.o $(CFLAGS) `pkg-config --cflags --libs sndfile` -lpthread -ldl

install: jack_playfile
	install -m755 jack_playfile $(INSTALLPATH)

clean:
	rm jack_playfile
	rm $(BUILD)/*.o
