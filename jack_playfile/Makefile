CC = g++
CFLAGS ?= -D_GNU_SOURCE=1 -D__STDC_FORMAT_MACROS -DUSE_WEAK_JACK=1 -DNO_JACK_METADATA=1 -Wno-write-strings -Wno-pointer-arith
#-DNO_JACK_METADATA -w

#http://www.cprogramdevelop.com/4787258/

SRC ?= src
BUILD ?= build

INSTALLPATH=/usr/local/bin/

DISTRO_LIBS_DIR = /usr/lib/x86_64-linux-gnu
LOCAL_LIBS_DIR = /usr/local/lib

STATIC_LIBS_DISTRO = -l:$(DISTRO_LIBS_DIR)/libsndfile.a -l:$(DISTRO_LIBS_DIR)/libFLAC.a -l:$(DISTRO_LIBS_DIR)/libvorbisenc.a -l:$(DISTRO_LIBS_DIR)/libvorbis.a -l:$(DISTRO_LIBS_DIR)/libogg.a -l:$(DISTRO_LIBS_DIR)/libopus.a -l:$(DISTRO_LIBS_DIR)/libvorbisfile.a
#custom install locaiton
LIB_OPUSFILE_A = -l:/usr/lib/libopusfile.a

#zita-resampler: ar -cvq libzita-resampler.a resampler.o vresampler.o resampler-table.o
#mpg123: ./configure --enable static

STATIC_LIBS_LOCAL = -l:$(LOCAL_LIBS_DIR)/libzita-resampler.a -l:$(LOCAL_LIBS_DIR)/libmpg123.a 

STATIC_LIBS = $(LIB_OPUSFILE_A) $(STATIC_LIBS_DISTRO) $(STATIC_LIBS_LOCAL)

#-l:$(DISTRO_LIBS_DIR)/libdl.a
#-l:$(DISTRO_LIBS_DIR)/libpthread.a

default: jack_playfile

all:	jack_playfile jack_playfile_static

jack_playfile: $(SRC)/jack_playfile.c $(SRC)/jack_playfile.h $(SRC)/common.h $(SRC)/buffers.h $(SRC)/config.h $(SRC)/control.h $(SRC)/jackaudio.h $(SRC)/resampler.h $(SRC)/sndin.h $(SRC)/weak_libjack.c $(SRC)/weak_libjack.h
	mkdir -p $(BUILD)
	$(CC) -c -o $(BUILD)/weak_libjack.o $(SRC)/weak_libjack.c $(CFLAGS) `pkg-config --cflags --libs jack` -DNDEBUG
	$(CC) -c -o $(BUILD)/jack_playfile.o $(SRC)/jack_playfile.c $(CFLAGS) `pkg-config --cflags --libs sndfile opusfile`
	$(CC) -o jack_playfile $(BUILD)/jack_playfile.o $(BUILD)/weak_libjack.o $(CFLAGS) `pkg-config --cflags --libs sndfile opusfile vorbisfile` -lpthread -ldl -lzita-resampler -lmpg123

jack_playfile_static: $(SRC)/jack_playfile.c $(SRC)/jack_playfile.h $(SRC)/common.h $(SRC)/buffers.h $(SRC)/config.h $(SRC)/control.h $(SRC)/jackaudio.h $(SRC)/resampler.h $(SRC)/sndin.h $(SRC)/weak_libjack.c $(SRC)/weak_libjack.h
#	@echo $(STATIC_LIBS)
	$(CC) -c -o $(BUILD)/weak_libjack.o $(SRC)/weak_libjack.c $(CFLAGS) `pkg-config --cflags --libs jack` -DNDEBUG
	$(CC) -c -o $(BUILD)/jack_playfile.o $(SRC)/jack_playfile.c $(CFLAGS) `pkg-config --cflags --libs sndfile opusfile`
	$(CC) -static-libgcc -static-libstdc++ -o jack_playfile_static $(BUILD)/jack_playfile.o $(BUILD)/weak_libjack.o $(CFLAGS) $(STATIC_LIBS) -ldl -lpthread

	strip --strip-all jack_playfile_static
	readelf -h jack_playfile_static
	du -h jack_playfile_static

install: jack_playfile
	install -m755 jack_playfile $(INSTALLPATH)
#	install -m755 jack_playfile_static $(INSTALLPATH)

clean:
	-rm jack_playfile
	-rm jack_playfile_static
	-rm $(BUILD)/*.o
