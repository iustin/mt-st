# mt-st tools

This directory contains two programs; `mt` and `stinit`, used for
dealing with Linux-specific tape-drive handling.

The project was authored and is copyright by Kai Mäkisara
(<Kai.Makisara@kolumbus.fi>), and since version 1.2 is maintained by
Iustin Pop (<iustin@k1024.org>). For copyright information, see the
`COPYING` file.

## mt

`mt` is basically a "standard" mt with additional commands to send the
ioctls specific to the Linux SCSI tape driver. The source supports all
SCSI tape ioctls up to kernel version 2.6.0 but it can also be
compiled in kernels >= 2.0.x (and hopefully with 1.2.x). Although this
mt program is tailored for SCSI tapes, it can also be used with other
Linux tape drivers using the same ioctls (some of the commands may not
work with all drivers).

## stinit

The program `stinit` is meant for initializing of SCSI tape drive modes
at system startup, when the tape driver module is loaded, or when new
tape drivers are initialized using:

    echo "scsi add-single-device x y z v" >/proc/scsi/scsi

or (with 2.6 kernels):

    echo "y z v" > /sys/class/scsi_host/hostx/scan

where `x`=host `y`=channel `z`=id `v`=lun (`-` is wild card for 2.6).

The parameters used in initialization of a tape drive are fetched from
a text file. The parameter file is indexed by the inquiry data
returned by the drive, i.e., the parameters are defined by the drive
manufacturer, model, etc. This means that the initialization for a
drive does not depend on its hardware address. A similar method is
used by most Unices either within the kernel or outside the kernel.

The contents of the configuration file and the command line parameters
are defined in the man page `stinit.8`. A sample configuration file
`stinit.def.examples` is included in this distribution. It can be used
as example when writing descriptions for the tape drives in a
system. NOTE that the examples by no means specify what are the
"correct" parameters for different types of devices.

The program is configured for maximum of 32 tapes and 4 modes (the
default Linux configuration). If the kernel is configured for
different number of tape modes, the definitions `MAX_TAPES` and
`NBR_MODES` in `stinit.c` should be configured accordingly. (With 8 bit
minor numbers `NBR_MODES * MAX_TAPES == 128`.)

## Contents

The files:

- `README.md`: This file.
- `CHANGELOG.md`: Changes between versions.
- `COPYING`: The GNU Public License
- `Makefile`: Makefile for programs
- `mt.c`: The mt source
- `mt.1`: The man page for mt
- `mtio.h`: The tape command definitions
- `qic117.h`: Needed by mtio.h
- `stinit.c`: The stinit source
- `stinit.8`: The man page for stinit
- `stinit.def.examples`: example configurations for different devices

## Installation

Really simple:

- review the makefile
- `make`
- `make install`
