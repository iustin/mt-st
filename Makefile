CFLAGS?=  -Wall -O2
SBINDIR= $(DESTDIR)/sbin
BINDIR=  $(DESTDIR)/bin
USRBINDIR= $(DESTDIR)/usr/bin
MANDIR= $(DESTDIR)/usr/share/man

MTDIR=$(BINDIR)

PROGS=mt stinit


# Release-related variables
DISTFILES = \
	CHANGELOG.md \
	COPYING \
	Makefile \
	mt.1 \
	mt.c \
	mtio.h \
	README.md \
	stinit.8 \
	stinit.c \
	stinit.def.examples

VERSION=1.2
RELEASEDIR=mt-st-$(VERSION)
TARFILE=mt-st-$(VERSION).tar.gz

all:	$(PROGS)

version.h: Makefile
	echo '#define VERSION "$(VERSION)"' > $@

%: %.c version.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $<

install: $(PROGS)
	install -d $(MTDIR) $(MANDIR) $(SBINDIR)
	install -s mt $(MTDIR)
	install -c -m 444 mt.1 $(MANDIR)/man1
	(if [ -f $(MANDIR)/man1/mt.1.gz ] ; then \
	  rm -f $(MANDIR)/man1/mt.1.gz; gzip $(MANDIR)/man1/mt.1; fi)
	install -s stinit $(SBINDIR)
	install -c -m 444 stinit.8 $(MANDIR)/man8
	(if [ -f $(MANDIR)/man8/stinit.8.gz ] ; then \
	  rm -f $(MANDIR)/man8/stinit.8.gz; gzip $(MANDIR)/man8/stinit.8; fi)

dist:
	BASE=`mktemp -d` && \
	trap "rm -rf $$BASE" EXIT && \
	DIST="$$BASE/$(RELEASEDIR)" && \
	mkdir "$$DIST" && \
	install -m 0644 -p -t "$$DIST/" $(DISTFILES) && \
	tar czvf $(TARFILE) -C "$$BASE" \
	  --owner root --group root \
	  $(RELEASEDIR)

distcheck: dist
	BASE=`mktemp -d` && \
	trap "rm -rf $$BASE" EXIT && \
	tar xvf $(TARFILE) -C "$$BASE" && \
	cd "$$BASE/$(RELEASEDIR)" && \
	make && ./mt --version && ./stinit --version && \
	make dist

release-tag:
	git tag -s -m 'Release version $(VERSION).' mt-st-$(VERSION)

clean:
	rm -f *~ \#*\# *.o $(PROGS) version.h

.PHONY: dist distcheck clean
