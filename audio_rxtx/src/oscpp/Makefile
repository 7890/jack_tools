CC ?= gcc
CFLAGS ?= `pkg-config --libs --cflags liblo` -D_GNU_SOURCE -lm
PREFIX ?= /usr/local
INSTALLDIR ?= $(PREFIX)/bin

SRC = .
BLD = build

default: compile

compile: $(SRC)/oscpp.c

	@echo ""
	@echo "compiling jack_audio_send, jack_audio_recieve"
	@echo "---------------------------------------------"
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

	$(CC) -o $(BLD)/oscpp $(SRC)/oscpp.c $(CFLAGS)

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
	@echo "installing oscpp"
	@echo "----------------------------------------------"
	@echo ""
	@echo "INSTALLDIR: $(INSTALLDIR)"
	@echo ""
	@echo "'make install' needs to be run with root privileges, i.e."
	@echo ""
	@echo "sudo make install"
	@echo ""

	mkdir -p $(DESTDIR)$(INSTALLDIR)/
	install -m755 $(BLD)/oscpp $(DESTDIR)$(INSTALLDIR)/

	@echo ""
	@echo "done!"
	@echo ""

uninstall:
	@echo ""
	@echo "uninstalling oscpp"
	@echo "------------------------------------------------"
	@echo ""
	@echo "INSTALLDIR: $(INSTALLDIR)"
	@echo ""
	@echo "'make uninstall' needs to be run with root privileges, i.e."
	@echo ""
	@echo "sudo make uninstall"
	@echo ""

	rm -f $(DESTDIR)$(INSTALLDIR)/oscpp
	-rmdir $(DESTDIR)$(INSTALLDIR)

	@echo ""
	@echo "done."
	@echo ""

.PHONY: clean install uninstall
