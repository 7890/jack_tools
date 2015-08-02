CC = g++
CFLAGS ?= -D_GNU_SOURCE=1 -D__STDC_FORMAT_MACROS -DUSE_WEAK_JACK=1 -Wno-write-strings -Wno-pointer-arith
#-DNO_JACK_METADATA -w

#http://www.cprogramdevelop.com/4787258/

SRC ?= src
BUILD ?= build

INSTALLPATH=/usr/local/bin/

DISTRO_LIBS_DIR = /usr/lib/x86_64-linux-gnu
LOCAL_LIBS_DIR = /usr/local/lib

#--enable-static
#ar rcs x.a x.o

STATIC_LIBS_DISTRO = -l:$(DISTRO_LIBS_DIR)/libsndfile.a -l:$(DISTRO_LIBS_DIR)/libFLAC.a -l:$(DISTRO_LIBS_DIR)/libvorbisenc.a -l:$(DISTRO_LIBS_DIR)/libvorbis.a -l:$(DISTRO_LIBS_DIR)/libogg.a 
STATIC_LIBS_LOCAL = -l:$(LOCAL_LIBS_DIR)/libzita-resampler.a -l:$(LOCAL_LIBS_DIR)/libmpg123.a -l:$(LOCAL_LIBS_DIR)/libopusfile.a -l:$(LOCAL_LIBS_DIR)/libopus.a
STATIC_LIBS = $(STATIC_LIBS_DISTRO) $(STATIC_LIBS_LOCAL)

#-l:$(DISTRO_LIBS_DIR)/libdl.a
#-l:$(DISTRO_LIBS_DIR)/libpthread.a

default: jack_playfile

all:	jack_playfile jack_playfile_static

jack_playfile: $(SRC)/jack_playfile.c $(SRC)/jack_playfile.h $(SRC)/weak_libjack.c $(SRC)/weak_libjack.h
	mkdir -p $(BUILD)
	$(CC) -c -o $(BUILD)/weak_libjack.o $(SRC)/weak_libjack.c $(CFLAGS) `pkg-config --cflags --libs jack` -DNDEBUG
	$(CC) -c -o $(BUILD)/jack_playfile.o $(SRC)/jack_playfile.c $(CFLAGS) `pkg-config --cflags --libs sndfile opusfile`
	$(CC) -o jack_playfile $(BUILD)/jack_playfile.o $(BUILD)/weak_libjack.o $(CFLAGS) `pkg-config --cflags --libs sndfile opusfile` -lpthread -ldl -lzita-resampler -lmpg123

#	$(CC) -o jack_playfile $(SRC)/jack_playfile.c $(CFLAGS) `pkg-config --cflags --libs sndfile jack` -lpthread -lzita-resampler

#test
jack_playfile_static: $(SRC)/jack_playfile.c $(SRC)/jack_playfile.h $(SRC)/weak_libjack.c $(SRC)/weak_libjack.h
#	@echo $(STATIC_LIBS)
	$(CC) -c -o $(BUILD)/weak_libjack.o $(SRC)/weak_libjack.c $(CFLAGS) `pkg-config --cflags --libs jack` -DNDEBUG
	$(CC) -c -o $(BUILD)/jack_playfile.o $(SRC)/jack_playfile.c $(CFLAGS) `pkg-config --cflags --libs sndfile opusfile`
	$(CC) -static-libgcc -static-libstdc++ -o jack_playfile_static $(BUILD)/jack_playfile.o $(BUILD)/weak_libjack.o $(CFLAGS) $(STATIC_LIBS) -ldl -lpthread

	strip --strip-all jack_playfile_static
	readelf -h jack_playfile_static
	du -h jack_playfile_static

install: jack_playfile
	install -m755 jack_playfile $(INSTALLPATH)

clean:
	-rm jack_playfile
	-rm jack_playfile_static
	-rm $(BUILD)/*.o
