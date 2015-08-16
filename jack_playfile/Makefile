CC = g++
CFLAGS ?= -D_GNU_SOURCE=1 -D__STDC_FORMAT_MACROS -DUSE_WEAK_JACK=1 -DNO_JACK_METADATA=1 -Wno-write-strings -Wno-pointer-arith
#-DNO_JACK_METADATA -w

#http://www.cprogramdevelop.com/4787258/

SRC = src
DOC = doc
BUILD = build
ARCHIVE = archive

PREFIX = /usr/local
INSTALLDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1

DISTRO_LIBS_DIR = /usr/lib/x86_64-linux-gnu
LOCAL_LIBS_DIR = /usr/local/lib

STATIC_LIBS_DISTRO = -l:$(DISTRO_LIBS_DIR)/libsndfile.a -l:$(DISTRO_LIBS_DIR)/libFLAC.a -l:$(DISTRO_LIBS_DIR)/libvorbisenc.a -l:$(DISTRO_LIBS_DIR)/libvorbis.a -l:$(DISTRO_LIBS_DIR)/libogg.a -l:$(DISTRO_LIBS_DIR)/libopus.a -l:$(DISTRO_LIBS_DIR)/libvorbisfile.a
#custom install locaiton
LIB_OPUSFILE_A = -l:/usr/lib/libopusfile.a

#zita-resampler: ar -cvq libzita-resampler.a resampler.o vresampler.o resampler-table.o
#mpg123: ./configure --enable static

STATIC_LIBS_LOCAL = -l:$(LOCAL_LIBS_DIR)/libmpg123.a 
#-l:$(LOCAL_LIBS_DIR)/libzita-resampler.a

STATIC_LIBS = $(LIB_OPUSFILE_A) $(STATIC_LIBS_DISTRO) $(STATIC_LIBS_LOCAL)

#-l:$(DISTRO_LIBS_DIR)/libdl.a
#-l:$(DISTRO_LIBS_DIR)/libpthread.a

default: jack_playfile

all:	jack_playfile jack_playfile_static manpage

zita_resampler:
	mkdir -p $(BUILD)
	cp $(ARCHIVE)/zita-resampler-1.3.0.tar.bz2 $(BUILD)/ \
	&& cd $(BUILD)/ \
	&& bunzip2 -f zita-resampler-1.3.0.tar.bz2 \
	&& tar xf zita-resampler-1.3.0.tar --overwrite \
	&& cd zita-resampler-1.3.0/libs/ \
	&& make \
	&& cp *.o ../../

#$(BUILD)/resampler.o $(BUILD)/resampler-table.o $(BUILD)/vresampler.o

jack_playfile: zita_resampler $(SRC)/jack_playfile.c $(SRC)/jack_playfile.h $(SRC)/common.h $(SRC)/buffers.h $(SRC)/config.h $(SRC)/control.h $(SRC)/kb_control.h $(SRC)/jackaudio.h $(SRC)/resampler.h $(SRC)/sndin.h $(SRC)/weak_libjack.c $(SRC)/weak_libjack.h
	mkdir -p $(BUILD)
	$(CC) -c -o $(BUILD)/weak_libjack.o $(SRC)/weak_libjack.c $(CFLAGS) `pkg-config --cflags --libs jack` -DNDEBUG
	$(CC) -c -o $(BUILD)/jack_playfile.o $(SRC)/jack_playfile.c $(CFLAGS) `pkg-config --cflags --libs sndfile opusfile`
	$(CC) -o jack_playfile $(BUILD)/jack_playfile.o $(BUILD)/weak_libjack.o $(BUILD)/resampler.o $(BUILD)/resampler-table.o $(CFLAGS) `pkg-config --cflags --libs sndfile opusfile vorbisfile` -lpthread -ldl -lmpg123


jack_playfile_static: zita_resampler $(SRC)/jack_playfile.c $(SRC)/jack_playfile.h $(SRC)/common.h $(SRC)/buffers.h $(SRC)/config.h $(SRC)/control.h $(SRC)/kb_control.h $(SRC)/jackaudio.h $(SRC)/resampler.h $(SRC)/sndin.h $(SRC)/manpage.h $(SRC)/weak_libjack.c $(SRC)/weak_libjack.h
#	@echo $(STATIC_LIBS)

	#create built-in man page
	zcat $(DOC)/jack_playfile.1.gz | groff -Tascii -man - > jack_playfile_man_dump \
	&& xxd -i jack_playfile_man_dump | sed 's/};/,0x00};\/\//g' | sed 's/;$$/+1;/g' \
	> $(BUILD)/manpage.data.h \
	&& rm -f jack_playfile_man_dump

	$(CC) -c -o $(BUILD)/weak_libjack.o $(SRC)/weak_libjack.c $(CFLAGS) `pkg-config --cflags --libs jack` -DNDEBUG
	$(CC) -c -o $(BUILD)/jack_playfile.o $(SRC)/jack_playfile.c $(CFLAGS) -DSTATIC_BUILD=1 `pkg-config --cflags --libs sndfile opusfile`
	$(CC) -static-libgcc -static-libstdc++ -o jack_playfile_static $(BUILD)/jack_playfile.o $(BUILD)/weak_libjack.o $(BUILD)/resampler.o $(BUILD)/resampler-table.o $(CFLAGS) $(STATIC_LIBS) -ldl -lpthread

	strip --strip-all jack_playfile_static
	readelf -h jack_playfile_static
	du -h jack_playfile_static

manpage:
	@echo ""
	@echo "creating manpage with asciidoc"
	@echo "------------------------------"
	@echo ""

	a2x --doctype manpage --format manpage --keep-artifacts $(DOC)/jack_playfile.man.asciidoc
	gzip -9 -f $(DOC)/jack_playfile.1

	docbook2pdf --backend pdf --output $(DOC)/ --nochunks $(DOC)/jack_playfile.man.xml
	mv $(DOC)/jack_playfile.man.pdf $(DOC)/jack_playfile.pdf
	rm -f $(DOC)/jack_playfile.man.xml
	rm -f $(DOC)/jack_playfile.man.html

#	man -l -Tps $(DOC)/jack_playfile.1.gz | ps2pdf -sPAPERSIZE=a4 - $(DOC)/jack_playfile.pdf

	@echo ""
	@echo "done."
	@echo ""


install: jack_playfile
	install -m755 jack_playfile $(DESTDIR)$(INSTALLDIR)/
#	install -m755 jack_playfile_static $(DESTDIR)$(INSTALLDIR)/
	install -m644 $(DOC)/jack_playfile.1.gz $(DESTDIR)$(MANDIR)/

uninstall:
	rm -f $(DESTDIR)$(INSTALLDIR)/jack_playfile
	rm -f $(DESTDIR)$(MANDIR)/jack_playfile.1.gz

clean:
	-rm jack_playfile
	-rm jack_playfile_static
	-rm -rf $(BUILD)/*
