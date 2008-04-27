/*	This file contains the source of the 'mt' program intended for
	Linux systems. The program supports the basic mt commands found
	in most Unix-like systems. In addition to this the program
	supports several commands designed for use with the Linux SCSI
	tape drive.

	Maintained by Kai Mäkisara (email Kai.Makisara@kolumbus.fi)
	Copyright by Kai Mäkisara, 1998 - 2008. The program may be distributed
	according to the GNU Public License

	Last Modified: Sun Apr 27 19:49:00 2008 by kai.makisara
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include "mtio.h"

#ifndef DEFTAPE
#define DEFTAPE "/dev/tape"     /* default tape device */
#endif /* DEFTAPE */

#define VERSION "1.1"

typedef int (* cmdfunc)(/* int, struct cmdef_tr *, int, char ** */);

typedef struct cmdef_tr {
    char *cmd_name;
    int cmd_code;
    cmdfunc cmd_function;
    int cmd_count_bits;
    unsigned char cmd_fdtype;
    unsigned char arg_cnt;
    int error_tests;
} cmdef_tr;

#define NO_FD      0
#define FD_RDONLY  1
#define FD_RDWR    2

#define NO_ARGS       0
#define ONE_ARG       1
#define TWO_ARGS      2
#define MULTIPLE_ARGS 255

#define DO_BOOLEANS    1002
#define SET_BOOLEANS   1003
#define CLEAR_BOOLEANS 1004

#define ET_ONLINE    1
#define ET_WPROT     2

static void usage(int);
static int do_standard(int, cmdef_tr *, int, char **);
static int do_drvbuffer(int, cmdef_tr *, int, char **);
static int do_options(int, cmdef_tr *, int, char **);
static int do_tell(int, cmdef_tr *, int, char **);
static int do_partseek(int, cmdef_tr *, int, char **);
static int do_status(int, cmdef_tr *, int, char **);
static int print_densities(int, cmdef_tr *, int, char **);
static int do_asf(int, cmdef_tr *, int, char **);
static int do_show_options(int, cmdef_tr *, int, char **);
static void test_error(int, cmdef_tr *);

static cmdef_tr cmds[] = {
    { "weof",		MTWEOF,	  do_standard, 0, FD_RDWR,   ONE_ARG,
    ET_ONLINE | ET_WPROT },
    { "wset",		MTWSM,	  do_standard, 0, FD_RDWR,   ONE_ARG,
    ET_ONLINE | ET_WPROT },
    { "eof",		MTWEOF,	  do_standard, 0, FD_RDWR, ONE_ARG,
    ET_ONLINE },
    { "fsf",		MTFSF,	  do_standard, 0, FD_RDONLY, ONE_ARG,
    ET_ONLINE },
    { "fsfm",		MTFSFM,	  do_standard, 0, FD_RDONLY, ONE_ARG,
    ET_ONLINE },
    { "bsf",		MTBSF,	  do_standard, 0, FD_RDONLY, ONE_ARG,
    ET_ONLINE },
    { "bsfm",		MTBSFM,	  do_standard, 0, FD_RDONLY, ONE_ARG,
    ET_ONLINE },
    { "fsr",		MTFSR,	  do_standard, 0, FD_RDONLY, ONE_ARG,
    ET_ONLINE },
    { "bsr",		MTBSR,	  do_standard, 0, FD_RDONLY, ONE_ARG,
    ET_ONLINE },
    { "fss",		MTFSS,	  do_standard, 0, FD_RDONLY, ONE_ARG,
    ET_ONLINE },
    { "bss",		MTBSS,	  do_standard, 0, FD_RDONLY, ONE_ARG,
    ET_ONLINE },
    { "rewind",		MTREW,	  do_standard, 0, FD_RDONLY, NO_ARGS,
    ET_ONLINE },
    { "offline",	MTOFFL,	  do_standard, 0, FD_RDONLY, NO_ARGS,
    ET_ONLINE },
    { "rewoffl",	MTOFFL,	  do_standard, 0, FD_RDONLY, NO_ARGS,
    ET_ONLINE },
    { "eject",		MTOFFL,	  do_standard, 0, FD_RDONLY, NO_ARGS,
    ET_ONLINE },
    { "retension",	MTRETEN,  do_standard, 0, FD_RDONLY, NO_ARGS,
    ET_ONLINE },
    { "eod",		MTEOM,	  do_standard, 0, FD_RDONLY, NO_ARGS,
    ET_ONLINE },
    { "seod",		MTEOM,	  do_standard, 0, FD_RDONLY, NO_ARGS,
    ET_ONLINE },
    { "seek",		MTSEEK,   do_standard, 0, FD_RDONLY, ONE_ARG,
    ET_ONLINE },
    { "tell",		MTTELL,	  do_tell,     0, FD_RDONLY, NO_ARGS,
    ET_ONLINE },
    { "status",		MTNOP,	  do_status,   0, FD_RDONLY, NO_ARGS,
    0 },
    { "erase",		MTERASE,  do_standard, 0, FD_RDWR,   ONE_ARG,
    ET_ONLINE },
    { "setblk",		MTSETBLK, do_standard, 0, FD_RDONLY, ONE_ARG,
    0 },
    { "lock",		MTLOCK,   do_standard, 0, FD_RDONLY, NO_ARGS,
    ET_ONLINE },
    { "unlock", 	MTUNLOCK, do_standard, 0, FD_RDONLY, NO_ARGS,
    ET_ONLINE },
    { "load",		MTLOAD,	  do_standard, 0, FD_RDONLY, ONE_ARG,
    0 },
    { "compression",	MTCOMPRESSION,  do_standard,  0, FD_RDONLY, ONE_ARG,
    0 },
    { "setdensity",	MTSETDENSITY,   do_standard,  0, FD_RDONLY, ONE_ARG,
    0 },
    { "drvbuffer",	MTSETDRVBUFFER, do_drvbuffer, 0, FD_RDONLY, ONE_ARG,
    0 },
    { "stwrthreshold",	MTSETDRVBUFFER, do_drvbuffer, MT_ST_WRITE_THRESHOLD,
      FD_RDONLY, ONE_ARG, 0},
    { "stoptions",	DO_BOOLEANS,    do_options,   0, FD_RDONLY,
      MULTIPLE_ARGS, 0},
    { "stsetoptions",   SET_BOOLEANS,   do_options,   0, FD_RDONLY,
      MULTIPLE_ARGS, 0},
    { "stclearoptions", CLEAR_BOOLEANS, do_options,   0, FD_RDONLY,
      MULTIPLE_ARGS, 0},
    { "defblksize",	MTSETDRVBUFFER, do_drvbuffer, MT_ST_DEF_BLKSIZE,
      FD_RDONLY, ONE_ARG, 0},
    { "defdensity",	MTSETDRVBUFFER, do_drvbuffer, MT_ST_DEF_DENSITY,
      FD_RDONLY, ONE_ARG, 0},
    { "defdrvbuffer",	MTSETDRVBUFFER, do_drvbuffer, MT_ST_DEF_DRVBUFFER,
      FD_RDONLY, ONE_ARG, 0},
    { "defcompression", MTSETDRVBUFFER, do_drvbuffer, MT_ST_DEF_COMPRESSION,
      FD_RDONLY, ONE_ARG, 0},
    { "stsetcln",	MTSETDRVBUFFER, do_drvbuffer, MT_ST_SET_CLN,
      FD_RDONLY, ONE_ARG, 0},
    { "sttimeout",	MTSETDRVBUFFER, do_drvbuffer, MT_ST_SET_TIMEOUT,
      FD_RDONLY, ONE_ARG, 0},
    { "stlongtimeout",	MTSETDRVBUFFER, do_drvbuffer, MT_ST_SET_LONG_TIMEOUT,
      FD_RDONLY, ONE_ARG, 0},
    { "densities",	0, print_densities,     0, NO_FD,     NO_ARGS,
    0 },
    { "setpartition",	MTSETPART, do_standard, 0, FD_RDONLY, ONE_ARG,
    ET_ONLINE },
    { "mkpartition",	MTMKPART,  do_standard, 0, FD_RDWR,   ONE_ARG,
    ET_ONLINE },
    { "partseek",	0,         do_partseek, 0, FD_RDONLY, TWO_ARGS,
    ET_ONLINE },
    { "asf",		0,         do_asf, MTREW,  FD_RDONLY, ONE_ARG,
    ET_ONLINE },
    { "stshowopt",	0,         do_show_options, 0,  FD_RDONLY, ONE_ARG,
    0 },
    { NULL, 0, 0, 0 }
};


static struct densities {
    int code;
    char *name;
} density_tbl[] = {
    {0x00, "default"},
    {0x01, "NRZI (800 bpi)"},
    {0x02, "PE (1600 bpi)"},
    {0x03, "GCR (6250 bpi)"},
    {0x04, "QIC-11"},
    {0x05, "QIC-45/60 (GCR, 8000 bpi)"},
    {0x06, "PE (3200 bpi)"},
    {0x07, "IMFM (6400 bpi)"},
    {0x08, "GCR (8000 bpi)"},
    {0x09, "GCR (37871 bpi)"},
    {0x0a, "MFM (6667 bpi)"},
    {0x0b, "PE (1600 bpi)"},
    {0x0c, "GCR (12960 bpi)"},
    {0x0d, "GCR (25380 bpi)"},
    {0x0f, "QIC-120 (GCR 10000 bpi)"},
    {0x10, "QIC-150/250 (GCR 10000 bpi)"},
    {0x11, "QIC-320/525 (GCR 16000 bpi)"},
    {0x12, "QIC-1350 (RLL 51667 bpi)"},
    {0x13, "DDS (61000 bpi)"},
    {0x14, "EXB-8200 (RLL 43245 bpi)"},
    {0x15, "EXB-8500 or QIC-1000"},
    {0x16, "MFM 10000 bpi"},
    {0x17, "MFM 42500 bpi"},
    {0x18, "TZ86"},
    {0x19, "DLT 10GB"},
    {0x1a, "DLT 20GB"},
    {0x1b, "DLT 35GB"},
    {0x1c, "QIC-385M"},
    {0x1d, "QIC-410M"},
    {0x1e, "QIC-1000C"},
    {0x1f, "QIC-2100C"},
    {0x20, "QIC-6GB"},
    {0x21, "QIC-20GB"},
    {0x22, "QIC-2GB"},
    {0x23, "QIC-875"},
    {0x24, "DDS-2"},
    {0x25, "DDS-3"},
    {0x26, "DDS-4 or QIC-4GB"},
    {0x27, "Exabyte Mammoth"},
    {0x28, "Exabyte Mammoth-2"},
    {0x29, "QIC-3080MC"},
    {0x30, "AIT-1 or MLR3"},
    {0x31, "AIT-2"},
    {0x32, "AIT-3 or SLR7"},
    {0x33, "SLR6"},
    {0x34, "SLR100"},
    {0x40, "DLT1 40 GB, or Ultrium"},
    {0x41, "DLT 40GB, or Ultrium2"},
    {0x42, "LTO-2"},
    {0x44, "LTO-3"},
    {0x45, "QIC-3095-MC (TR-4)"},
    {0x46, "LTO-4"},
    {0x47, "DDS-5 or TR-5"},
    {0x51, "IBM 3592 J1A"},
    {0x52, "IBM 3592 E05"},
    {0x80, "DLT 15GB uncomp. or Ecrix"},
    {0x81, "DLT 15GB compressed"},
    {0x82, "DLT 20GB uncompressed"},
    {0x83, "DLT 20GB compressed"},
    {0x84, "DLT 35GB uncompressed"},
    {0x85, "DLT 35GB compressed"},
    {0x86, "DLT1 40 GB uncompressed"},
    {0x87, "DLT1 40 GB compressed"},
    {0x88, "DLT 40GB uncompressed"},
    {0x89, "DLT 40GB compressed"},
    {0x8c, "EXB-8505 compressed"},
    {0x90, "SDLT110 uncompr/EXB-8205 compr"},
    {0x91, "SDLT110 compressed"},
    {0x92, "SDLT160 uncompressed"},
    {0x93, "SDLT160 comprssed"}
};

#define NBR_DENSITIES (sizeof(density_tbl) / sizeof(struct densities))

static struct booleans {
    char *name;
    unsigned long bitmask;
    char *expl;
} boolean_tbl[] = {
    {"buffer-writes", MT_ST_BUFFER_WRITES, "buffered writes"},
    {"async-writes",  MT_ST_ASYNC_WRITES,  "asynchronous writes"},
    {"read-ahead",    MT_ST_READ_AHEAD,    "read-ahead for fixed block size"},
    {"debug",         MT_ST_DEBUGGING,     "debugging (if compiled into driver)"},
    {"two-fms",       MT_ST_TWO_FM,        "write two filemarks when file closed"},
    {"fast-eod",      MT_ST_FAST_MTEOM, "space directly to eod (and lose file number)"},
    {"auto-lock",     MT_ST_AUTO_LOCK,     "automatically lock/unlock drive door"},
    {"def-writes",    MT_ST_DEF_WRITES,    "the block size and density are for writes"},
    {"can-bsr",       MT_ST_CAN_BSR,       "drive can space backwards well"},
    {"no-blklimits",  MT_ST_NO_BLKLIMS,    "drive doesn't support read block limits"},
    {"can-partitions",MT_ST_CAN_PARTITIONS,"drive can handle partitioned tapes"},
    {"scsi2logical",  MT_ST_SCSI2LOGICAL,  "logical block addresses used with SCSI-2"},
    {"no-wait",       MT_ST_NOWAIT,        "immediate mode for rewind, etc."},
#ifdef MT_ST_SYSV
    {"sysv",	      MT_ST_SYSV,	   "enable the SystemV semantics"},
#endif
    {"sili",	      MT_ST_SILI,	   "enable SILI for variable block mode"},
    {"cleaning",      MT_ST_SET_CLN,	   "set the cleaning bit location and mask"},
    {NULL, 0}};

static char *tape_name;   /* The tape name for messages */


	int
main(int argc, char **argv)
{
    int mtfd, cmd_code, i, argn, len, oflags;
    char *cmdstr;
    cmdef_tr *comp, *comp2;

    for (argn=1; argn < argc; argn++)
	if (*argv[argn] == '-')
	    switch (*(argv[argn] + 1)) {
	    case 'f':
	    case 't':
		argn += 1;
		if (argn >= argc) {
		    usage(0);
		    exit(1);
		}
		tape_name = argv[argn];
		break;
	    case 'h':
		usage(1);
		exit(0);
		break;
	    case 'v':
		printf("mt-st v. %s\n", VERSION);
		exit(0);
		break;
	    case '-':
		if (*(argv[argn] + 1) == '-' &&
		    *(argv[argn] + 2) == 'v') {
		    printf("mt-st v. %s\n", VERSION);
		    exit(0);
		}
		/* Fall through */
	    default:
		usage(0);
		exit(1);
	}
	else
	    break;

    if (tape_name == NULL && (tape_name = getenv("TAPE")) == NULL)
	tape_name = DEFTAPE;
       
    if (argn >= argc ) {
	usage(0);
	exit(1);
    }
    cmdstr = argv[argn++];

    len = strlen(cmdstr);
    for (comp = cmds; comp->cmd_name != NULL; comp++)
	if (strncmp(cmdstr, comp->cmd_name, len) == 0)
	    break;
    if (comp->cmd_name == NULL) {
	fprintf(stderr, "mt: unknown command \"%s\"\n", cmdstr);
	usage(1);
	exit(1);
    }
    if (len != strlen(comp->cmd_name)) {
	for (comp2 = comp + 1; comp2->cmd_name != NULL; comp2++)
	    if (strncmp(cmdstr, comp2->cmd_name, len) == 0)
		break;
	if (comp2->cmd_name != NULL) {
	    fprintf(stderr, "mt: ambiguous command \"%s\"\n", cmdstr);
	    usage(1);
	    exit(1);
	}
    }
    if (comp->arg_cnt != MULTIPLE_ARGS && comp->arg_cnt < argc - argn) {
	fprintf(stderr, "mt: too many arguments for the command '%s'.\n",
		comp->cmd_name);
	exit(1);
    }
    cmd_code = comp->cmd_code;

    if (comp->cmd_fdtype != NO_FD) {
	oflags = comp->cmd_fdtype == FD_RDONLY ? O_RDONLY : O_RDWR;
	if ((comp->error_tests & ET_ONLINE) == 0)
	    oflags |= O_NONBLOCK;
	if ((mtfd = open(tape_name, oflags)) < 0) {
	    perror(tape_name);
	    exit(1);
	}
    }
    else
	mtfd = (-1);

    if (comp->cmd_function != NULL) {
	i = comp->cmd_function(mtfd, comp, argc - argn,
			       (argc - argn > 0 ? argv + argn : NULL));
	if (i) {
	    if (errno == ENOSYS)
		fprintf(stderr, "mt: Command not supported by this kernel.\n");
	    else if (comp->error_tests != 0)
		test_error(mtfd, comp);
	}
    }
    else {
	fprintf(stderr, "mt: Internal error: command without function.\n");
	i = 1;
    }

    if (mtfd >= 0)
	close(mtfd);
    return i;
}


	static void
usage(int explain)
{
    int ind;
    char line[100];

    fprintf(stderr, "usage: mt [-v] [--version] [-h] [ -f device ] command [ count ]\n");
    if (explain) {
	for (ind=0; cmds[ind].cmd_name != NULL; ) {
	    if (ind == 0)
		strcpy(line, "commands: ");
	    else
		strcpy(line, "          ");
	    for ( ; cmds[ind].cmd_name != NULL; ind++) {
		strcat(line, cmds[ind].cmd_name);
		if (cmds[ind+1].cmd_name != NULL)
		    strcat(line, ", ");
		else
		    strcat(line, ".");
		if (strlen(line) >= 70 || cmds[ind+1].cmd_name == NULL) {
		    fprintf(stderr, "%s\n", line);
		    ind++;
		    break;
		}
	    }
	}
    }
}



/* Do a command that simply feeds an argument to the MTIOCTOP ioctl */
	static int
do_standard(int mtfd, cmdef_tr *cmd, int argc, char **argv)
{
    struct mtop mt_com;
    char *endp;

    mt_com.mt_op = cmd->cmd_code;
    mt_com.mt_count = (argc > 0 ? strtol(*argv, &endp, 0) : 1);
    if (argc > 0 && endp != *argv) {
	if (*endp == 'k')
	    mt_com.mt_count *= 1024;
	else if (*endp == 'M')
	    mt_com.mt_count *= 1024 * 1024;
	else if (*endp == 'G')
	    mt_com.mt_count *= 1024 * 1024 * 1024;
    }
    mt_com.mt_count |= cmd->cmd_count_bits;
    if (mt_com.mt_count < 0) {
	fprintf(stderr, "mt: negative repeat count\n");
	return 1;
    }
    if (ioctl(mtfd, MTIOCTOP, (char *)&mt_com) < 0) {
	perror(tape_name);
	return 2;
    }
    return 0;
}


/* The the drive buffering and other things with this (highly overloaded)
   ioctl function. (See also do_options below.) */
	static int
do_drvbuffer(int mtfd, cmdef_tr *cmd, int argc, char **argv)
{
    struct mtop mt_com;

    mt_com.mt_op = MTSETDRVBUFFER;
    mt_com.mt_count = (argc > 0 ? strtol(*argv, NULL, 0) : 1);
    if ((cmd->cmd_count_bits & MT_ST_OPTIONS) == MT_ST_DEF_OPTIONS)
	mt_com.mt_count &= 0xfffff;
#ifdef MT_ST_TIMEOUTS
    else if ((cmd->cmd_count_bits & MT_ST_OPTIONS) == MT_ST_TIMEOUTS)
	mt_com.mt_count &= 0x7ffffff;
#endif
    else
	mt_com.mt_count &= 0xfffffff;
    mt_com.mt_count |= cmd->cmd_count_bits;
    if (ioctl(mtfd, MTIOCTOP, (char *)&mt_com) < 0) {
	perror(tape_name);
	return 2;
    }
    return 0;
}


/* Set the tape driver options */
	static int
do_options(int mtfd, cmdef_tr *cmd, int argc, char **argv)
{
    int i, an, len;
    struct mtop mt_com;

    mt_com.mt_op = MTSETDRVBUFFER;
    if (argc == 0)
	mt_com.mt_count = 0;
    else if (isdigit(**argv))
	mt_com.mt_count = strtol(*argv, NULL, 0) & ~MT_ST_OPTIONS;
    else
	for (an = 0, mt_com.mt_count = 0; an < argc; an++) {
	    len = strlen(argv[an]);
	    for (i=0; boolean_tbl[i].name != NULL; i++)
		if (!strncmp(boolean_tbl[i].name, argv[an], len)) {
		    mt_com.mt_count |= boolean_tbl[i].bitmask;
		    break;
		}
	    if (boolean_tbl[i].name == NULL) {
		fprintf(stderr, "Illegal property name '%s'.\n", argv[an]);
		fprintf(stderr, "The implemented property names are:\n");
		for (i=0; boolean_tbl[i].name != NULL; i++)
		    fprintf(stderr, "  %9s -> %s\n", boolean_tbl[i].name,
			    boolean_tbl[i].expl);
		return 1;
	    }
	    if (len != strlen(boolean_tbl[i].name))
		for (i++ ; boolean_tbl[i].name != NULL; i++)
		    if (!strncmp(boolean_tbl[i].name, argv[an], len)) {
			fprintf(stderr, "Property name '%s' ambiguous.\n",
				argv[an]);
			return 1;
		    }
	}

    switch (cmd->cmd_code) {
    case DO_BOOLEANS:
	mt_com.mt_count |= MT_ST_BOOLEANS;
	break;
    case SET_BOOLEANS:
	mt_com.mt_count |= MT_ST_SETBOOLEANS;
	break;
    case CLEAR_BOOLEANS:
	mt_com.mt_count |= MT_ST_CLEARBOOLEANS;
	break;
    }
    if (ioctl(mtfd, MTIOCTOP, (char *)&mt_com) < 0) {
	perror(tape_name);
	return 2;
    }
    return 0;
}


/* Tell where the tape is */
	static int
do_tell(int mtfd, cmdef_tr *cmd, int argc, char **argv)
{
    struct mtpos mt_pos;

    if (ioctl(mtfd, MTIOCPOS, (char *)&mt_pos) < 0) {
	perror(tape_name);
	return 2;
    }
    printf("At block %ld.\n", mt_pos.mt_blkno);
    return 0;
}


/* Position the tape to a specific location within a specified partition */
	static int
do_partseek(int mtfd, cmdef_tr *cmd, int argc, char **argv)
{
    struct mtop mt_com;

    mt_com.mt_op = MTSETPART;
    mt_com.mt_count = (argc > 0 ? strtol(*argv, NULL, 0) : 0);
    if (ioctl(mtfd, MTIOCTOP, (char *)&mt_com) < 0) {
	perror(tape_name);
	return 2;
    }
    mt_com.mt_op = MTSEEK;
    mt_com.mt_count = (argc > 1 ? strtol(argv[1], NULL, 0) : 0);
    if (ioctl(mtfd, MTIOCTOP, (char *)&mt_com) < 0) {
	perror(tape_name);
	return 2;
    }
    return 0;
}


/* Position to start of file n. This might be implemented more intelligently
   some day. */
	static int
do_asf(int mtfd, cmdef_tr *cmd, int argc, char **argv)
{
    struct mtop mt_com;

    mt_com.mt_op = MTREW;
    mt_com.mt_count = 1;
    if (ioctl(mtfd, MTIOCTOP, (char *)&mt_com) < 0) {
	perror(tape_name);
	return 2;
    }
    mt_com.mt_count = (argc > 0 ? strtol(*argv, NULL, 0) : 0);
    if (mt_com.mt_count > 0) {
	mt_com.mt_op = MTFSF;
	if (ioctl(mtfd, MTIOCTOP, (char *)&mt_com) < 0) {
	    perror(tape_name);
	    return 2;
	}
    }
    return 0;
}


/*** Decipher the status ***/

	static int
do_status(int mtfd, cmdef_tr *cmd, int argc, char **argv)
{
    struct mtget status;
    int dens, i;
    char *type, *density;

    if (ioctl(mtfd, MTIOCGET, (char *)&status) < 0) {
	perror(tape_name);
	return 2;
    }

    if (status.mt_type == MT_ISSCSI1)
	type = "SCSI 1";
    else if (status.mt_type == MT_ISSCSI2)
	type = "SCSI 2";
    else if (status.mt_type == MT_ISONSTREAM_SC)
	type = "OnStream SC-, DI-, DP-, or USB";
    else
	type = NULL;
    if (type == NULL) {
	if (status.mt_type & 0x800000)
	    printf ("qic-117 drive type = 0x%05lx\n", status.mt_type & 0x1ffff);
	else if (status.mt_type == 0)
	    printf("IDE-Tape (type code 0) ?\n");
	else
	    printf("Unknown tape drive type (type code %ld)\n", status.mt_type);
	printf("File number=%d, block number=%d.\n",
	       status.mt_fileno, status.mt_blkno);
	printf("mt_resid: %ld, mt_erreg: 0x%lx\n",
	       status.mt_resid, status.mt_erreg);
	printf("mt_dsreg: 0x%lx, mt_gstat: 0x%lx\n",
	       status.mt_dsreg, status.mt_gstat);
    }
    else {
	printf("%s tape drive:\n", type);
	if (status.mt_type == MT_ISSCSI2)
	    printf("File number=%d, block number=%d, partition=%ld.\n",
		   status.mt_fileno, status.mt_blkno, (status.mt_resid & 0xff));
	else
	    printf("File number=%d, block number=%d.\n",
		   status.mt_fileno, status.mt_blkno);
	if (status.mt_type == MT_ISSCSI1 ||
	    status.mt_type == MT_ISSCSI2 ||
	    status.mt_type == MT_ISONSTREAM_SC) {
	    dens = (status.mt_dsreg & MT_ST_DENSITY_MASK) >> MT_ST_DENSITY_SHIFT;
	    density = "no translation";
	    for (i=0; i < NBR_DENSITIES; i++)
		if (density_tbl[i].code == dens) {
		    density = density_tbl[i].name;
		    break;
		}
	    printf("Tape block size %ld bytes. Density code 0x%x (%s).\n",
		   ((status.mt_dsreg & MT_ST_BLKSIZE_MASK) >> MT_ST_BLKSIZE_SHIFT),
		   dens, density);

	    printf("Soft error count since last status=%ld\n",
		   (status.mt_erreg & MT_ST_SOFTERR_MASK) >> MT_ST_SOFTERR_SHIFT);
	}
    }

    printf("General status bits on (%lx):\n", status.mt_gstat);
    if (GMT_EOF(status.mt_gstat))
	printf(" EOF");
    if (GMT_BOT(status.mt_gstat))
	printf(" BOT");
    if (GMT_EOT(status.mt_gstat))
	printf(" EOT");
    if (GMT_SM(status.mt_gstat))
	printf(" SM");
    if (GMT_EOD(status.mt_gstat))
	printf(" EOD");
    if (GMT_WR_PROT(status.mt_gstat))
	printf(" WR_PROT");
    if (GMT_ONLINE(status.mt_gstat))
	printf(" ONLINE");
    if (GMT_D_6250(status.mt_gstat))
	printf(" D_6250");
    if (GMT_D_1600(status.mt_gstat))
	printf(" D_1600");
    if (GMT_D_800(status.mt_gstat))
	printf(" D_800");
    if (GMT_DR_OPEN(status.mt_gstat))
	printf(" DR_OPEN");	  
    if (GMT_IM_REP_EN(status.mt_gstat))
	printf(" IM_REP_EN");
    if (GMT_CLN(status.mt_gstat))
	printf(" CLN");
    printf("\n");
    return 0;
}


/* From linux/drivers/scsi/st.[ch] */
#define ST_NBR_MODE_BITS 2
#define ST_NBR_MODES (1 << ST_NBR_MODE_BITS)
#define ST_MODE_SHIFT (7 - ST_NBR_MODE_BITS)
#define ST_MODE_MASK ((ST_NBR_MODES - 1) << ST_MODE_SHIFT)
#define TAPE_NR(minor) ( (((minor) & ~255) >> (ST_NBR_MODE_BITS + 1)) | \
    ((minor) & ~(-1 << ST_MODE_SHIFT)) )
#define TAPE_MODE(minor) (((minor) & ST_MODE_MASK) >> ST_MODE_SHIFT)
static const char *st_formats[] = {
        "",  "r", "k", "s", "l", "t", "o", "u",
        "m", "v", "p", "x", "a", "y", "q", "z"}; 

/* Show the options if visible in sysfs */
static int do_show_options(int mtfd, cmdef_tr *cmd, int argc, char **argv)
{
    int i, fd, options, tapeminor, tapeno, tapemode;
    struct stat stat;
    struct utsname uts;
    char fname[100], buf[20];

    if (uname(&uts) < 0) {
	perror(tape_name);
	return 2;
    }
    sscanf(uts.release, "%d.%d.%d", &i, &tapeno, &tapemode);
    if (i < 2 || tapeno < 6 || tapemode < 26)
	printf("Your kernel (%d.%d.%d) may be too old for this command.\n",
	       i, tapeno, tapemode);

    if (fstat(mtfd, &stat) < 0) {
	perror(tape_name);
	return 1;
    }

    if (!(stat.st_mode & S_IFCHR)) {
	fprintf(stderr, "mt: not a character device.\n");
	return 1;
    }

    tapeminor = minor(stat.st_rdev);
    tapeno = TAPE_NR(tapeminor);
    tapemode = TAPE_MODE(tapeminor);
    tapemode <<= 4 - ST_NBR_MODE_BITS;  /* from st.c */
    sprintf(fname, "/sys/class/scsi_tape/st%d%s/options", tapeno,
	    st_formats[tapemode]);
    /* printf("Trying file '%s' (st_rdev 0x%lx).\n", fname, stat.st_rdev); */

    if ((fd = open(fname, O_RDONLY)) < 0 ||
	read(fd, buf, 20) < 0) {
	fprintf(stderr, "Can't read the sysfs file '%s'.\n", fname);
	return 2;
    }
    close(fd);

    options = strtol(buf, NULL, 0);

    printf("The options set:");
    for (i=0; boolean_tbl[i].name != NULL; i++)
	if (options & boolean_tbl[i].bitmask)
	    printf(" %s", boolean_tbl[i].name);
    printf("\n");

    return 0;
}


/* Print a list of possible density codes */
	static int
print_densities(int fd, cmdef_tr *cmd, int argc, char **argv)
{
    int i, offset;

    printf("Some SCSI tape density codes:\ncode   explanation                   code   explanation\n");
    offset = (NBR_DENSITIES + 1) / 2;
    for (i=0; i < offset; i++) {
	printf("0x%02x   %-28s", density_tbl[i].code, density_tbl[i].name);
	if (i + offset < NBR_DENSITIES)
	    printf("  0x%02x   %s\n", density_tbl[i + offset].code, density_tbl[i + offset].name);
	else
	    printf("\n");
    }
    return 0;
}


/* Try to find out why the command failed */
	static void
test_error(int mtfd, cmdef_tr *cmd)
{
    struct mtget status;

    if (ioctl(mtfd, MTIOCGET, (char *)&status) < 0)
	return;

    if (status.mt_type != MT_ISSCSI1 && status.mt_type != MT_ISSCSI2)
	return;

    if ((cmd->error_tests & ET_ONLINE) && !GMT_ONLINE(status.mt_gstat))
	fprintf(stderr, "mt: The device is offline (not powered on, no tape ?).\n");
    if ((cmd->error_tests & ET_WPROT) && !GMT_WR_PROT(status.mt_gstat))
	fprintf(stderr, "mt: The tape is write-protected.\n");
}

