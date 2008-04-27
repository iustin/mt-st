/* This program initializes Linux SCSI tape drives using the
   inquiry data from the devices and a text database.

   Copyright 1996-2008 by Kai Mäkisara (email Kai.Makisara@kolumbus.fi)
   Distribution of this program is allowed according to the
   GNU Public Licence.

   Last modified: Sun Apr 27 14:24:16 2008 by kai.makisara
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <linux/major.h>
#include <scsi/sg.h>

#include "mtio.h"

#ifndef FALSE
#define TRUE 1
#define FALSE 0
#endif
#define SKIP_WHITE(p) for ( ; *p == ' ' || *p == '\t'; p++)

#define VERSION "1.1"

typedef struct _modepar_tr {
    int defined;
    int blocksize;
    int density;
    int buffer_writes;
    int async_writes;
    int read_ahead;
    int two_fm;
    int compression;
    int auto_lock;
    int fast_eod;
    int can_bsr;
    int no_blklimits;
    int can_partitions;
    int scsi2logical;
    int sysv;
    int defs_for_writes;
} modepar_tr;

typedef struct _devdef_tr {
    int do_rewind;
    int drive_buffering;
    int timeout;
    int long_timeout;
    int cleaning;
    int nowait;
    int sili;
    modepar_tr modedefs[4];
} devdef_tr;


#define DEFMAX 2048
#define LINEMAX 256

#define MAX_TAPES 32
#define NBR_MODES 4


static int verbose = 0;

/* The device directories being searched */
typedef struct 
{
    char dir[PATH_MAX];
    int selective_scan;
} devdir;
static devdir devdirs[] = { {"/dev/scsi", 0}, {"/dev", 1}, {"", 0}};

#define DEVFS_PATH    "/dev/tapes"
#define DEVFS_TAPEFMT DEVFS_PATH "/tape%d"

/* The partial names of the tape devices being included in the
   search in selective scan */
static char *tape_name_bases[] = {
    "st", "nst", "rmt", "nrmt", "tape", NULL};

/* The list of standard definition files being searched */
static char *std_databases[] = {
    "/etc/stinit.def",
    NULL};

	static FILE *
open_database(char *base)
{
    int i;
    FILE *f;

    if (base != NULL) {
	if ((f = fopen(base, "r")) == NULL)
	    fprintf(stderr, "stinit: Can't find SCSI tape database '%s'.\n",
		    base);
	return f;
    }

    for (i=0; std_databases[i] != NULL; i++) {
	if (verbose > 1)
	    fprintf(stderr, "Trying to open database '%s'.\n", std_databases[i]);
	if ((f = fopen(std_databases[i], "r")) != NULL) {
	    if (verbose > 1)
		fprintf(stderr, "Open succeeded.\n");
	    return f;
	}
    }
    fprintf(stderr, "Can't find the tape characteristics database.\n");

    return NULL;
}


	static char *
find_string(char *s, char *target, char *buf, int buflen)
{
    int have_arg;
    char *cp, *cp2, c, *argp;

    if (buf != NULL && buflen > 0)
	*buf = '\0';

    for ( ; *s != '\0'; ) {
	SKIP_WHITE(s);
	if (isalpha(*s)) {
	    for (cp=s ; isalnum(*cp) || *cp == '-'; cp++)
		;
	    cp2 = cp;
	    SKIP_WHITE(cp);
	    if (*cp == '=') {
		cp++;
		SKIP_WHITE(cp);
		if (*cp == '"') {
		    cp++;
		    for (cp2=cp; *cp2 != '"' && *cp2 != '\0'; cp2++)
			;
		}
		else
		    for (cp2=cp+1; isalnum(*cp2) || *cp2 == '-'; cp2++)
			;
		if (cp2 == '\0')
		    return NULL;
		have_arg = TRUE;
		argp = cp;
	    }
	    else {
		have_arg = FALSE;
		argp = "1";
	    }

	    if (!strncmp(target, s, strlen(target))) {
		c = *cp2;
		*cp2 = '\0';
		if (buf == NULL)
		    buf = strdup(argp);
		else {
		    if (strlen(argp) < buflen)
			strcpy(buf, argp);
		    else {
			strncpy(buf, argp, buflen);
			buf[buflen - 1] = '\0';
		    }
		}
		if (have_arg && c == '"')
		    cp2++;
		else
		    *cp2 = c;
		if (*cp2 != '\0')
		    memmove(s, cp2, strlen(cp2) + 1);
		else
		    *s = '\0';
		return buf;
	    }
	    s = cp2;
	}
	else
	    for ( ; *s != '\0' && *s != ' ' && *s != '\t'; s++)
		;
    }
    return NULL;
}


	static int
num_arg(char *t)
{
    int nbr;
    char *tt;

    nbr = strtol(t, &tt, 0);
    if (t != tt) {
	if (*tt == 'k')
	    nbr *= 1024;
	else if (*tt == 'M')
	    nbr *= 1024 * 1024;
    }
    return nbr;
}


	static int
next_block(FILE *dbf, char *buf, int buflen, int limiter)
{
    int len;
    char *cp, *bp;
    static char lbuf[LINEMAX];

    if (limiter == 0) {  /* Restart */
	rewind(dbf);
	lbuf[0] = '\0';
	return TRUE;
    }

    for (len = 0 ; ; ) {
	bp = buf + len;
	if ((cp = strchr(lbuf, limiter)) != NULL) {
	    *cp = '\0';
	    strcpy(bp, lbuf);
	    cp++;
	    SKIP_WHITE(cp);
	    memmove(lbuf, cp, strlen(cp) + 1);
	    return TRUE;
	}
	if (len + strlen(lbuf) >= DEFMAX) {
	    fprintf(stderr, "Too long definition: '%s'\n", buf);
	    return FALSE;
	}
	cp = lbuf;
	SKIP_WHITE(cp);
	strcpy(bp, cp);
	strcat(bp, " ");
	len += strlen(cp) + 1;
	if (fgets(lbuf, LINEMAX, dbf) == NULL)
	    return FALSE;
	if ((cp = strchr(lbuf, '#')) != NULL)
	    *cp = '\0';
	else
	    lbuf[strlen(lbuf) - 1] = '\0';
    }
}


	static int
find_pars(FILE *dbf, char *company, char *product, char *rev, devdef_tr *defs,
	  int parse_only)
{
    int i, mode, modes_defined, errors;
    char line[LINEMAX], defstr[DEFMAX], comdef[DEFMAX];
    char tmpcomp[LINEMAX], tmpprod[LINEMAX], tmprev[LINEMAX], *cp, c, *t;
    char *nextdef, *curdef, *comptr;
    static int call_nbr = 0;

    call_nbr++;
    defs->drive_buffering = (-1);
    defs->timeout = (-1);
    defs->long_timeout = (-1);
    defs->cleaning = (-1);
    defs->nowait = (-1);
    defs->sili = (-1);
    for (i=0; i < NBR_MODES; i++) {
	defs->modedefs[i].defined = FALSE;
	defs->modedefs[i].blocksize = (-1);
	defs->modedefs[i].density = (-1);
	defs->modedefs[i].buffer_writes = (-1);
	defs->modedefs[i].async_writes = (-1);
	defs->modedefs[i].read_ahead = (-1);
	defs->modedefs[i].two_fm = (-1);
	defs->modedefs[i].compression = (-1);
	defs->modedefs[i].auto_lock = (-1);
	defs->modedefs[i].fast_eod = (-1);
	defs->modedefs[i].can_bsr = (-1);
	defs->modedefs[i].no_blklimits = (-1);
	defs->modedefs[i].can_partitions = (-1);
	defs->modedefs[i].scsi2logical = (-1);
	defs->modedefs[i].sysv = (-1);
	defs->modedefs[i].defs_for_writes = (-1);
    }
    next_block(dbf, NULL, 0, 0);

    /* Find matching inquiry block */
    for (errors=0 ; ; ) {

	if (!next_block(dbf, defstr, DEFMAX, '{'))
	    break;

	find_string(defstr, "manuf", tmpcomp, LINEMAX);
	find_string(defstr, "model", tmpprod, LINEMAX);
	find_string(defstr, "rev", tmprev, LINEMAX);

	if (!next_block(dbf, defstr, DEFMAX, '}')) {
	    fprintf(stderr,
		    "End of definition block not found for ('%s', '%s', '%s').\n",
		    tmpcomp, tmpprod, tmprev);
	    return FALSE;
	}

	if (!parse_only) {
	    if (tmpcomp[0] != '\0' &&
		strncmp(company, tmpcomp, strlen(tmpcomp)))
		continue;
	    if (tmpprod[0] != '\0' &&
		strncmp(product, tmpprod, strlen(tmpprod)))
		continue;
	    if (tmprev[0] != '\0' &&
		strncmp(rev, tmprev, strlen(tmprev)))
		continue;
	}
	else if (verbose > 0)
	    printf("\nParsing modes for ('%s', '%s', '%s').\n",
		   tmpcomp, tmpprod, tmprev);

	/* Block found, get the characteristics */
	for (nextdef=defstr; *nextdef != '\0' &&
		 (*nextdef != 'm' || strncmp(nextdef, "mode", 2)); nextdef++)
	    ;
	c = *nextdef;
	*nextdef = '\0';
	strcpy(comdef, defstr);
	*nextdef = c;
	comptr = comdef;
	SKIP_WHITE(comptr);

	for ( ; *nextdef != '\0'; ) {
	    curdef = nextdef;
	    SKIP_WHITE(curdef);
	    for (nextdef++ ; *nextdef != '\0' &&
		 (*nextdef != 'm' || strncmp(nextdef, "mode", 2)); nextdef++)
		;
	    c = *nextdef;
	    *nextdef = '\0';

	    mode = strtol(curdef + 4, &cp, 0) - 1;
	    if (mode < 0 || mode >= NBR_MODES) {
		fprintf(stderr,
			"Illegal mode for ('%s', '%s', '%s'):\n'%s'\n",
			tmpcomp, tmpprod, tmprev, curdef);
		*nextdef = c;
		errors++;
		continue;
	    }

	    strcpy(defstr, comptr);
	    strcat(defstr, cp);
	    *nextdef = c;

	    if (verbose > 1)
		fprintf(stderr, "Mode %d definition: %s\n", mode + 1, defstr);

	    if ((t = find_string(defstr, "disab", line, LINEMAX)) != NULL &&
		strtol(t, NULL, 0) != 0) {
		defs->modedefs[mode].defined = FALSE;
		continue;
	    }

	    if ((t = find_string(defstr, "drive-", line, LINEMAX)) != NULL)
		defs->drive_buffering = num_arg(t);
	    if ((t = find_string(defstr, "timeout", line, LINEMAX)) != NULL)
		defs->timeout = num_arg(t);
	    if ((t = find_string(defstr, "long-time", line, LINEMAX)) != NULL)
		defs->long_timeout = num_arg(t);
	    if ((t = find_string(defstr, "clean", line, LINEMAX)) != NULL)
		defs->cleaning = num_arg(t);
	    if ((t = find_string(defstr, "no-w", line, LINEMAX)) != NULL)
		defs->nowait = num_arg(t);
	    if ((t = find_string(defstr, "sili", line, LINEMAX)) != NULL)
		defs->sili = num_arg(t);

	    defs->modedefs[mode].defined = TRUE;
	    if ((t = find_string(defstr, "block", line, LINEMAX)) != NULL)
		defs->modedefs[mode].blocksize = num_arg(t);
	    if ((t = find_string(defstr, "dens", line, LINEMAX)) != NULL)
		defs->modedefs[mode].density = num_arg(t);
	    if ((t = find_string(defstr, "buff", line, LINEMAX)) != NULL)
		defs->modedefs[mode].buffer_writes = num_arg(t);
	    if ((t = find_string(defstr, "async", line, LINEMAX)) != NULL)
		defs->modedefs[mode].async_writes = num_arg(t);
	    if ((t = find_string(defstr, "read", line, LINEMAX)) != NULL)
		defs->modedefs[mode].read_ahead = num_arg(t);
	    if ((t = find_string(defstr, "two", line, LINEMAX)) != NULL)
		defs->modedefs[mode].two_fm = num_arg(t);
	    if ((t = find_string(defstr, "comp", line, LINEMAX)) != NULL)
		defs->modedefs[mode].compression = num_arg(t);
	    if ((t = find_string(defstr, "auto", line, LINEMAX)) != NULL)
		defs->modedefs[mode].auto_lock = num_arg(t);
	    if ((t = find_string(defstr, "fast", line, LINEMAX)) != NULL)
		defs->modedefs[mode].fast_eod = num_arg(t);
	    if ((t = find_string(defstr, "can-b", line, LINEMAX)) != NULL)
		defs->modedefs[mode].can_bsr = num_arg(t);
	    if ((t = find_string(defstr, "noblk", line, LINEMAX)) != NULL)
		defs->modedefs[mode].no_blklimits = num_arg(t);
	    if ((t = find_string(defstr, "can-p", line, LINEMAX)) != NULL)
		defs->modedefs[mode].can_partitions = num_arg(t);
	    if ((t = find_string(defstr, "scsi2", line, LINEMAX)) != NULL)
		defs->modedefs[mode].scsi2logical = num_arg(t);
	    if ((t = find_string(defstr, "sysv", line, LINEMAX)) != NULL)
		defs->modedefs[mode].sysv = num_arg(t);
	    if ((t = find_string(defstr, "defs-for-w", line, LINEMAX)) != NULL)
		defs->modedefs[mode].defs_for_writes = num_arg(t);

	    for (t=defstr; *t == ' ' || *t == '\t'; t++)
		;
	    if (*t != '\0' && call_nbr <= 1) {
		fprintf(stderr,
			"Warning: errors in definition for ('%s', '%s', '%s'):\n%s\n",
			tmpcomp, tmpprod, tmprev, defstr);
		errors++;
	    }
	}
    }

    if (parse_only) {
	if (verbose > 0)
	    printf("\n");
	printf("Definition parse completed. ");
	if (errors) {
	    printf("Errors found!\n");
	    return FALSE;
	}
	else {
	    printf("No errors found.\n");
	    return TRUE;
	}
    }
    else {
	for (i=modes_defined=0; i < NBR_MODES; i++)
	    if (defs->modedefs[i].defined)
		modes_defined++;
	if (modes_defined == 0) {
	    fprintf(stderr,
		    "Warning: No modes in definition for ('%s', '%s', '%s').\n",
		    tmpcomp, tmpprod, tmprev);
	    errors++;
	}
    }

    if (modes_defined)
	return TRUE;
    return FALSE;
}


static int sg_io_errcheck(struct sg_io_hdr *hdp)
{
    int status;

    status = hdp->status & 0x7e;
    if ((hdp->status & 0x7e) == 0 || hdp->host_status == 0 ||
	hdp->driver_status == 0)
	return 0;
    return EIO;
}


#define INQUIRY 0x12
#define INQUIRY_CMDLEN  6
#define SENSE_BUFF_LEN 32
#define DEF_TIMEOUT 60000

#ifndef SCSI_IOCTL_SEND_COMMAND
#define SCSI_IOCTL_SEND_COMMAND 1
#endif
#define IOCTL_HEADER_LENGTH 8

	static int
do_inquiry(char *tname, char *company, char *product, char *rev, int print_non_found)
{
    int fn;
    int result, *ip, i;
#define BUFLEN 256
    unsigned char buffer[BUFLEN], *cmd, *inqptr;
    struct sg_io_hdr io_hdr;
    unsigned char inqCmdBlk[INQUIRY_CMDLEN] = {INQUIRY, 0, 0, 0, 200, 0};
    unsigned char sense_b[SENSE_BUFF_LEN];

    if ((fn = open(tname, O_RDONLY | O_NONBLOCK)) < 0) {
	if (print_non_found || verbose > 0) {
	    if (errno == ENXIO)
		fprintf(stderr, "Device '%s' not found by kernel.\n", tname);
	    else
		fprintf(stderr, "Can't open tape device '%s' (errno %d).\n",
			tname, errno);
	}
	return FALSE;
    }

    /* Try SG_IO first, if it is not supported, use SCSI_IOCTL_SEND_COMMAND */
    memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = sizeof(inqCmdBlk);
    io_hdr.mx_sb_len = sizeof(sense_b);
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.dxfer_len = 200;
    io_hdr.dxferp = buffer;
    io_hdr.cmdp = inqCmdBlk;
    io_hdr.sbp = sense_b;
    io_hdr.timeout = DEF_TIMEOUT;
    inqptr = buffer;

    result = ioctl(fn, SG_IO, &io_hdr);
    if (!result)
	result = sg_io_errcheck(&io_hdr);
    if (result) {
	if (errno == ENOTTY || errno == EINVAL) {
	    memset(buffer, 0, BUFLEN);
	    ip = (int *)&(buffer[0]);
	    *ip = 0;
	    *(ip+1) = BUFLEN - 13;

	    cmd = &(buffer[8]);
	    cmd[0] = INQUIRY;
	    cmd[4] = 200;

	    result = ioctl(fn, SCSI_IOCTL_SEND_COMMAND, buffer);
	    inqptr = buffer + IOCTL_HEADER_LENGTH;
	}
	if (result) {
	    close(fn);
	    sprintf((char *)buffer,
		    "The SCSI INQUIRY for device '%s' failed (power off?)",
		    tname);
	    perror((char *)buffer);
	    return FALSE;
	}
    }

    memcpy(company, inqptr + 8, 8);
    for (i=8; i > 0 && company[i-1] == ' '; i--)
	;
    company[i] = '\0';
    memcpy(product, inqptr + 16, 16);
    for (i=16; i > 0 && product[i-1] == ' '; i--)
	;
    product[i] = '\0';
    memcpy(rev, inqptr + 32, 4);
    for (i=4; i > 0 && rev[i-1] == ' '; i--)
	;
    rev[i] = '\0';

    close(fn);
    return TRUE;
}


	static int
tapenum(char *name)
{
    int dev;
    struct dirent *dent;
    DIR *dirp;
    char tmpname[PATH_MAX];
    const char *dn;
    const devdir *dvd;
    struct stat statbuf;

    if (strchr(name, '/') != NULL) {  /* Complete name */
	if (stat(name, &statbuf) != 0) {
	    fprintf(stderr, "Can't stat '%s'.\n", name);
	    return (-1);
	}
	if (!S_ISCHR(statbuf.st_mode) ||
	    major(statbuf.st_rdev) != SCSI_TAPE_MAJOR)
	    return (-1);
	dev = minor(statbuf.st_rdev) & 31;
	return dev;
    }
    else { /* Search from the device directories */
	for (dvd=devdirs; dvd->dir[0] != 0; dvd++) {
	    dn = dvd->dir;
	    if ((dirp = opendir(dn)) == NULL)
	      continue;

	    for ( ; (dent = readdir(dirp)) != NULL; )
		if (!strcmp(dent->d_name, name)) {
		    strcpy(tmpname, dn);
		    strcat(tmpname, "/");
		    strcat(tmpname, dent->d_name);
		    if (stat(tmpname, &statbuf) != 0) {
			fprintf(stderr, "Can't stat '%s'.\n", tmpname);
			continue;
		    }
		    if (!S_ISCHR(statbuf.st_mode) ||
			major(statbuf.st_rdev) != SCSI_TAPE_MAJOR)
			continue;
		    dev = minor(statbuf.st_rdev) & 31;
		    closedir(dirp);
		    return dev;
		}
	    closedir(dirp);
	}
    }

    return (-1);
}


	static int
accept_tape_name(char *name)
{
    char **npp;

    for (npp=tape_name_bases; *npp; npp++)
	if (!strncmp(name, *npp, strlen(*npp)))
	    return TRUE;
    return FALSE;
}


	static int
find_devfiles(int tapeno, char **names)
{
    int dev, mode, found;
    int non_rew[NBR_MODES];
    struct dirent *dent;
    DIR *dirp;
    char tmpname[PATH_MAX];
    const char *dn;
    static const devdir *dvd;
    const devdir *dvp;
    int tmpdevdirsindex = 0;
    static devdir tmpdevdirs[MAX_TAPES + 1];
    struct stat statbuf;

    for (found=0; found < NBR_MODES; found++) {
	*names[found] = '\0';
	non_rew[found] = FALSE;
    }

    if (dvd == NULL && !stat(DEVFS_PATH, &statbuf)) {
	if (S_ISDIR(statbuf.st_mode) && (dirp=opendir(DEVFS_PATH)) != NULL) {
	    /* Assume devfs, one directory for each tape */
	    for ( ; (dent = readdir(dirp)) != NULL; ) {
		if (!strcmp (dent->d_name, ".")
		    || !strcmp (dent->d_name, ".."))
		    continue;
		snprintf(tmpdevdirs[tmpdevdirsindex].dir,
			 sizeof(tmpdevdirs[tmpdevdirsindex].dir),
			 "%s/%s", DEVFS_PATH, dent->d_name);
		tmpdevdirs[tmpdevdirsindex].selective_scan = FALSE;
		if (++tmpdevdirsindex == MAX_TAPES)
		    break;
	    }
	    tmpdevdirs[tmpdevdirsindex].dir[0] = 0;
	    closedir(dirp);
	    dvd = &tmpdevdirs[0];
	}
    }
    if (dvd == NULL)
	dvd = devdirs;

    for (found=0, dvp=dvd; found < NBR_MODES && dvp->dir[0] != 0; dvp++) {
	dn = dvp->dir;
	if ((dirp = opendir(dn)) == NULL)
	    continue;

	for ( ; (dent = readdir(dirp)) != NULL; ) {
	    if (!strcmp (dent->d_name, ".") || !strcmp (dent->d_name, ".."))
		continue;
	    /* Ignore non-tape devices to avoid loading all the modules */
	    if (dvp->selective_scan && !accept_tape_name(dent->d_name))
		continue;
	    strcpy(tmpname, dn);
	    strcat(tmpname, "/");
	    strcat(tmpname, dent->d_name);
	    if (stat(tmpname, &statbuf) != 0) {
		fprintf(stderr, "Can't stat '%s'.\n", tmpname);
		continue;
	    }
	    if (!S_ISCHR(statbuf.st_mode) || major(statbuf.st_rdev) !=
		SCSI_TAPE_MAJOR)
		continue;
	    dev = minor(statbuf.st_rdev);
	    if ((dev & 31) != tapeno)
		continue;
	    mode = (dev & 127) >> 5;
	    if (non_rew[mode])
		continue;
	    if (*names[mode] == '\0')
		found++;
	    strcpy(names[mode], tmpname);
	    non_rew[mode] = (dev & 128) != 0;
	}
	closedir(dirp);
    }

    return (found > 0);
}


	static int
set_defs(devdef_tr *defs, char **fnames)
{
    int i, tape;
    int clear_set[2];
    struct mtop op;

    for (i=0; i < NBR_MODES; i++) {
	if (*fnames[i] == '\0' || !defs->modedefs[i].defined)
	    continue;

	if ((tape = open(fnames[i], O_RDONLY | O_NONBLOCK)) < 0) {
	    fprintf(stderr, "Can't open the tape device '%s' for mode %d.\n",
		    fnames[i], i);
	    return FALSE;
	}

	if (i == 0) {
	    if (defs->do_rewind) {
		op.mt_op = MTREW;
		op.mt_count = 1;
		ioctl(tape, MTIOCTOP, &op);  /* Don't worry about result */
	    }

	    if (defs->drive_buffering >= 0) {
		op.mt_op = MTSETDRVBUFFER;
		op.mt_count = MT_ST_DEF_DRVBUFFER | defs->drive_buffering;
		if (ioctl(tape, MTIOCTOP, &op) != 0) {
		    fprintf(stderr, "Can't set drive buffering to %d.\n",
			    defs->drive_buffering);
		}
	    }

	    if (defs->timeout >= 0) {
		op.mt_op = MTSETDRVBUFFER;
		op.mt_count = MT_ST_SET_TIMEOUT | defs->timeout;
		if (ioctl(tape, MTIOCTOP, &op) != 0) {
		    fprintf(stderr, "Can't set device timeout %d s.\n",
			    defs->timeout);
		}
	    }

	    if (defs->long_timeout >= 0) {
		op.mt_op = MTSETDRVBUFFER;
		op.mt_count = MT_ST_SET_LONG_TIMEOUT | defs->long_timeout;
		if (ioctl(tape, MTIOCTOP, &op) != 0) {
		    fprintf(stderr, "Can't set device long timeout %d s.\n",
			    defs->long_timeout);
		}
	    }

	    if (defs->cleaning >= 0) {
		op.mt_op = MTSETDRVBUFFER;
		op.mt_count = MT_ST_SET_CLN | defs->cleaning;
		if (ioctl(tape, MTIOCTOP, &op) != 0) {
		    fprintf(stderr, "Can't set cleaning request parameter to %x\n",
			    defs->cleaning);
		}
	    }
	}

	op.mt_op = MTSETDRVBUFFER;

	clear_set[0] = clear_set[1] = 0;
	if (defs->nowait >= 0)
	    clear_set[defs->nowait != 0] |= MT_ST_NOWAIT;
	if (defs->sili >= 0)
	    clear_set[defs->sili != 0] |= MT_ST_SILI;
	if (defs->modedefs[i].buffer_writes >= 0)
	    clear_set[defs->modedefs[i].buffer_writes != 0] |= MT_ST_BUFFER_WRITES;
	if (defs->modedefs[i].async_writes >= 0)
	    clear_set[defs->modedefs[i].async_writes != 0] |= MT_ST_ASYNC_WRITES;
	if (defs->modedefs[i].read_ahead >= 0)
	    clear_set[defs->modedefs[i].read_ahead != 0] |= MT_ST_READ_AHEAD;
	if (defs->modedefs[i].two_fm >= 0)
	    clear_set[defs->modedefs[i].two_fm != 0] |= MT_ST_TWO_FM;
	if (defs->modedefs[i].fast_eod >= 0)
	    clear_set[defs->modedefs[i].fast_eod != 0] |= MT_ST_FAST_MTEOM;
	if (defs->modedefs[i].auto_lock >= 0)
	    clear_set[defs->modedefs[i].auto_lock != 0] |= MT_ST_AUTO_LOCK;
	if (defs->modedefs[i].can_bsr >= 0)
	    clear_set[defs->modedefs[i].can_bsr != 0] |= MT_ST_CAN_BSR;
	if (defs->modedefs[i].no_blklimits >= 0)
	    clear_set[defs->modedefs[i].no_blklimits != 0] |= MT_ST_NO_BLKLIMS;
	if (defs->modedefs[i].can_partitions >= 0)
	    clear_set[defs->modedefs[i].can_partitions != 0] |= MT_ST_CAN_PARTITIONS;
	if (defs->modedefs[i].scsi2logical >= 0)
	    clear_set[defs->modedefs[i].scsi2logical != 0] |= MT_ST_SCSI2LOGICAL;
	if (defs->modedefs[i].sysv >= 0)
	    clear_set[defs->modedefs[i].sysv != 0] |= MT_ST_SYSV;
	if (defs->modedefs[i].defs_for_writes >= 0)
	    clear_set[defs->modedefs[i].defs_for_writes != 0] |= MT_ST_DEF_WRITES;

	if (clear_set[0] != 0) {
	    op.mt_count = MT_ST_CLEARBOOLEANS | clear_set[0];
	    if (ioctl(tape, MTIOCTOP, &op) != 0) {
		fprintf(stderr, "Can't clear the tape options (bits 0x%x, mode %d).\n",
			clear_set[0], i);
	    }
	}
	if (clear_set[1] != 0) {
	    op.mt_count = MT_ST_SETBOOLEANS | clear_set[1];
	    if (ioctl(tape, MTIOCTOP, &op) != 0) {
		fprintf(stderr, "Can't set the tape options (bits 0x%x, mode %d).\n",
			clear_set[1], i);
	    }
	}

	if (defs->modedefs[i].blocksize >= 0) {
	    op.mt_count = MT_ST_DEF_BLKSIZE | defs->modedefs[i].blocksize;
	    if (ioctl(tape, MTIOCTOP, &op) != 0) {
		fprintf(stderr, "Can't set blocksize %d for mode %d.\n",
			defs->modedefs[i].blocksize, i);
	    }
	}
	if (defs->modedefs[i].density >= 0) {
	    op.mt_count = MT_ST_DEF_DENSITY | defs->modedefs[i].density;
	    if (ioctl(tape, MTIOCTOP, &op) != 0) {
		fprintf(stderr, "Can't set density %x for mode %d.\n",
			defs->modedefs[i].density, i);
	    }
	}
	if (defs->modedefs[i].compression >= 0) {
	    op.mt_count = MT_ST_DEF_COMPRESSION | defs->modedefs[i].compression;
	    if (ioctl(tape, MTIOCTOP, &op) != 0) {
		fprintf(stderr, "Can't set compression %d for mode %d.\n",
			defs->modedefs[i].compression, i);
	    }
	}

	close(tape);
    }
    return TRUE;
}


	static int
define_tape(int tapeno, FILE *dbf, devdef_tr *defptr, int print_non_found)
{
    int i;
    char company[10], product[20], rev[5], *tname, *fnames[NBR_MODES];

    if (verbose > 0)
	printf("\nstinit, processing tape %d\n", tapeno);

    if ((fnames[0] = calloc(NBR_MODES, PATH_MAX)) == NULL) {
	fprintf(stderr, "Can't allocate name buffers.\n");
	return FALSE;
    }
    for (i=1; i < NBR_MODES; i++)
	fnames[i] = fnames[i-1] + PATH_MAX;

    if (!find_devfiles(tapeno, fnames) ||
	*fnames[0] == '\0') {
	if (print_non_found)
	    fprintf(stderr, "Can't find any device files for tape %d.\n",
		    tapeno);
	free(fnames[0]);
	return FALSE;
    }
    if (verbose > 1)
	for (i=0; i < NBR_MODES; i++)
	    printf("Mode %d, name '%s'\n", i + 1, fnames[i]);

    tname = fnames[0];
    if (!do_inquiry(tname, company, product, rev, print_non_found)) {
	free(fnames[0]);
	return FALSE;
    }
    if (verbose > 0)
	printf("The manufacturer is '%s', product is '%s', and revision '%s'.\n",
	       company, product, rev);

    if (!find_pars(dbf, company, product, rev, defptr, FALSE)) {
	fprintf(stderr, "Can't find defaults for tape number %d.\n", tapeno);
	free(fnames[0]);
	return FALSE;
    }

    if (!set_defs(defptr, fnames)) {
	free(fnames[0]);
	return FALSE;
    }

    free(fnames[0]);
    return TRUE;
}


	static char
usage(int retval)
{
    fprintf(stderr,
	    "Usage: stinit [-h] [-v] [-f dbname] [-p] [drivename_or_number ...]\n");
    exit(retval);
}


	int
main(int argc, char **argv)
{
    FILE *dbf = NULL;
    int argn;
    int tapeno, parse_only = FALSE;
    char *dbname = NULL;
    devdef_tr defs;

    defs.do_rewind = FALSE;
    for (argn=1; argn < argc && *argv[argn] == '-'; argn++) {
	if (*(argv[argn] + 1) == 'v')
	    verbose++;
	else if (*(argv[argn] + 1) == 'p')
	    parse_only = TRUE;
	else if (*(argv[argn] + 1) == 'h')
	    usage(0);
	else if (*(argv[argn] + 1) == 'r')
	    defs.do_rewind = TRUE;
	else if (*(argv[argn] + 1) == 'f') {
	    argn += 1;
	    if (argn >= argc)
		usage(1);
	    dbname = argv[argn];
	}
	else if (*(argv[argn] + 1) == '-' &&
		 *(argv[argn] + 2) == 'v') {
	    printf("stinit v. %s\n", VERSION);
	    exit(0);
	    break;
	}
	else
	    usage(1);
    }

    if ((dbf = open_database(dbname)) == NULL)
	return 1;  

    if (parse_only) {
	if (argc > argn)
	    fprintf(stderr, "Extra arguments on command line ignored.\n");
	if (!find_pars(dbf, "xyz", "xyz", "xyz", &defs, TRUE))
	    return 1;
	return 0;
    }

    if (argc > argn) {  /* Initialize specific drives */
	for ( ; argn < argc; argn++) {
	    if (*argv[argn] == '-') {
		usage(1);
		return 1; /* Never executed but makes gcc happy */
	    }
	    else if (isdigit(*argv[argn]))
		tapeno = strtol(argv[argn], NULL, 0);
	    else if ((tapeno = tapenum(argv[argn])) < 0) {
		fprintf(stderr, "Can't find tape number for name '%s'.\n",
			argv[argn]);
		continue;
	    }
	    if (!define_tape(tapeno, dbf, &defs, TRUE))
		fprintf(stderr, "Definition for '%s' failed.\n", argv[argn]);
	}
    }
    else {  /* Initialize all SCSI tapes */
	for (tapeno=0; tapeno < MAX_TAPES; tapeno++)
	    if (!define_tape(tapeno, dbf, &defs, FALSE)) {
		fprintf(stderr, "Initialized %d tape device%s.\n",
			tapeno, (tapeno != 1 ? "s" : ""));
		return 0;  /* Process tapes until failure */
	    }
    }

    return 0;
}
