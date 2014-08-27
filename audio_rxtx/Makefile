CC ?= gcc
CFLAGS ?= `pkg-config --libs --cflags jack liblo` -D_GNU_SOURCE -lm
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
RELEASE ?= 7
RELEASE_DATE ?= 140827
#remove for now
#REQUIRES ?= "liblo7, jackd, libjack0, libc6"
REQUIRES ?= "libc6"

PKG_GROUP ?= "sound"

ARCH ?= "i386"
#ARCH ?= "amd64"
#ARCH ?= "armhf"

default: compile

all: compile manpage

compile: $(SRC)/jack_audio_send.c $(SRC)/jack_audio_receive.c

	@echo ""
	@echo "compiling jack_audio_send, jack_audio_recieve"
	@echo "---------------------------------------------"
	@echo ""
	@echo "CC        : $(CC)"
	@echo "CFLAGS    : $(CFLAGS)"
	@echo "PREFIX    : $(PREFIX)"
	@echo "INSTALLDIR: $(INSTALLDIR)"
	@echo "MANDIR    : $(MANDIR)"
	@echo ""
	@echo "to change these variables either edit the Makefile or use i.e.:"
	@echo "sudo make install PREFIX=/usr"
	@echo ""

	mkdir -p $(BLD)

	$(CC) -c -o $(BLD)/jack_audio_common.o $(SRC)/jack_audio_common.c $(CFLAGS)

	$(CC) -c -o $(BLD)/jack_audio_receive.o $(SRC)/jack_audio_receive.c $(CFLAGS)
	$(CC) -o $(BLD)/jack_audio_receive $(BLD)/jack_audio_common.o $(BLD)/jack_audio_receive.o $(CFLAGS)

	$(CC) -c -o $(BLD)/jack_audio_send.o $(SRC)/jack_audio_send.c $(CFLAGS)
	$(CC) -o $(BLD)/jack_audio_send $(BLD)/jack_audio_common.o $(BLD)/jack_audio_send.o $(CFLAGS)

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
	mv $(PROGNAME)_$(VERSION)-$(RELEASE)_$(ARCH).deb  $(BLD)/$(PROGNAME)_$(RELEASE_DATE)_$(VERSION)-$(RELEASE)_$(ARCH).deb
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
