CC = g++
#CFLAGS ?= -D_GNU_SOURCE=1 -D__STDC_FORMAT_MACROS -DUSE_WEAK_JACK=1 -DNO_JACK_METADATA=1 -DRB_DISABLE_MLOCK -DRB_DISABLE_RW_MUTEX -DRB_DISABLE_SHM -Wno-write-strings -Wno-pointer-arith -O3
CFLAGS ?= -D_GNU_SOURCE=1 -D__STDC_FORMAT_MACROS -DUSE_WEAK_JACK=1 -DNO_JACK_METADATA=1 -DRB_DISABLE_MLOCK -DRB_DISABLE_RW_MUTEX -DRB_DEFAULT_USE_SHM -Wno-write-strings -Wno-pointer-arith -O3

#http://www.cprogramdevelop.com/4787258/

SRC = src
DOC = doc
BUILD = build
ARCHIVE = archive
RBURL = https://raw.githubusercontent.com/7890/csnip/master/rb/rb.h

PREFIX = /usr/local
INSTALLDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1

DISTRO_LIBS_DIR = /usr/lib/x86_64-linux-gnu
#DISTRO_LIBS_DIR = /usr/lib/i386-linux-gnu
LOCAL_LIBS_DIR = /usr/local/lib

STATIC_LIBS_DISTRO = -l:$(DISTRO_LIBS_DIR)/libsndfile.a -l:$(DISTRO_LIBS_DIR)/libFLAC.a -l:$(DISTRO_LIBS_DIR)/libvorbisenc.a -l:$(DISTRO_LIBS_DIR)/libvorbis.a -l:$(DISTRO_LIBS_DIR)/libogg.a -l:$(DISTRO_LIBS_DIR)/libvorbisfile.a -l:$(DISTRO_LIBS_DIR)/libopus.a
#custom install locaiton
LIB_OPUSFILE_A = -l:/usr/lib/libopusfile.a

#zita-resampler: ar -cvq libzita-resampler.a resampler.o vresampler.o resampler-table.o
#mpg123: ./configure --enable static

STATIC_LIBS_LOCAL = -l:$(LOCAL_LIBS_DIR)/libmpg123.a
#-l:$(LOCAL_LIBS_DIR)/libzita-resampler.a

STATIC_LIBS = $(LIB_OPUSFILE_A) $(STATIC_LIBS_DISTRO) $(STATIC_LIBS_LOCAL)

#-l:$(DISTRO_LIBS_DIR)/libdl.a
#-l:$(DISTRO_LIBS_DIR)/libpthread.a

$(shell mkdir -p $(BUILD))

default: jack_playfile

all:	jack_playfile jack_playfile_static manpage

zita_resampler:
	@echo "checking prerequisites..."
	which $(CC) \
	&& which bunzip2 \
	&& which tar
	@echo "ok."

	cp $(ARCHIVE)/zita-resampler-1.3.0.tar.bz2 $(BUILD)/ \
	&& cd $(BUILD)/ \
	&& bunzip2 -f zita-resampler-1.3.0.tar.bz2 \
	&& tar xf zita-resampler-1.3.0.tar --overwrite \
	&& cd zita-resampler-1.3.0/libs/ \
	&& make \
	&& cp *.o ../../

jack_playfile: zita_resampler $(SRC)/jack_playfile.c $(SRC)/jack_playfile.h $(SRC)/common.h $(SRC)/buffers.h $(SRC)/config.h $(SRC)/control.h $(SRC)/kb_control.h $(SRC)/jackaudio.h $(SRC)/resampler.h $(SRC)/sndin.h $(SRC)/weak_libjack.c $(SRC)/weak_libjack.h
	@echo "checking prerequisites..."
	which $(CC)
	which pkg-config
	pkg-config --exists jack
	pkg-config --exists sndfile
	pkg-config --exists opus
	pkg-config --exists opusfile
	pkg-config --exists vorbis
	pkg-config --exists vorbisfile
	pkg-config --exists ogg
	pkg-config --exists flac
	pkg-config --exists libmpg123
	@echo "ok."

	$(CC) -c -o $(BUILD)/weak_libjack.o $(SRC)/weak_libjack.c $(CFLAGS) `pkg-config --cflags --libs jack` -DNDEBUG -Wno-deprecated-declarations
	$(CC) -c -o $(BUILD)/jack_playfile.o $(SRC)/jack_playfile.c $(CFLAGS) -Ibuild/zita-resampler-1.3.0/libs/ `pkg-config --cflags --libs sndfile opusfile uuid`
	$(CC) -o jack_playfile $(BUILD)/jack_playfile.o $(BUILD)/weak_libjack.o $(BUILD)/resampler.o $(BUILD)/resampler-table.o $(CFLAGS) `pkg-config --cflags --libs sndfile opusfile vorbisfile uuid` -lrt -lpthread -ldl -lmpg123

jack_playfile_static: zita_resampler build_info $(SRC)/jack_playfile.c $(SRC)/jack_playfile.h $(SRC)/common.h $(SRC)/buffers.h $(SRC)/config.h $(SRC)/control.h $(SRC)/kb_control.h $(SRC)/jackaudio.h $(SRC)/resampler.h $(SRC)/sndin.h $(SRC)/manpage.h $(SRC)/weak_libjack.c $(SRC)/weak_libjack.h
#	@echo $(STATIC_LIBS)

	echo "checking prerequisites..."
	which zcat
	which pkg-config
	which xxd
	which groff
	which sed
	which strip
	which readelf
	which du
	pkg-config --exists jack
	pkg-config --exists sndfile
	pkg-config --exists opus
	pkg-config --exists opusfile
	pkg-config --exists vorbis
	pkg-config --exists vorbisfile
	pkg-config --exists ogg
	pkg-config --exists flac
	pkg-config --exists libmpg123
	@echo "ok."

	#create built-in man page
	zcat $(DOC)/jack_playfile.1.gz | groff -Tascii -man - > jack_playfile_man_dump \
	&& xxd -i jack_playfile_man_dump | sed 's/};/,0x00};\/\//g' | sed 's/;$$/+1;/g' \
	> $(BUILD)/manpage.data.h \
	&& rm -f jack_playfile_man_dump

	$(CC) -c -o $(BUILD)/weak_libjack.o $(SRC)/weak_libjack.c $(CFLAGS) `pkg-config --cflags --libs jack` -DNDEBUG -Wno-deprecated-declarations
	$(CC) -c -o $(BUILD)/jack_playfile.o $(SRC)/jack_playfile.c $(CFLAGS) -DSTATIC_BUILD=1 -DRB_DISABLE_SHM `pkg-config --cflags --libs sndfile opusfile`
	$(CC) -static-libgcc -static-libstdc++ -o jack_playfile_static $(BUILD)/jack_playfile.o $(BUILD)/weak_libjack.o $(BUILD)/resampler.o $(BUILD)/resampler-table.o $(CFLAGS) -DRB_DISABLE_SHM $(STATIC_LIBS) -ldl -lpthread

	strip --strip-all jack_playfile_static
	readelf -h jack_playfile_static
	du -h jack_playfile_static

manpage:
	@echo "checking prerequisites..."
	which asciidoc \
	&& asciidoc --filter list | grep graphviz \
	&& which a2x \
	&& which gzip \
	&& which dblatex
	@echo "ok."

	@echo ""
	@echo "creating manpage with asciidoc"
	@echo "------------------------------"
	@echo ""

	#man
	a2x --doctype manpage --format manpage $(DOC)/jack_playfile.man.asciidoc
	gzip -9 -f $(DOC)/jack_playfile.1

	#html
	asciidoc $(DOC)/jack_playfile.man.asciidoc

	#pdf, xml (docbook)
	a2x --format pdf --keep-artifacts --dblatex-opts " -P doc.layout=\"coverpage mainmatter\" -P doc.publisher.show=0" $(DOC)/jack_playfile.man.asciidoc

	mv $(DOC)/jack_playfile.man.pdf $(DOC)/jack_playfile.pdf
	mv $(DOC)/jack_playfile.man.xml $(DOC)/jack_playfile.xml
	mv $(DOC)/jack_playfile.man.html $(DOC)/jack_playfile.html

	@echo ""
	@echo "done."
	@echo ""

build_info:
	@echo "checking prerequisites..."
	which git \
	&& which pkg-config \
	&& which uname \
	&& which xxd
	@echo "ok."

	(echo   -n "build date        " \
	&& echo "`date`" \
	&& echo -n "git master        " \
	&& git show-ref master | head -1 \
	&& echo -n "cc                " \
	&& $(CC) --version | head -1 \
	&& echo -n "libsndfile        " \
	&& pkg-config --modversion sndfile \
	&& echo -n "libopus           " \
	&& pkg-config --modversion opus \
	&& echo -n "libopusfile       " \
	&& pkg-config --modversion opusfile \
	&& echo -n "libvorbisfile     " \
	&& pkg-config --modversion vorbisfile \
	&& echo -n "libogg            " \
	&& pkg-config --modversion ogg \
	&& echo -n "libflac           " \
	&& pkg-config --modversion flac \
	&& echo -n "libmpg123         " \
	&& pkg-config --modversion libmpg123 \
	&& echo "libzita-resampler 1.3.0" \
	&& echo -n "buildhost         " \
	&& uname -a) > build_info_dump \
	&& xxd -i build_info_dump | sed 's/};/,0x00};\/\//g' | sed 's/;$$/+1;/g' \
	> $(BUILD)/build_info.data.h \
	&& rm -f build_info_dump

update_rb:
	@echo "checking prerequisites..."
	which wget
	@echo "ok."

	@echo "updating rb.h with $(RBURL)?"
	@echo "/!\\ THIS WILL REPLACE ANY LOCAL MODIFICATIONS IN rb.h"
	@read a
	wget -O $(SRC)/rb.h $(RBURL)
	@echo "now make."

install:
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
