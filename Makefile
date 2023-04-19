CFLAGS?=  -Wall -O2
PREFIX?= /usr
EXEC_PREFIX?= /
SBINDIR= $(DESTDIR)/$(EXEC_PREFIX)/sbin
BINDIR=  $(DESTDIR)$(EXEC_PREFIX)/bin
DATAROOTDIR= $(DESTDIR)/$(PREFIX)/share
MANDIR= $(DATAROOTDIR)/man
COMPLETIONINSTALLDIR=$(DESTDIR)/etc/bash_completion.d
DEFTAPE?= /dev/tape
INSTALL= install

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
	mt-st.bash_completion \
	stinit.8 \
	stinit.c \
	stinit.def.examples \
	.dir-locals.el \
	.clang-format

VERSION=1.5
RELEASEDIR=mt-st-$(VERSION)
TARFILE=mt-st-$(VERSION).tar.gz

all:	$(PROGS)

version.h: Makefile
	echo '#define VERSION "$(VERSION)"' > $@

%: %.c version.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -DDEFTAPE='"$(DEFTAPE)"' -o $@ $<

install: $(PROGS)
	$(INSTALL) -d $(BINDIR)  $(SBINDIR) $(MANDIR) $(MANDIR)/man1 $(MANDIR)/man8 $(COMPLETIONINSTALLDIR)
	$(INSTALL) mt $(BINDIR)
	$(INSTALL) -m 444 mt.1 $(MANDIR)/man1
	$(INSTALL) -m 644 mt-st.bash_completion $(COMPLETIONINSTALLDIR)/mt-st
	(if [ -f $(MANDIR)/man1/mt.1.gz ] ; then \
	  rm -f $(MANDIR)/man1/mt.1.gz; gzip $(MANDIR)/man1/mt.1; fi)
	$(INSTALL) stinit $(SBINDIR)
	$(INSTALL) -m 444 stinit.8 $(MANDIR)/man8
	(if [ -f $(MANDIR)/man8/stinit.8.gz ] ; then \
	  rm -f $(MANDIR)/man8/stinit.8.gz; gzip $(MANDIR)/man8/stinit.8; fi)

dist:
	rm -f "$(TARFILE)" && \
	BASE=`mktemp -d` && \
	trap "rm -rf $$BASE" EXIT && \
	DIST="$$BASE/$(RELEASEDIR)" && \
	mkdir "$$DIST" && \
	$(INSTALL) -m 0644 -p -t "$$DIST/" $(DISTFILES) && \
	tar czvf $(TARFILE) -C "$$BASE" \
	  --owner root --group root \
	  $(RELEASEDIR)

distcheck: dist
	BASE=`mktemp -d` && \
	trap "rm -rf $$BASE" EXIT && \
	SRC="$$BASE/src" && mkdir "$$SRC" && \
	DST="$$BASE/dst" && mkdir "$$DST" && \
	tar xvf $(TARFILE) -C "$$SRC" && \
	cd "$$SRC/$(RELEASEDIR)" && \
	make CFLAGS="-Wall -Wextra -Werror" && \
	echo Checking version output && \
	./mt --version && ./stinit --version && \
	echo Checking parse status && \
	./stinit -p -f stinit.def.examples && \
	echo Checking complete stinit parsing && \
	( ./stinit -v -v -p -f stinit.def.examples 2>&1 | grep -q 'Mode 1 definition: scsi2logical=1 can-bsr=1 auto-lock=0 two-fms=0 drive-buffering=1 buffer-writes read-ahead=1 async-writes=1 can-partitions=0 fast-eom=1 blocksize=0 sili=1   timeout=900  long-timeout=14400     density=0x44 compression=0' ) && \
	make dist && \
	make install DESTDIR="$$DST" && \
	numfiles=$$( \
	find "$$DST" -type f \
	  \( -name mt -o -name stinit -o -name mt.1 -o -name stinit.8 -o -name mt-st \) | \
	  wc -l) && \
	echo "$$numfiles files installed (5 expected)" && \
	test "$$numfiles" -eq 5

check: $(PROGS)
	shelltest -DVERSION=$(VERSION) tests

# This needs lcov installed, and it's useful for local testing.
coverage: clean
	$(MAKE) CFLAGS=-coverage
	$(MAKE) check
	lcov --capture --directory . --no-external --output-file coverage.info
	genhtml coverage.info --output-directory out


release-tag:
	git tag -s -m 'Release version $(VERSION).' mt-st-$(VERSION)

clean:
	rm -f *~ \#*\# *.o *.gcno *.gcda coverage.info $(PROGS) version.h
	rm -rf out

reindent:
	clang-format -i mt.c stinit.c

.PHONY: dist distcheck clean reindent
