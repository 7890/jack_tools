CC ?= gcc
CFLAGS ?= `pkg-config --libs --cflags jack liblo` -D_GNU_SOURCE -lm
PREFIX ?= /usr/local
INSTALLDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man/man1

SRC = ./
BLD = build
DOC = doc

#.deb package
SRC_URL="https://github.com/7890/jack_tools"
MAINTAINER="tom@trellis.ch"
LICENSE="GPL"
VERSION=0
RELEASE=1

default: compile

all: compile manpage

compile: $(SRC)/osc_bridge_in.c $(SRC)/osc_bridge_out.c $(SRC)/osc_filter.c $(SRC)/gen.c

	@echo ""
	@echo "compiling jack_osc_bridge_in, jack_osc_bridge_out, jack_osc_filter, jack_gen"
	@echo "----------------------------------------------------------------------------"
	@echo ""
	@echo "CC        : $(CC)"
	@echo "CFLAGS    : $(CFLAGS)"
	@echo "PREFIX    : $(PREFIX)"
	@echo "INSTALLDIR: $(INSTALLDIR)"
#	@echo "MANDIR    : $(MANDIR)"
	@echo ""
	@echo "to change these variables either edit the Makefile or use i.e.:"
	@echo "sudo make install PREFIX=/usr"
	@echo ""

	mkdir -p $(BLD)

	$(CC) -o $(BLD)/jack_osc_bridge_in $(SRC)/osc_bridge_in.c $(CFLAGS)
	$(CC) -o $(BLD)/jack_osc_bridge_out $(SRC)/osc_bridge_out.c $(CFLAGS)
	$(CC) -o $(BLD)/jack_osc_filter $(SRC)/osc_filter.c $(CFLAGS)
	$(CC) -o $(BLD)/jack_gen $(SRC)/gen.c $(CFLAGS)

	@echo ""
	@echo "done. next (if there were no errors) is: sudo make install"
	@echo ""

manpage:
	@echo ""
	@echo "creating manpage with asciidoc"
	@echo "------------------------------"
	@echo ""

#	a2x --doctype manpage --format manpage $(DOC)/jack_audio_send.man.asciidoc
#	gzip -9 -f $(DOC)/jack_audio_send.1

	@echo ""
	@echo "done."
	@echo ""

clean:
	rm -f ./$(BLD)/*.o
	rm -f ./$(BLD)/jack_osc_*
	rm -f ./$(BLD)/jack_gen*

	@echo ""
	@echo "done."
	@echo ""

install: 
	@echo ""
	@echo "installing jack_osc_bridge_in, jack_osc_bridge_out, jack_osc_filter, jack_gen"
	@echo "-----------------------------------------------------------------------------"
	@echo ""
	@echo "INSTALLDIR: $(INSTALLDIR)"
	@echo ""
	@echo "'make install' needs to be run with root privileges, i.e."
	@echo ""
	@echo "sudo make install"
	@echo ""

	mkdir -p $(DESTDIR)$(INSTALLDIR)/
#	mkdir -p $(DESTDIR)$(MANDIR)/
	install -m755 $(BLD)/jack_osc_bridge_in $(DESTDIR)$(INSTALLDIR)/
	install -m755 $(BLD)/jack_osc_bridge_out $(DESTDIR)$(INSTALLDIR)/
	install -m755 $(BLD)/jack_osc_filter $(DESTDIR)$(INSTALLDIR)/
	install -m755 $(BLD)/jack_gen $(DESTDIR)$(INSTALLDIR)/
#	install -m644 $(DOC)/jack_audio_send.1.gz $(DESTDIR)$(MANDIR)/

	@echo ""
	@echo "done!"
	@echo ""

uninstall:
	@echo ""
	@echo "uninstalling jack_osc_bridge_in, jack_osc_bridge_out, jack_osc_filter, jack_gen"
	@echo "-------------------------------------------------------------------------------"
	@echo ""
	@echo "INSTALLDIR: $(INSTALLDIR)"
	@echo ""
	@echo "'make uninstall' needs to be run with root privileges, i.e."
	@echo ""
	@echo "sudo make uninstall"
	@echo ""

	rm -f $(DESTDIR)$(INSTALLDIR)/jack_osc_bridge_in
	rm -f $(DESTDIR)$(INSTALLDIR)/jack_osc_bridge_out
	rm -f $(DESTDIR)$(INSTALLDIR)/jack_osc_filter
	rm -f $(DESTDIR)$(INSTALLDIR)/jack_gen

#	rm -f $(DESTDIR)$(MANDIR)/jack_audio_send.1.gz
#	rm -f $(DESTDIR)$(MANDIR)/jack_audio_receive.1.gz

	@echo ""
	@echo "done."
	@echo ""

deb64:
#	checkinstall -D --arch=amd64 --pkgname=audio-rxtx --pkgsource=$(SRC_URL) --pkgversion=$(VERSION) --pkgrelease=$(RELEASE) \
#	--maintainer=$(MAINTAINER) --pkglicense=$(LICENSE) --pkggroup="audioutils" --requires="liblo7,jackd" --install=no make install
#	sudo make deb64 PREFIX=/usr

deb32:
#	checkinstall -D --arch=i386 --pkgname=audio-rxtx --pkgsource=$(SRC_URL) --pkgversion=$(VERSION) --pkgrelease=$(RELEASE) \
#	--maintainer=$(MAINTAINER) --pkglicense=$(LICENSE) --pkggroup="audioutils" --requires="liblo7,jackd" --install=no make install
#	sudo make deb32 PREFIX=/usr

debarmhf: 
#	checkinstall -D --arch=armhf --pkgname=$(PROGNAME) --pkgsource=$(SRC_URL) --pkgversion=$(VERSION) --pkgrelease=$(RELEASE) \
#	--maintainer=$(MAINTAINER) --pkglicense=$(LICENSE) --pkggroup="shellutils" --requires="liblo7,libncurses5" --install=no make install
#	sudo make debarmhf PREFIX=/usr

.PHONY: all
