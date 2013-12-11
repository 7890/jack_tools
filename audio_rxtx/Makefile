CC=gcc
CFLAGS=`pkg-config --libs --cflags jack liblo`
SRC=src

all: $(SRC)/jack_audio_send.c $(SRC)/jack_audio_receive.c
	$(CC) -o jack_audio_send $(SRC)/jack_audio_send.c $(CFLAGS)
	$(CC) -o jack_audio_receive $(SRC)/jack_audio_receive.c $(CFLAGS)

