CC ?= gcc
CFLAGS ?= `pkg-config --libs --cflags liblo` -D_GNU_SOURCE=1 -DUSE_WEAK_JACK=1 -lm -ldl
# -ldl

PREFIX ?= /usr/local
PREFIX_PACKAGES ?= /usr
INSTALLDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man/man1
PROGNAME ?= audio-rxtx

SRC = src
BLD = build
DOC = doc
DIST = dist

#.deb package
SRC_URL ?= "https://github.com/7890/jack_tools"
MAINTAINER ?= "Thomas Brand \<tom@trellis.ch\>"
LICENSE ?= "GPL2"
VERSION ?= 0
RELEASE ?= 83
RELEASE_DATE ?= 141024
#remove for now
#REQUIRES ?= "liblo7, jackd, libjack0, libc6"
REQUIRES ?= "libc6"

PKG_GROUP ?= "sound"

ARCH ?= "i386"
#ARCH ?= "amd64"
#ARCH ?= "armhf"

default: compile

all: compile manpage

compile: 
	@echo ""
	@echo "compiling jack_audio_send, jack_audio_recieve, audio_post_send"
	@echo "--------------------------------------------------------------"
	@echo ""
	@echo "CC        : $(CC)"
	@echo "CFLAGS    : $(CFLAGS)"
	@echo "PREFIX    : $(PREFIX)"
	@echo "INSTALLDIR: $(INSTALLDIR)"
	@echo "MANDIR    : $(MANDIR)"
	@echo ""
	@echo "to change these variables either edit the Makefile or use i.e.:"
	@echo "sudo make install PREFIX=/usr"

	mkdir -p $(BLD)

	@echo ""
	@echo "/!\\ audio_rxtx needs liblo fixmax branch for full operability"
	@echo "https://github.com/radarsat1/liblo/tree/fixmax"
	@echo "(alt.) https://github.com/7890/liblo/tree/fixmax"

	@echo ""
	@echo "testing for liblo version >= 0.28"
	pkg-config --atleast-version 0.28 liblo || [ $$? -eq 0 ]

	@echo ""
	@echo "compiling small lo program"
	$(CC) -o $(BLD)/test_lofixmax $(SRC)/test_lofixmax.c `pkg-config --libs --cflags liblo`

	@echo ""
	@echo "now testing for fixmax MAX_UDP_MSG_SIZE ~65k"
	$(BLD)/test_lofixmax || [ $$? -eq 0 ]

#weak libjack
	#weak libjack allows to determine at runtime if libjack.so.0 /.. is available -> no "hard" linking to libjack
	$(CC) -c -o $(BLD)/weak_libjack.o $(SRC)/weak_libjack.c $(CFLAGS) `pkg-config --libs --cflags jack`

#common
	$(CC) -c -o $(BLD)/jack_audio_common.o $(SRC)/jack_audio_common.c $(CFLAGS)

#send
	$(CC) -c -o $(BLD)/jack_audio_send.o $(SRC)/jack_audio_send.c $(CFLAGS)
	$(CC) -o $(BLD)/jack_audio_send $(BLD)/jack_audio_common.o $(BLD)/jack_audio_send.o $(BLD)/weak_libjack.o $(CFLAGS)

#receive
	$(CC) -c -o $(BLD)/jack_audio_receive.o $(SRC)/jack_audio_receive.c $(CFLAGS)
	$(CC) -o $(BLD)/jack_audio_receive $(BLD)/jack_audio_common.o $(BLD)/jack_audio_receive.o $(BLD)/weak_libjack.o $(CFLAGS)

#post_send
	#experimental
	$(CC) -c -o $(BLD)/audio_post_send.o $(SRC)/audio_post_send.c $(CFLAGS)
	$(CC) -o $(BLD)/audio_post_send $(BLD)/jack_audio_common.o $(BLD)/audio_post_send.o $(BLD)/weak_libjack.o $(CFLAGS)

	@echo ""
	@echo "done. next (if there were no errors) is: sudo make install"
	@echo ""

manpage:
	@echo ""
	@echo "creating manpage with asciidoc"
	@echo "------------------------------"
	@echo ""

	a2x --doctype manpage --format manpage $(DOC)/jack_audio_send.man.asciidoc
	gzip -9 -f $(DOC)/jack_audio_send.1
	a2x --doctype manpage --format manpage $(DOC)/jack_audio_receive.man.asciidoc
	gzip -9 -f $(DOC)/jack_audio_receive.1

	man -l -Tps $(DOC)/jack_audio_send.1.gz | ps2pdf -sPAPERSIZE=a4 - $(DOC)/jack_audio_send.pdf
	man -l -Tps $(DOC)/jack_audio_receive.1.gz | ps2pdf -sPAPERSIZE=a4 - $(DOC)/jack_audio_receive.pdf

	@echo ""
	@echo "done."
	@echo ""

clean:
	rm -rf $(BLD)
	rm -rf doc-pak
	rm -f description-pak

	@echo ""
	@echo "done."
	@echo ""

install: 
	@echo ""
	@echo "installing jack_audio_send, jack_audio_recieve"
	@echo "----------------------------------------------"
	@echo ""
	@echo "INSTALLDIR: $(INSTALLDIR)"
	@echo ""
	@echo "'make install' needs to be run with root privileges, i.e."
	@echo ""
	@echo "sudo make install"
	@echo ""

	mkdir -p $(DESTDIR)$(INSTALLDIR)/
	mkdir -p $(DESTDIR)$(MANDIR)/
	install -m755 $(BLD)/jack_audio_send $(DESTDIR)$(INSTALLDIR)/
	install -m755 $(BLD)/jack_audio_receive $(DESTDIR)$(INSTALLDIR)/
	install -m644 $(DOC)/jack_audio_send.1.gz $(DESTDIR)$(MANDIR)/
	install -m644 $(DOC)/jack_audio_receive.1.gz $(DESTDIR)$(MANDIR)/

	install -m755 $(BLD)/audio_post_send $(DESTDIR)$(INSTALLDIR)/

	@echo ""
	@echo "done!"
	@echo ""

uninstall:
	@echo ""
	@echo "uninstalling jack_audio_send, jack_audio_recieve"
	@echo "------------------------------------------------"
	@echo ""
	@echo "INSTALLDIR: $(INSTALLDIR)"
	@echo ""
	@echo "'make uninstall' needs to be run with root privileges, i.e."
	@echo ""
	@echo "sudo make uninstall"
	@echo ""

	rm -f $(DESTDIR)$(INSTALLDIR)/jack_audio_send
	rm -f $(DESTDIR)$(INSTALLDIR)/jack_audio_receive
	rm -f $(DESTDIR)$(MANDIR)/jack_audio_send.1.gz
	rm -f $(DESTDIR)$(MANDIR)/jack_audio_receive.1.gz

	rm -f $(DESTDIR)$(INSTALLDIR)/audio_post_send

	@echo ""
	@echo "done."
	@echo ""

prepare_checkinstall:
	mkdir -p doc-pak
	chmod 0755 doc-pak
	cp README.md doc-pak
	cp COPYING doc-pak/copyright

	chmod 0644 doc-pak/README.md
	chmod 0644 doc-pak/copyright

	mkdir -p doc-pak/doc
	chmod 0755 doc-pak/doc

	cp $(DOC)/jack_audio_receive.man.asciidoc doc-pak/doc
	cp $(DOC)/jack_audio_send.man.asciidoc doc-pak/doc
	cp $(DOC)/sequence_diagram.txt doc-pak/doc
	cp $(DOC)/description-pak .

	chmod 0644 doc-pak/doc/jack_audio_receive.man.asciidoc
	chmod 0644 doc-pak/doc/jack_audio_send.man.asciidoc
	chmod 0644 doc-pak/doc/sequence_diagram.txt

	@echo "done."
	@echo "next: sudo make deb ARCH=i386|amd64|armhf"

#set ARCH to i386, amd64 or armhf
deb:
	rm -f doc-pak/changelog.Debian.gz
	cp $(DOC)/changelog.Debian.$(ARCH) doc-pak/changelog.Debian
	gzip -9 doc-pak/changelog.Debian
	chmod 0644 doc-pak/changelog.Debian.gz

	checkinstall -D --exclude="/home/*" --arch=$(ARCH) --pkgname=$(PROGNAME) --pkgsource=$(SRC_URL) --pkgversion=$(VERSION) --pkgrelease=$(RELEASE) \
	--requires=$(REQUIRES) --maintainer=$(MAINTAINER) --pkglicense=$(LICENSE) --pkggroup=$(PKG_GROUP) --backup=no --install=no make install PREFIX=$(PREFIX_PACKAGES)

	@echo "done."
	@echo "next: make deb_dist ARCH=i386|amd64|armhf"

deb_dist:
	@echo "doing lintian check of deb, installation test, creating report..."

	mkdir -p $(DIST)
	mv $(PROGNAME)_$(VERSION)-$(RELEASE)_$(ARCH).deb $(BLD)/$(PROGNAME)_$(RELEASE_DATE)_$(VERSION)-$(RELEASE)_$(ARCH).deb
	$(DIST)/create_report.sh $(BLD)/$(PROGNAME)_$(RELEASE_DATE)_$(VERSION)-$(RELEASE)_$(ARCH).deb > $(BLD)/$(PROGNAME)_$(RELEASE_DATE)_$(VERSION)-$(RELEASE)_$(ARCH).deb.txt 2>&1

	@echo "done."
	@echo ""
	cat $(BLD)/$(PROGNAME)_$(RELEASE_DATE)_$(VERSION)-$(RELEASE)_$(ARCH).deb.txt
	@echo ""
	@echo "files created:"
	@echo "$(BLD)/$(PROGNAME)_$(RELEASE_DATE)_$(VERSION)-$(RELEASE)_$(ARCH).deb"
	@echo "$(BLD)/$(PROGNAME)_$(RELEASE_DATE)_$(VERSION)-$(RELEASE)_$(ARCH).deb.txt"
	@echo ""
	@echo "if package is ok to release, move to $(DIST) and add to repository"

.PHONY: compile manpage clean install uninstall prepare_checkinstall deb deb_dist
