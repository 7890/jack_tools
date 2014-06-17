CC ?= gcc
CFLAGS ?= `pkg-config --libs --cflags jack liblo` -D_GNU_SOURCE -lm
PREFIX ?= /usr/local
INSTALLDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man/man1

SRC = src
BLD = build
DOC = doc
DIST = dist

#.deb package
SRC_URL="https://github.com/7890/jack_tools"
MAINTAINER="tom@trellis.ch"
LICENSE="GPL"
VERSION ?= 0
RELEASE ?= 2
RELEASE_DATE ?= 140618

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
	rm -f ./$(BLD)/*.o
	rm -f ./$(BLD)/jack_audio*

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

prepare_deb:
	mkdir -p doc-pak
	rm -rf doc-pak/*
	mkdir -p $(DIST)/
	cp $(DIST)/README.md doc-pak
	cp $(DOC)/jack_audio_receive.man.asciidoc doc-pak
	cp $(DOC)/jack_audio_send.man.asciidoc doc-pak
	cp $(DOC)/sequence_diagram.txt doc-pak

deb64:
	checkinstall -D --arch=amd64 --pkgname=audio-rxtx --pkgsource=$(SRC_URL) --pkgversion=$(VERSION) --pkgrelease=$(RELEASE) \
	--maintainer=$(MAINTAINER) --pkglicense=$(LICENSE) --pkggroup="audioutils" --requires="" --install=no make install
#	sudo make deb64 PREFIX=/usr	

deb64_dist:
	mkdir -p $(DIST)
	mv audio-rxtx_$(VERSION)-$(RELEASE)_amd64.deb  $(DIST)/audio-rxtx_$(RELEASE_DATE)_$(VERSION)-$(RELEASE)_amd64.deb
	$(DIST)/create_report.sh $(DIST)/audio-rxtx_$(RELEASE_DATE)_$(VERSION)-$(RELEASE)_amd64.deb > $(DIST)/audio-rxtx_$(RELEASE_DATE)_$(VERSION)-$(RELEASE)_amd64.deb.txt 2>&1

deb32:
	checkinstall -D --arch=i386 --pkgname=audio-rxtx --pkgsource=$(SRC_URL) --pkgversion=$(VERSION) --pkgrelease=$(RELEASE) \
	--maintainer=$(MAINTAINER) --pkglicense=$(LICENSE) --pkggroup="audioutils" --requires="" --install=no make install
#	sudo make deb32 PREFIX=/usr

deb32_dist:
	mkdir -p $(DIST)
	mv audio-rxtx_$(VERSION)-$(RELEASE)_i386.deb  $(DIST)/audio-rxtx_$(RELEASE_DATE)_$(VERSION)-$(RELEASE)_i386.deb
	$(DIST)/create_report.sh $(DIST)/audio-rxtx_$(RELEASE_DATE)_$(VERSION)-$(RELEASE)_i386.deb > $(DIST)/audio-rxtx_$(RELEASE_DATE)_$(VERSION)-$(RELEASE)_i386.deb.txt 2>&1

debarmhf: 
	checkinstall -D --arch=armhf --pkgname=$(PROGNAME) --pkgsource=$(SRC_URL) --pkgversion=$(VERSION) --pkgrelease=$(RELEASE) \
	--maintainer=$(MAINTAINER) --pkglicense=$(LICENSE) --pkggroup="audioutils" --requires="" --install=no make install
#	sudo make debarmhf PREFIX=/usr

debarmhf_dist:
	mkdir -p $(DIST)
	mv audio-rxtx_$(VERSION)-$(RELEASE)_armhf.deb  $(DIST)/audio-rxtx_$(RELEASE_DATE)_$(VERSION)-$(RELEASE)_armhf.deb
	$(DIST)/create_report.sh $(DIST)/audio-rxtx_$(RELEASE_DATE)_$(VERSION)-$(RELEASE)_armhf.deb > $(DIST)/audio-rxtx_$(RELEASE_DATE)_$(VERSION)-$(RELEASE)_armhf.deb.txt 2>&1

.PHONY: all
