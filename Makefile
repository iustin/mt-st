CFLAGS=  -Wall -O2
SBINDIR= /sbin
BINDIR=  /bin
MANDIR= /usr/man

all:	mt stinit

mt:	mt.c
	$(CC) $(CFLAGS) -o mt mt.c

stinit:	stinit.c
	$(CC) $(CFLAGS) -o stinit stinit.c

install: mt stinit
	install -s mt $(BINDIR)
	install -c -m 444 mt.1 $(MANDIR)/man1
	install -s stinit $(SBINDIR)
	install -c -m 444 stinit.8 $(MANDIR)/man8

dist:	clean
	(mydir=`basename \`pwd\``;\
	cd .. && tar cvvf - $$mydir | gzip -9 > $${mydir}.tar.gz)

clean:
	rm -f *~ \#*\# *.o mt stinit
