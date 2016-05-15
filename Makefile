PREFIX=/usr/local
EXEC_PREFIX=$(PREFIX)
BINDIR=$(EXEC_PREFIX)/bin
LIBDIR=$(EXEC_PREFIX)/lib
MANDIR=$(PREFIX)/man
INCLUDEDIR=$(PREFIX)/include

CC=gcc
INSTALL=/usr/bin/install -c
INSTALL_DATA=$(INSTALL) -m 644

CFLAGS=-O2 -fPIC -Wall -pipe -I. -DXOSD_VERSION=\"$(VERSION)\" -I/usr/X11R6/include
#CFLAGS=-ggdb -fPIC -Wall -pipe -I. -DXOSD_VERSION=\"$(VERSION)\" -I/usr/X11R6/include
LDFLAGS=-L. -fPIC -L/usr/X11R6/lib -lX11 -lXext -lpthread -lXt

XOSDLIBS=-lxosd

SOURCES=NEWS AUTHORS ChangeLog README COPYING Makefile \
	osd_monitors.c osd_monitors.1

ARFLAGS=cru

all: osd_monitors

%.o: %.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

%.o.pic: %.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@ -fPIC

osd_monitors: osd_monitors.o
	$(CC) -o $@ osd_monitors.o $(LDFLAGS) $(XOSDLIBS)

tar: xosd-$(VERSION).tar.gz

install: all
	$(INSTALL) osd_monitors $(BINDIR)
	mkdir -p $(MANDIR)/man1 $(MANDIR)/man3
	$(INSTALL_DATA) osd_monitors.1 $(MANDIR)/man1/

clean:
	rm -f *~ *.o *.o.pic osd_monitors tags

.PHONY: all tar clean install
# vim: noexpandtab
