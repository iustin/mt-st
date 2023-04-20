# Changelog

## Changes in version 1.7 (Thu, 20 Apr 2023)

Fixes a single bug in stinit parsing of invalid definitions. This is a
trivial bug, and only affects config files manually installed by root,
so the impact should be minimal.

The bug also does not appear on amd64/x86, but (in Debian) was only
triggered (as undefined behaviour) on mips64el, arm64 and s390x,
likely due to different platform behaviour.

## Changes in version 1.6 (Wed, 19 Apr 2023)

This is bugfix release agains 1.5. In between 1.4 and 1.5, the "make
check" target was migrated to using
[shelltest](https://github.com/simonmichael/shelltestrunner), but the
`make dist` and `distcheck` targets were no, so the built archive was
actually not. This only fixes that and has no functional code changes
from 1.5.

## Changes in version 1.5 (Wed, 19 Apr 2023)

Trivial release:

- add IBM 3590 B/E format to tape densities table (Chris Dinneen).

Thanks!

## Changes in version 1.4 (Sun, 30 Aug 2020)

Small bugfixes and improvements release:

- show default tape device in usage output (Iustin Pop).
- improve parsing of the stinit.def configuration file to detect and
  flag some of the possible errors, and add tests to prevent
  regressions (Iustin Pop).
- add LTO-8 (hrchu) and LTO-7 formatted as M8 (Iustin Pop) density
  codes.
- internal code improvements for issues flagged by Coverity scan
  warnings (Gris Ge).
- add bash auto-completion file (Paweł Marciniak).
- don't strip anymore binaries on installation, as nowadays this is
  the job of package managers (Dan Horák).

Thanks to all the contributors!

## Changes in version 1.3 (Sun, 01 May 2016)

Small bugfixes and improvements:

- add more density codes (Kai Mäkisara)
- check for overflow when using k, M or G suffixes (Kai Mäkisara)
- allow negative argument for mkpartition, supported by Linux 4.6 and
  later (Kai Mäkisara)
- fix compilation with musl libc (Felix Janda)
- allow configuring the tape device and installation paths (e.g. /bin
  vs. /usr/bin) at build/install time (Iustin Pop)
- code cleanups from the SUSE package (Alexey Svistunov)
- update the supplied example file (Alexey Svistunov)
- fix config file parsing bug in stinit (Iustin Pop)

## Changes in version 1.2 (Sun, 07 Feb 2016)

This a mostly a cleanup release after many years of no updates,
integrating pending fixes and distribution patches from Debian and
RedHat, and a change of maintainership:

- many updates to density codes (SDLT, LTO 5,6 and 7, etc.) (various
  people)
- multiple man page updates (various people)
- improve default tape device handling in `mt`: check that it actually
  is a character device, in order to show better error messages when
  `/dev/tape` is a different type (e.g. directory when using `udev`)
- small bug fix in stinit in parsing the input file (David Binderman)
- improve build system by allowing easier customisation of build flags
  and installation directory (via `DESTDIR`, not prefix) and by
  sanitising the creation of the dist archive (Iustin Pop)
- sanitise the source code to get rid of GCC warnings (Jan Christoph
  Nordholz, Iustin Pop)
- add `stshowoptions` alias to `stshowopt` (Ivo De Decker)
- expand the provided `stinit.def.examples` file (Suggested by
  Ralf-Peter Rohbeck)
- improve the `--help` output of stinit (Dan Horák)
- change of maintainership to Iustin Pop <iustin@k1024.org>

## Changes in version 1.1 (Sun, 27 Apr 2008)

- unused defines removed from `mtio.h` (compiles also with
  distributions not having `linux/qic117.h`)
- add support for `MT_ST_SILI` to mt and stinit
- add mt command `showoptions` for kernels >= 2.6.26
- fix mode number printing in stinit's verbose mode (from Martin Jacobs)

## Changes in version 0.9b (Sun, 21 Aug 2005)

- stinit: fix back out to `SCSI_IOCTL_SEND_COMMAND` for 2.4 kernels
  (2.4 uses errno `EINVAL` for unsupported ioctls)

## Changes in version 0.9 (Sun, 29 May 2005)

- mt: more density codes
- stinit: try first `SG_IO` for inquiry, if the ioctl fails, try
  `SCSI_IOCTL_SEND_COMMAND`; note that error checking for `SG_IO` is
  very simplistic for now

## Changes in version 0.8 (Tue, 13 Apr 2004)

- put man pages into `/usr/share/man/man1`, respectively
  `/usr/share/man/man8`
- in devfs, `/dev/tapes/tape<n>` does not match *n*th drive after
  rmmoding and insmodding the st driver; fix provided by Philippe
  Troin
- documentation cleanup
- add some density translations
- counts can use the `k`, `M`, or `G` postfix

## Changes in version 0.7 (Wed, 21 Nov 2001)

mt:

- add command `eject` for compatibility with GNU mt (synonym for
  `offline` and `rewoffl`)
- the `load` and `erase` commands accept an argument
- add `CLN` (cleaning request) to status
- add command `stsetcln` to set the cleaning request recognition options
- add the flag `no-wait` to the settable/clearable options
- some new density codes added

stinit:

- the directory scanning for tape devices is restricted to files with
  certain names in some directories to avoid triggering automatic
  module loading for device that don't exist (original patch from
  Philippe Troin)
- support for `devfs` (`/dev/tapes`) added
- logging bug fixes
- add setting the cleaning request parameter
- add setting the no-wait (immediate) bit

## Changes in version 0.6 (Thu, 30 Nov 2000)

mt:

- uses local `mtio.h` to include support for the most recent driver
  features even when compiled on a system having old `mtio.h`
- on-line and write-protect are checked after some errors and a
  message is printed if the probable error reason is found
- the tape is opened with flag `O_NONBLOCK` for commands that are
  useful even when the device is not ready (no tape)
- some new density codes added for printout
- OnStream drives using the `osst` driver recognised
- the obsolete command 'datcompression' is removed
- new option `--version`

stinit:

- fix the bug with whitespace at the beginning of lines in the
  configuration file
- use `O_NONBLOCK` to open the tape (anticipate kernel change)

## Changes in version 0.5b (Sun, 16 Aug 1998)

mt:

- corrected the bug that caused the command argument to be ignored if
  option `-f` was used
- density `0x45` (TR-4) added to known density list

stinit:

- added `#include <errno.h>` to enable compilation with glibc

## Changes in version 0.5

- utility stinit added to package
- GNU Public License used for both programs
- binaries not distributed any more

mt:

- command `asf` added
- command `datcompression` not compiled in default configuration
- support added for setting timeouts
- bugs in argument parsing corrected
- help prints all commands
- some code cleanup

## Changes in version 0.4

- support for the ioctls for partitioned tapes
- compiles also with 1.2.13
- the driver options can be specified also with keywords
- floppy tape type is shown
- (not working) support for other operating systems removed

## Changes in version 0.3

- support for new ioctls
- accepts hexadecimal numbers with prefix `0x`
- the datcompression command improved (although it is being overridden
  by the command compression using a new ioctl)
- bus fixes
