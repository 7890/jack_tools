CC = g++
CFLAGS ?= -std=gnu99 -w -D_GNU_SOURCE=1 -D__STDC_FORMAT_MACROS -DUSE_WEAK_JACK=1 -w

#http://www.cprogramdevelop.com/4787258/

SRC ?= src
BUILD ?= build

INSTALLPATH=/usr/local/bin/

all:	jack_step

jack_step:	$(SRC)/jack_step.c $(SRC)/weak_libjack.c Makefile
	mkdir -p $(BUILD)
	$(CC) -c -o $(BUILD)/weak_libjack.o $(SRC)/weak_libjack.c $(CFLAGS) `pkg-config --cflags --libs jack` -DNDEBUG
	$(CC) -c -o $(BUILD)/jack_step.o $(SRC)/jack_step.c $(CFLAGS)
	$(CC) -o jack_step $(BUILD)/jack_step.o $(BUILD)/weak_libjack.o $(CFLAGS) -ldl

install: jack_step
	install -m755 jack_step $(INSTALLPATH)

clean:
	rm jack_step
	rm $(BUILD)/*.o
