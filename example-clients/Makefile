CC ?= gcc
CFLAGS ?= `pkg-config --libs --cflags jack liblo` -D_GNU_SOURCE -lm -Wno-deprecated-declarations
CFLAGS_JACK_ONLY ?= `pkg-config --libs --cflags jack` -D_GNU_SOURCE -lm -Wno-deprecated-declarations

CFLAGS_METADATA ?= -DHAS_JACK_METADATA_API
#CFLAGS_METADATA = 

PREFIX ?= /usr/local
INSTALLDIR ?= $(PREFIX)/bin
#MANDIR ?= $(PREFIX)/share/man/man1

#http://gcc.gnu.org/onlinedocs/gcc-3.4.3/gcc/Warning-Options.html

SRC = .
BLD = build
DOC = doc

default: compile

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

	$(CC) -o $(BLD)/jack_xlsp xlsp.c $(CFLAGS_JACK_ONLY)
	$(CC) -o $(BLD)/jack_oscev oscev.c $(CFLAGS)
	$(CC) -o $(BLD)/jack_tty tty.c $(CFLAGS)

#########
#	$(CC) -o $(BLD)/jack_gen $(SRC)/gen.c $(CFLAGS) $(CFLAGS_METADATA)
	g++ -o $(BLD)/jack_gen $(SRC)/gen.c $(CFLAGS) $(CFLAGS)

	$(CC) -o $(BLD)/jack_osc_bridge_in $(SRC)/osc_bridge_in.c $(CFLAGS) $(CFLAGS_METADATA)
	$(CC) -o $(BLD)/jack_osc_bridge_out $(SRC)/osc_bridge_out.c $(CFLAGS) $(CFLAGS_METADATA)
	$(CC) -o $(BLD)/jack_osc_filter $(SRC)/osc_filter.c $(CFLAGS) $(CFLAGS_METADATA)

	@echo ""
	@echo "done. next (if there were no errors) is: sudo make install"
	@echo ""

clean:
	rm -rf $(BLD)/*

	@echo ""
	@echo "done."
	@echo ""

install: 
	@echo ""
	@echo "install"
	@echo "-------"
	@echo ""
	@echo "jack_osc_bridge_in, jack_osc_bridge_out, jack_osc_filter"
	@echo "jack_gen"
	@echo ""
	@echo ""
	@echo "INSTALLDIR: $(INSTALLDIR)"
	@echo ""
	@echo "'make install' needs to be run with root privileges, i.e."
	@echo ""
	@echo "sudo make install"
	@echo ""

	mkdir -p $(DESTDIR)$(INSTALLDIR)/
#	mkdir -p $(DESTDIR)$(MANDIR)/

	install -m755 $(BLD)/jack_xlsp $(DESTDIR)$(INSTALLDIR)/
	install -m755 $(BLD)/jack_oscev $(DESTDIR)$(INSTALLDIR)/

	install -m755 $(BLD)/jack_osc_bridge_in $(DESTDIR)$(INSTALLDIR)/
	install -m755 $(BLD)/jack_osc_bridge_out $(DESTDIR)$(INSTALLDIR)/
	install -m755 $(BLD)/jack_osc_filter $(DESTDIR)$(INSTALLDIR)/
	install -m755 $(BLD)/jack_gen $(DESTDIR)$(INSTALLDIR)/

	@echo ""
	@echo "done!"
	@echo ""

uninstall:
	@echo ""
	@echo "uninstall"
	@echo "---------"
	@echo ""
	@echo "jack_osc_bridge_in, jack_osc_bridge_out, jack_osc_filter"
	@echo "jack_gen
	@echo ""
	@echo "INSTALLDIR: $(INSTALLDIR)"
	@echo ""
	@echo "'make uninstall' needs to be run with root privileges, i.e."
	@echo ""
	@echo "sudo make uninstall"
	@echo ""

	rm -f $(DESTDIR)$(INSTALLDIR)/jack_xlsp
	rm -f $(DESTDIR)$(INSTALLDIR)/jack_oscev

	rm -f $(DESTDIR)$(INSTALLDIR)/jack_osc_bridge_in
	rm -f $(DESTDIR)$(INSTALLDIR)/jack_osc_bridge_out
	rm -f $(DESTDIR)$(INSTALLDIR)/jack_osc_filter
	rm -f $(DESTDIR)$(INSTALLDIR)/jack_gen

	@echo ""
	@echo "done."
	@echo ""

.PHONY: clean install uninstall
