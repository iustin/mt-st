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

TESTFILES = $(wildcard tests/*.test)
TESTDATAFILES = $(wildcard tests/data/*)

VERSION=1.8
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
	mkdir "$$DIST/tests" && mkdir "$$DIST/tests/data" && \
	  $(INSTALL) -m 0644 -p -t "$$DIST/" $(DISTFILES) && \
	  $(INSTALL) -m 0644 -p -t "$$DIST/tests" $(TESTFILES) && \
	  $(INSTALL) -m 0644 -p -t "$$DIST/tests/data" $(TESTDATAFILES) && \
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
	make check && \
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

# Run the test suite under each of the sanitizers, like the "sanitizers"
# job in .github/workflows/ci.yml does; keep the flags below in sync with
# it. Each sanitizer needs a full rebuild, so this cleans the tree before
# each run and after the last one. On failure, the binaries of the failing
# build are left in place for debugging. A single sanitizer can be run
# with e.g. "make san-check-address".
SAN_CC ?= clang
SANITIZERS ?= address memory undefined
SAN_CFLAGS_address = -fsanitize=address -O1 -fno-omit-frame-pointer -g
SAN_CFLAGS_memory = -fsanitize=memory -fsanitize-memory-track-origins \
	-fPIE -pie -fno-omit-frame-pointer -g -O2
SAN_CFLAGS_undefined = -fsanitize=undefined -fno-sanitize-recover=all
# An exit code not expected by any test (UBSan's default is 1).
SAN_ENV = UBSAN_OPTIONS=exitcode=99

san-check:
	$(foreach san,$(SANITIZERS),$(MAKE) san-check-$(san) && ) true
	$(MAKE) clean

san-check-%:
	@echo "=== Testing with the $* sanitizer ==="
	$(MAKE) clean
	$(MAKE) CC="$(SAN_CC)" CFLAGS="$(SAN_CFLAGS_$*)"
	$(SAN_ENV) $(MAKE) check

# This needs lcov installed, and it's useful for local testing.
coverage: clean
	$(MAKE) CFLAGS=-coverage
	$(MAKE) check
	lcov --capture --directory . --no-external --output-file lcov.info
	genhtml lcov.info --output-directory out


release-tag:
	git tag -s -m 'Release version $(VERSION)' v$(VERSION)

clean:
	rm -f *~ \#*\# *.o *.gcno *.gcda coverage.info $(PROGS) version.h
	rm -rf out

reindent:
	clang-format -i mt.c stinit.c

.PHONY: dist distcheck clean reindent san-check
