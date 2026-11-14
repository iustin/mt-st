# Changelog

## Unreleased changes

This release fixes a number of bugs in stinit's definition file
parsing, makes argument checking stricter in both tools, and includes
documentation and bash completion cleanups. Some of the fixes change
which settings stinit applies to the drives, so please read the
significant changes below.

### Significant changes

**mt: numeric arguments are now strictly checked.** Previously,
invalid, empty or out of range numbers were silently converted to a
different value, and the command was then run with it and reported
success: for example `mt fsf abc` or `mt seek ""` used 0,
`mt fsf 5000000000` skipped 705032704 files (the number was truncated to
32 bits), `mt sttimeout abc` set the timeout to 0, `mt defblksize 64k`
set the default block size to 64 bytes, and `mt asf abc` rewound the
tape.

Such arguments are now rejected with an error and exit code 3, without
touching the tape or the driver settings. Valid numbers (including the
documented `-1` for the `def*` commands) work as before.

If you have seen unexplained tape positioning or driver settings in the
past, this might be the cause. Scripts that unknowingly relied on the
old behaviour (e.g. passing an empty variable) will now fail (Iustin
Pop).

**stinit: values directly before a closing brace are no longer
ignored.** A value placed directly before the closing brace of a
definition, as in `mode1 blocksize=0 compression=1}`, was silently
dropped: `stinit -p` reported an error, but a normal run simply didn't
apply that setting. Such values are now parsed correctly, so settings
that were previously ignored will now be applied to the drive. If your
`stinit.def` has definitions written this way, check that these settings
are what you want (Iustin Pop).

**stinit: failures of the SCSI INQUIRY command are now detected.** The
status of the INQUIRY command, used to identify the drive, is now
checked correctly; previously, a failed command was not detected, and
stinit went on using whatever data it got back. As a consequence, stinit
may now refuse to configure a drive whose INQUIRY command reports an
error (Chris Dinneen).

**stinit: definition blocks without modes are ignored, and always have
been.** Previous versions of the manual page and of the example file
incorrectly said that a block without any `modeN` keyword (and with no
identification keywords) can be used to define common parameters for
all devices. If your `stinit.def` relies on this, those parameters have
never been applied; move them into each device block, before the first
`modeN` keyword. The manual page and the example file now describe the
actual behaviour (Iustin Pop).

### Bug fixes

- stinit: many fixes in the definition file parsing: a final `}` was
  lost when the last line of the file didn't end with a newline, lines
  longer than 254 characters were misparsed (they are now reported as
  errors), a too long definition overflowed a stack buffer (now also
  reported as an error; as the definition file is installed by root, the
  impact should be minimal), and a stray `mo` at the end of a maximum
  size definition made the parser read past the end of a buffer (now
  reported as an illegal mode) (Iustin Pop).
- stinit: the error message for a failed SCSI INQUIRY could overflow a
  stack buffer when the device name was very long (Iustin Pop).
- Stricter argument checking: both mt and stinit now only accept the
  exact `--version` option (previously, any option starting with `--v`,
  e.g. `--verbose`, printed the version), and `mt stshowoptions` now
  rejects arguments, which were previously silently ignored (Chris
  Dinneen, and Iustin Pop for stinit).
- mt: remove the undocumented `cleaning` option of `stoptions`,
  `stsetoptions` and `stclearoptions`, which sent invalid values to the
  driver and always failed; use `stsetcln` instead (Chris Dinneen).

### Minor changes

- Documentation fixes: describe the quick (non-secure) erase via
  `erase 0`, fix the stinit `fast-eod` keyword name and other errors in
  the manual pages, and use `fast-eod` instead of the older `fast-mteom`
  spelling in the example file (both are accepted by stinit) (Chris
  Dinneen).
- Bash completion improvements: complete the same keywords for
  `stoptions`, `stsetoptions` and `stclearoptions`, allow completing
  several of them, only match keywords from their beginning, and
  complete the missing `-t` and `--version` options (Chris Dinneen).
- mt: use a bounded write when building the sysfs path in
  `stshowoptions` (hardening, Chris Dinneen).
- Development: document the definition file parsing code, make UBSan
  findings fail the CI sanitizers job, and add a `make san-check` target
  to run the sanitizer tests locally (Iustin Pop).

Thanks Chris for the many fixes and cleanups!

## Changes in version 1.8 (Sat, 08 Feb 2025)

After almost two years, a number of minor fixes and improvements have accumulated:

- A number of density code updates: LTO-9, 9track Reels, IBM 3480 and 3490E
  (from Chris Dinneen) and IBM 3592 (Kai Mäkisara)
- Fix write protect reporting (Kai Mäkisara)
- Implement support for the `MT_ST_NOWAIT_EOF` flag, via the new `weof-no-wait`
  for stinit (Kai Mäkisara)
- While at it, correctly report errors if some options cannot be set, rather
  than pretending all is good (also Kai Mäkisara)
- Improve the bash completion code to handle option completion better (Chris
  Dinneen)
- Very minor build system improvements and coverage extension (Iustin Pop)

Thanks Kai and Chris for the many improvements!

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
