CC=gcc
CFLAGS=`pkg-config --libs --cflags jack liblo` -D_GNU_SOURCE -lm
SRC=src

all: $(SRC)/jack_audio_send.c $(SRC)/jack_audio_receive.c
	$(CC) -c -o jack_audio_common.o $(SRC)/jack_audio_common.c $(CFLAGS)

	$(CC) -c -o jack_audio_receive.o $(SRC)/jack_audio_receive.c $(CFLAGS)
	$(CC) -o jack_audio_receive jack_audio_common.o jack_audio_receive.o $(CFLAGS)

	$(CC) -c -o jack_audio_send.o $(SRC)/jack_audio_send.c $(CFLAGS)
	$(CC) -o jack_audio_send jack_audio_common.o jack_audio_send.o $(CFLAGS)
