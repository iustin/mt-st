/* This program initializes Linux SCSI tape drives using the
   inquiry data from the devices and a text database.

   Maintained by Iustin Pop (iustin@k1024.org).
   Copyright 1996-2008 by Kai Mäkisara (Kai.Makisara@kolumbus.fi).
   Distribution of this program is allowed according to the
   GNU Public Licence.

*/

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/major.h>
#include <scsi/sg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include "mtio.h"
#include "version.h"

#ifndef FALSE
#define TRUE 1
#define FALSE 0
#endif
#define SKIP_WHITE(p) for (; *p == ' ' || *p == '\t'; p++)

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
    int weof_nowait;
    int sili;
    modepar_tr modedefs[4];
} devdef_tr;


#define DEFMAX 2048
#define LINEMAX 256

#define MAX_TAPES 32
#define NBR_MODES 4


static int verbose = 0;

/* The device directories being searched */
typedef struct {
    char dir[PATH_MAX];
    int selective_scan;
} devdir;
static devdir devdirs[] = { { "/dev/scsi", 0 }, { "/dev", 1 }, { "", 0 } };

#define DEVFS_PATH "/dev/tapes"
#define DEVFS_TAPEFMT DEVFS_PATH "/tape%d"

/* The partial names of the tape devices being included in the
   search in selective scan */
static char *tape_name_bases[] = { "st", "nst", "rmt", "nrmt", "tape", NULL };

/* The list of standard definition files being searched */
static char *std_databases[] = { "/etc/stinit.def", NULL };

static char usage(int retval) __attribute__((noreturn));

static FILE *open_database(char *base)
{
    int i;
    FILE *f;

    if (base != NULL) {
        if ((f = fopen(base, "r")) == NULL)
            fprintf(stderr, "stinit: Can't find SCSI tape database '%s'.\n", base);
        return f;
    }

    for (i = 0; std_databases[i] != NULL; i++) {
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


/* Find the parameter named target in the definition string s, return its
   value and REMOVE the parameter from s.

   The string s is a sequence of whitespace separated parameters of the form
   "key", "key=value" or "key = value". A key starts with a letter and
   continues with letters, digits and '-'. A value is either a quoted string
   ("..."), or a run of letters, digits and '-' (its first character may be
   anything, e.g. a sign). A parameter without a value has the value "1".

   Matching is by prefix: the key in s must start with target, so the target
   "manuf" matches "manufacturer" (and also e.g. "manufxyz"). Only the first
   matching parameter is used and removed; a later duplicate stays in s.

   The removal is what the error checking in find_pars() relies on: after all
   known parameters have been looked up, any text left in s is unknown or
   malformed. Note that only the parameter itself is removed, not the
   whitespace around it.

   On success, the value (with the quotes stripped) is copied into buf
   (truncated to buflen - 1 characters, or strdup()-ed if buf is NULL) and buf
   is returned. If target is not found, or the end of s is reached in a
   malformed parameter ("key=" or an unterminated quote), NULL is returned and
   buf is set to the empty string. */
static char *find_string(char *s, char *target, char *buf, int buflen)
{
    int have_arg;
    char *cp, *cp2, c, *argp;

    if (buf != NULL && buflen > 0)
        *buf = '\0';

    for (; *s != '\0';) {
        SKIP_WHITE(s);
        if (isalpha(*s)) {
            for (cp = s; isalnum(*cp) || *cp == '-'; cp++)
                ;
            cp2 = cp;
            SKIP_WHITE(cp);
            if (*cp == '=') {
                cp++;
                SKIP_WHITE(cp);
                if (*cp == '"') {
                    cp++;
                    for (cp2 = cp; *cp2 != '"' && *cp2 != '\0'; cp2++)
                        ;
                    if (*cp2 == '\0') /* Unterminated quoted value */
                        return NULL;
                } else {
                    /* Check if we actually have a non-empty argument. Note
                       that this only catches "key=" at the end of the
                       string: in "key= next", the value is "next". */
                    if (*cp == '\0')
                        return NULL;
                    else
                        for (cp2 = cp + 1; isalnum(*cp2) || *cp2 == '-'; cp2++)
                            ;
                    /* Unlike a quoted one, an unquoted value can end the
                       string (e.g. when placed directly before the closing
                       brace), so there is no end of string check here. */
                }
                have_arg = TRUE;
                argp = cp;
            } else {
                have_arg = FALSE;
                argp = "1";
            }

            if (!strncmp(target, s, strlen(target))) {
                c = *cp2;
                *cp2 = '\0';
                if (buf == NULL)
                    buf = strdup(argp);
                else {
                    if (strlen(argp) < (unsigned int)buflen)
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
        } else
            for (; *s != '\0' && *s != ' ' && *s != '\t'; s++)
                ;
    }
    return NULL;
}


/* Convert a parameter value to a number. Decimal, hex (0x) and octal (0)
   are accepted, with an optional 'k' (* 1024) or 'M' (* 1024 * 1024) suffix.
   Anything after the number is ignored, and there is no overflow check. */
static int num_arg(char *t)
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


/* Return values of next_block() */
#define NB_OK 1
#define NB_EOF 0
#define NB_ERROR (-1)

/* Read from the database into buf all the text up to the next limiter
   character. find_pars() calls this alternately with '{' (to get the
   identification part of a definition block) and '}' (to get its body).

   The function keeps state between calls: lbuf holds the not yet used rest
   of the current line. When the limiter is found in the middle of a line,
   the text before it is returned, the limiter itself is dropped, and the
   text after it (minus leading whitespace) is kept in lbuf for the next
   call. Calling with limiter == 0 (and buf == NULL) rewinds the file and
   clears this state.

   The text is transformed as follows:
   - '#' and the rest of the line are removed (comments);
   - lines not containing the limiter are appended with their leading
     whitespace removed and followed by one space, which replaces the
     newline;
   - the part before the limiter is appended as is, including any leading
     whitespace;
   - whitespace inside or at the end of lines is kept unchanged.
   For example, "a=1\n  b=2  \n  c=3 }" read with '}' gives
   " a=1 b=2     c=3 " (the first space comes from the initially empty
   lbuf; the five spaces between "b=2" and "c=3" are the two trailing ones,
   the newline, and the two leading ones of the last line). The parsing
   done on the result ignores the extra whitespace.

   Returns NB_OK on success, NB_EOF if the end of the file was reached
   before the limiter (the text read so far is left in buf), and NB_ERROR
   (after printing a message) if the text doesn't fit into buf or a line
   doesn't fit into lbuf. */
static int next_block(FILE *dbf, char *buf, size_t buflen, int limiter)
{
    size_t len, n;
    char *cp, *bp;
    static char lbuf[LINEMAX];

    if (limiter == 0) { /* Restart */
        rewind(dbf);
        lbuf[0] = '\0';
        return NB_OK;
    }

    buf[0] = '\0';
    for (len = 0;;) {
        bp = buf + len;
        if ((cp = strchr(lbuf, limiter)) != NULL) {
            *cp = '\0';
            /* Need room for the text and the terminating NUL. */
            if (len + strlen(lbuf) + 1 > buflen) {
                fprintf(stderr, "Too long definition: '%s'\n", buf);
                return NB_ERROR;
            }
            strcpy(bp, lbuf);
            cp++;
            SKIP_WHITE(cp);
            memmove(lbuf, cp, strlen(cp) + 1);
            return NB_OK;
        }
        cp = lbuf;
        SKIP_WHITE(cp);
        /* Need room for the text, the trailing space and the NUL. */
        if (len + strlen(cp) + 2 > buflen) {
            fprintf(stderr, "Too long definition: '%s'\n", buf);
            return NB_ERROR;
        }
        strcpy(bp, cp);
        strcat(bp, " ");
        len += strlen(cp) + 1;
        if (fgets(lbuf, LINEMAX, dbf) == NULL)
            return NB_EOF;
        n = strlen(lbuf);
        if (n > 0 && lbuf[n - 1] == '\n')
            lbuf[n - 1] = '\0';
        else if (!feof(dbf)) {
            fprintf(stderr, "Too long line in database (maximum is %d characters): '%s'\n",
                    LINEMAX - 2, lbuf);
            return NB_ERROR;
        }
        if ((cp = strchr(lbuf, '#')) != NULL)
            *cp = '\0';
    }
}


/* Find the parameters for the device identified by company, product and
   rev (as returned by INQUIRY) in the database, and store them in defs.

   The database is a sequence of definition blocks:

       manufacturer=X model=Y revision=Z {
       common parameters
       mode1 parameters for mode 1
       mode2 parameters for mode 2 ...
       }

   For each block:
   1. The identification part (before '{') is searched for the "manuf",
      "model" and "rev" keywords; any other text there is silently ignored.
   2. In normal mode, the block is skipped unless each given keyword is a
      prefix of the corresponding INQUIRY string; a missing keyword matches
      anything. In parse mode (-p), all blocks are processed.
   3. The body is split at the "mode" keywords. The text before the first
      one is the common part, and each mode section runs up to the next
      "mode" keyword. Known issues: only "mo" is compared, without a word
      boundary, so e.g. "more=1" starts a new (illegal) mode section; and a
      block without any mode keyword sets nothing at all, even though the
      manual page used to say it could be used for common definitions.
   4. For each mode section, the common part followed by the section text is
      put into modebuf, and every known parameter is looked up with
      find_string(), which removes it from modebuf. If a parameter is given
      twice, the first occurrence (i.e. the one in the common part) is used,
      and the second one is left over (and reported as an error).
   5. Any text left in modebuf is reported as an error.

   The values are stored directly into defs, with -1 meaning "not given"
   (set_defs() skips those). Since all matching blocks are processed in
   order, a later block overrides the values set by an earlier one, for the
   modes it defines. The device wide values (timeout, drive-buffering, ...)
   are also parsed per mode section, so the last mode section setting them
   wins.

   In parse mode, returns TRUE if no errors were found. In normal mode,
   returns TRUE if at least one mode was defined; parsing errors are only
   reported, they don't make the function fail. */
static int
find_pars(FILE *dbf, char *company, char *product, char *rev, devdef_tr *defs, int parse_only)
{
    int i, mode, modes_defined, errors;
    char line[LINEMAX], defstr[DEFMAX], comdef[DEFMAX], modebuf[DEFMAX];
    char tmpcomp[LINEMAX], tmpprod[LINEMAX], tmprev[LINEMAX], *cp, c, *t;
    char *nextdef, *curdef, *comptr;
    /* find_pars() is called once per tape when initializing several tapes;
       call_nbr is used to report the leftover text errors only once. */
    static int call_nbr = 0;

    call_nbr++;
    defs->drive_buffering = (-1);
    defs->timeout = (-1);
    defs->long_timeout = (-1);
    defs->cleaning = (-1);
    defs->nowait = (-1);
    defs->weof_nowait = (-1);
    defs->sili = (-1);
    for (i = 0; i < NBR_MODES; i++) {
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
    next_block(dbf, NULL, 0, 0); /* Rewind the database */

    /* These are printed in the messages below, even when the database
       has no blocks at all */
    tmpcomp[0] = tmpprod[0] = tmprev[0] = '\0';

    /* Find matching inquiry block */
    for (errors = 0;;) {
        int nb;

        nb = next_block(dbf, defstr, DEFMAX, '{');
        if (nb == NB_ERROR)
            return FALSE;
        if (nb == NB_EOF)
            break;

        find_string(defstr, "manuf", tmpcomp, LINEMAX);
        find_string(defstr, "model", tmpprod, LINEMAX);
        find_string(defstr, "rev", tmprev, LINEMAX);

        nb = next_block(dbf, defstr, DEFMAX, '}');
        if (nb == NB_ERROR)
            return FALSE;
        if (nb == NB_EOF) {
            fprintf(stderr,
                    "End of definition block not found for ('%s', "
                    "'%s', '%s').\n",
                    tmpcomp, tmpprod, tmprev);
            return FALSE;
        }

        /* Skip the block if it doesn't match the device (steps 1-2) */
        if (!parse_only) {
            if (tmpcomp[0] != '\0' && strncmp(company, tmpcomp, strlen(tmpcomp)))
                continue;
            if (tmpprod[0] != '\0' && strncmp(product, tmpprod, strlen(tmpprod)))
                continue;
            if (tmprev[0] != '\0' && strncmp(rev, tmprev, strlen(tmprev)))
                continue;
        } else if (verbose > 0)
            printf("\nParsing modes for ('%s', '%s', '%s').\n", tmpcomp, tmpprod, tmprev);

        /* Block found, get the characteristics. First copy the text
           before the first mode keyword into comdef (step 3). */
        for (nextdef = defstr;
             *nextdef != '\0' && (*nextdef != 'm' || strncmp(nextdef, "mode", 2)); nextdef++)
            ;
        c = *nextdef;
        *nextdef = '\0';
        strcpy(comdef, defstr);
        *nextdef = c;
        comptr = comdef;
        SKIP_WHITE(comptr);

        /* Process each mode section; nextdef points to its "mode" keyword.
           The section is temporarily terminated by writing a NUL at the
           start of the next one (the saved character c is put back). */
        for (; *nextdef != '\0';) {
            int tmpsize;
            curdef = nextdef;
            SKIP_WHITE(curdef);
            for (nextdef++;
                 *nextdef != '\0' && (*nextdef != 'm' || strncmp(nextdef, "mode", 2));
                 nextdef++)
                ;
            c = *nextdef;
            *nextdef = '\0';

            /* Skip the "mode" keyword. The section can be shorter than
               that, since the split above only checks for "mo" (e.g. a
               stray "mo" at the end of the block), so don't skip past
               its end. */
            mode = strtol(curdef + strnlen(curdef, 4), &cp, 0) - 1;
            if (mode < 0 || mode >= NBR_MODES) {
                fprintf(stderr, "Illegal mode for ('%s', '%s', '%s'):\n'%s'\n",
                        tmpcomp, tmpprod, tmprev, curdef);
                *nextdef = c;
                errors++;
                continue;
            }

            /* modebuf = common part + text after "modeN" (step 4) */
            if ((tmpsize = snprintf(modebuf, DEFMAX, "%s%s", comptr, cp)) >= DEFMAX) {
                fprintf(stderr, "Definition too large for ('%s', '%s', '%s'): size %d, maximum is %d\n",
                        tmpcomp, tmpprod, tmprev, tmpsize, DEFMAX);
                *nextdef = c;
                errors++;
                continue;
            }

            *nextdef = c;

            if (verbose > 1)
                fprintf(stderr, "Mode %d definition: %s\n", mode + 1, modebuf);

            /* "disabled" undefines the mode, and the rest of the section is
               ignored, without checking for errors */
            if ((t = find_string(modebuf, "disab", line, LINEMAX)) != NULL &&
                strtol(t, NULL, 0) != 0) {
                defs->modedefs[mode].defined = FALSE;
                continue;
            }

            /* Device wide parameters */
            if ((t = find_string(modebuf, "drive-", line, LINEMAX)) != NULL)
                defs->drive_buffering = num_arg(t);
            if ((t = find_string(modebuf, "timeout", line, LINEMAX)) != NULL)
                defs->timeout = num_arg(t);
            if ((t = find_string(modebuf, "long-time", line, LINEMAX)) != NULL)
                defs->long_timeout = num_arg(t);
            if ((t = find_string(modebuf, "clean", line, LINEMAX)) != NULL)
                defs->cleaning = num_arg(t);
            if ((t = find_string(modebuf, "no-w", line, LINEMAX)) != NULL)
                defs->nowait = num_arg(t);
            if ((t = find_string(modebuf, "weof-n", line, LINEMAX)) != NULL)
                defs->weof_nowait = num_arg(t);
            if ((t = find_string(modebuf, "sili", line, LINEMAX)) != NULL)
                defs->sili = num_arg(t);

            /* Mode parameters */
            defs->modedefs[mode].defined = TRUE;
            if ((t = find_string(modebuf, "block", line, LINEMAX)) != NULL)
                defs->modedefs[mode].blocksize = num_arg(t);
            if ((t = find_string(modebuf, "dens", line, LINEMAX)) != NULL)
                defs->modedefs[mode].density = num_arg(t);
            if ((t = find_string(modebuf, "buff", line, LINEMAX)) != NULL)
                defs->modedefs[mode].buffer_writes = num_arg(t);
            if ((t = find_string(modebuf, "async", line, LINEMAX)) != NULL)
                defs->modedefs[mode].async_writes = num_arg(t);
            if ((t = find_string(modebuf, "read", line, LINEMAX)) != NULL)
                defs->modedefs[mode].read_ahead = num_arg(t);
            if ((t = find_string(modebuf, "two", line, LINEMAX)) != NULL)
                defs->modedefs[mode].two_fm = num_arg(t);
            if ((t = find_string(modebuf, "comp", line, LINEMAX)) != NULL)
                defs->modedefs[mode].compression = num_arg(t);
            if ((t = find_string(modebuf, "auto", line, LINEMAX)) != NULL)
                defs->modedefs[mode].auto_lock = num_arg(t);
            if ((t = find_string(modebuf, "fast", line, LINEMAX)) != NULL)
                defs->modedefs[mode].fast_eod = num_arg(t);
            if ((t = find_string(modebuf, "can-b", line, LINEMAX)) != NULL)
                defs->modedefs[mode].can_bsr = num_arg(t);
            if ((t = find_string(modebuf, "noblk", line, LINEMAX)) != NULL)
                defs->modedefs[mode].no_blklimits = num_arg(t);
            if ((t = find_string(modebuf, "can-p", line, LINEMAX)) != NULL)
                defs->modedefs[mode].can_partitions = num_arg(t);
            if ((t = find_string(modebuf, "scsi2", line, LINEMAX)) != NULL)
                defs->modedefs[mode].scsi2logical = num_arg(t);
            if ((t = find_string(modebuf, "sysv", line, LINEMAX)) != NULL)
                defs->modedefs[mode].sysv = num_arg(t);
            if ((t = find_string(modebuf, "defs-for-w", line, LINEMAX)) != NULL)
                defs->modedefs[mode].defs_for_writes = num_arg(t);

            /* All known parameters have been removed from modebuf, so
               anything left is unknown or malformed (step 5) */
            for (t = modebuf; *t == ' ' || *t == '\t'; t++)
                ;
            if (*t != '\0' && call_nbr <= 1) {
                fprintf(stderr,
                        "Warning: errors in definition for ('%s', "
                        "'%s', '%s'):\n%s\n",
                        tmpcomp, tmpprod, tmprev, modebuf);
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
        } else {
            printf("No errors found.\n");
            return TRUE;
        }
    } else {
        for (i = modes_defined = 0; i < NBR_MODES; i++)
            if (defs->modedefs[i].defined)
                modes_defined++;
        if (modes_defined == 0) {
            fprintf(stderr, "Warning: No modes in definition for ('%s', '%s', '%s').\n",
                    tmpcomp, tmpprod, tmprev);
            errors++;
        }
    }

    if (modes_defined)
        return TRUE;
    return FALSE;
}


/* Get the sense key, additional sense code (ASC) and qualifier (ASCQ) from
   the sense data in sb (len bytes long), in either fixed or descriptor
   format; returns FALSE if there is no (usable) sense data. */
static int get_sense(const unsigned char *sb, int len, int *key, int *asc, int *ascq)
{
    if (len < 1)
        return FALSE;
    switch (sb[0] & 0x7f) {
    case 0x70: /* Fixed format, current and deferred errors */
    case 0x71:
        if (len < 14)
            return FALSE;
        *key = sb[2] & 0x0f;
        *asc = sb[12];
        *ascq = sb[13];
        return TRUE;
    case 0x72: /* Descriptor format, current and deferred errors */
    case 0x73:
        if (len < 4)
            return FALSE;
        *key = sb[1] & 0x0f;
        *asc = sb[2];
        *ascq = sb[3];
        return TRUE;
    default:
        return FALSE;
    }
}

/* Print the sense key, ASC and ASCQ from the sense data in sb (len bytes
   long), if there is any, as the end of an error message. */
static void print_sense(const unsigned char *sb, int len)
{
    static const char *const key_names[16] = {
        "NO SENSE",        "RECOVERED ERROR", "NOT READY",
        "MEDIUM ERROR",    "HARDWARE ERROR",  "ILLEGAL REQUEST",
        "UNIT ATTENTION",  "DATA PROTECT",    "BLANK CHECK",
        "VENDOR SPECIFIC", "COPY ABORTED",    "ABORTED COMMAND",
        "RESERVED",        "VOLUME OVERFLOW", "MISCOMPARE",
        "COMPLETED"
    };
    int key, asc, ascq;

    if (get_sense(sb, len, &key, &asc, &ascq))
        fprintf(stderr, ", sense key 0x%x (%s), ASC 0x%02x, ASCQ 0x%02x", key,
                key_names[key], asc, ascq);
}


#define CHECK_CONDITION 0x02
#define RECOVERED_ERROR 0x01
#define SG_DRIVER_SENSE 0x08

/* Check the outcome of an SG_IO command: returns 0 if it was successful,
   and EIO otherwise. A RECOVERED ERROR counts as a success: the command
   completed, and the device only reports that it had to recover (e.g. by
   retrying). The DRIVER_SENSE flag in driver_status only says that there
   is sense data, so it is ignored here. */
static int sg_io_errcheck(struct sg_io_hdr *hdp)
{
    int key, asc, ascq;

    if (hdp->host_status != 0 || (hdp->driver_status & ~SG_DRIVER_SENSE) != 0)
        return EIO;
    if ((hdp->status & 0x7e) == 0)
        return 0;
    if ((hdp->status & 0x7e) == CHECK_CONDITION &&
        get_sense(hdp->sbp, hdp->sb_len_wr, &key, &asc, &ascq) && key == RECOVERED_ERROR)
        return 0;
    return EIO;
}


#define INQUIRY 0x12
#define INQUIRY_CMDLEN 6
#define SENSE_BUFF_LEN 32
#define DEF_TIMEOUT 60000

#ifndef SCSI_IOCTL_SEND_COMMAND
#define SCSI_IOCTL_SEND_COMMAND 1
#endif
#define IOCTL_HEADER_LENGTH 8

static int do_inquiry(char *tname, char *company, char *product, char *rev, int print_non_found)
{
    int fn;
    int result, *ip, i;
#define BUFLEN 256
    unsigned char buffer[BUFLEN], *cmd, *inqptr;
    struct sg_io_hdr io_hdr;
    unsigned char inqCmdBlk[INQUIRY_CMDLEN] = { INQUIRY, 0, 0, 0, 200, 0 };
    unsigned char sense_b[SENSE_BUFF_LEN];

    if ((fn = open(tname, O_RDONLY | O_NONBLOCK)) < 0) {
        if (print_non_found || verbose > 0) {
            if (errno == ENXIO)
                fprintf(stderr, "Device '%s' not found by kernel.\n", tname);
            else
                fprintf(stderr, "Can't open tape device '%s' (errno %d).\n", tname, errno);
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
    /* Clear the buffer, so that the identification is empty rather than
       random if the drive returns less data than expected */
    memset(buffer, 0, BUFLEN);

    /* Two kinds of failures are handled below: the ioctl itself can fail
       (it returns -1 and sets errno), or the device can complete the
       command with an error status (which doesn't set errno). Only an
       unsupported SG_IO ioctl makes us try SCSI_IOCTL_SEND_COMMAND. */
    result = ioctl(fn, SG_IO, &io_hdr);
    if (result < 0 && (errno == ENOTTY || errno == EINVAL)) {
        memset(buffer, 0, BUFLEN);
        ip = (int *)&(buffer[0]);
        *ip = 0;
        *(ip + 1) = BUFLEN - 13;

        cmd = &(buffer[8]);
        cmd[0] = INQUIRY;
        cmd[4] = 200;

        result = ioctl(fn, SCSI_IOCTL_SEND_COMMAND, buffer);
        inqptr = buffer + IOCTL_HEADER_LENGTH;
        /* A positive result is the SCSI status of the failed command, and
           the sense data (if any) replaces the command at inqptr. */
        if (result > 0) {
            close(fn);
            fprintf(stderr, "The SCSI INQUIRY for device '%s' failed: SCSI status 0x%02x",
                    tname, result);
            print_sense(inqptr, BUFLEN - IOCTL_HEADER_LENGTH);
            fprintf(stderr, ".\n");
            return FALSE;
        }
    } else if (result == 0 && sg_io_errcheck(&io_hdr)) {
        close(fn);
        fprintf(stderr,
                "The SCSI INQUIRY for device '%s' failed: SCSI status 0x%02x, "
                "host status 0x%02x, driver status 0x%02x",
                tname, io_hdr.status, io_hdr.host_status, io_hdr.driver_status);
        print_sense(sense_b, io_hdr.sb_len_wr);
        fprintf(stderr, ".\n");
        return FALSE;
    }
    if (result < 0) {
        int err = errno;

        close(fn);
        snprintf((char *)buffer, BUFLEN,
                 "The SCSI INQUIRY for device '%s' failed (power off?)", tname);
        errno = err;
        perror((char *)buffer);
        return FALSE;
    }

    memcpy(company, inqptr + 8, 8);
    for (i = 8; i > 0 && company[i - 1] == ' '; i--)
        ;
    company[i] = '\0';
    memcpy(product, inqptr + 16, 16);
    for (i = 16; i > 0 && product[i - 1] == ' '; i--)
        ;
    product[i] = '\0';
    memcpy(rev, inqptr + 32, 4);
    for (i = 4; i > 0 && rev[i - 1] == ' '; i--)
        ;
    rev[i] = '\0';

    close(fn);
    return TRUE;
}


static int tapenum(char *name)
{
    int dev;
    struct dirent *dent;
    DIR *dirp;
    char tmpname[PATH_MAX];
    const char *dn;
    const devdir *dvd;
    struct stat statbuf;

    if (strchr(name, '/') != NULL) { /* Complete name */
        if (stat(name, &statbuf) != 0) {
            fprintf(stderr, "Can't stat '%s'.\n", name);
            return (-1);
        }
        if (!S_ISCHR(statbuf.st_mode) || major(statbuf.st_rdev) != SCSI_TAPE_MAJOR)
            return (-1);
        dev = minor(statbuf.st_rdev) & 31;
        return dev;
    } else { /* Search from the device directories */
        for (dvd = devdirs; dvd->dir[0] != 0; dvd++) {
            dn = dvd->dir;
            if ((dirp = opendir(dn)) == NULL)
                continue;

            for (; (dent = readdir(dirp)) != NULL;)
                if (!strcmp(dent->d_name, name)) {
                    strcpy(tmpname, dn);
                    strcat(tmpname, "/");
                    strcat(tmpname, dent->d_name);
                    if (stat(tmpname, &statbuf) != 0) {
                        fprintf(stderr, "Can't stat '%s'.\n", tmpname);
                        continue;
                    }
                    if (!S_ISCHR(statbuf.st_mode) || major(statbuf.st_rdev) != SCSI_TAPE_MAJOR)
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


static int accept_tape_name(char *name)
{
    char **npp;

    for (npp = tape_name_bases; *npp; npp++)
        if (!strncmp(name, *npp, strlen(*npp)))
            return TRUE;
    return FALSE;
}


static int find_devfiles(int tapeno, char **names)
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

    for (found = 0; found < NBR_MODES; found++) {
        *names[found] = '\0';
        non_rew[found] = FALSE;
    }

    if (dvd == NULL && !stat(DEVFS_PATH, &statbuf)) {
        if (S_ISDIR(statbuf.st_mode) && (dirp = opendir(DEVFS_PATH)) != NULL) {
            /* Assume devfs, one directory for each tape */
            for (; (dent = readdir(dirp)) != NULL;) {
                if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
                    continue;
                snprintf(tmpdevdirs[tmpdevdirsindex].dir,
                         sizeof(tmpdevdirs[tmpdevdirsindex].dir), "%s/%s",
                         DEVFS_PATH, dent->d_name);
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

    for (found = 0, dvp = dvd; found < NBR_MODES && dvp->dir[0] != 0; dvp++) {
        dn = dvp->dir;
        if ((dirp = opendir(dn)) == NULL)
            continue;

        for (; (dent = readdir(dirp)) != NULL;) {
            if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
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
            if (!S_ISCHR(statbuf.st_mode) || major(statbuf.st_rdev) != SCSI_TAPE_MAJOR)
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


static int set_defs(devdef_tr *defs, char **fnames)
{
    int i, tape, fails;
    int clear_set[2];
    struct mtop op;

    for (i = fails = 0; i < NBR_MODES; i++) {
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
                if (ioctl(tape, MTIOCTOP, &op) != 0) {
                    fails++;
                    fprintf(stderr, "Rewind of %s fails.\n", fnames[i]);
                }
            }

            if (defs->drive_buffering >= 0) {
                op.mt_op = MTSETDRVBUFFER;
                op.mt_count = MT_ST_DEF_DRVBUFFER | defs->drive_buffering;
                if (ioctl(tape, MTIOCTOP, &op) != 0) {
                    fails++;
                    fprintf(stderr, "Can't set drive buffering to %d.\n", defs->drive_buffering);
                }
            }

            if (defs->timeout >= 0) {
                op.mt_op = MTSETDRVBUFFER;
                op.mt_count = MT_ST_SET_TIMEOUT | defs->timeout;
                if (ioctl(tape, MTIOCTOP, &op) != 0) {
                    fails++;
                    fprintf(stderr, "Can't set device timeout %d s.\n", defs->timeout);
                }
            }

            if (defs->long_timeout >= 0) {
                op.mt_op = MTSETDRVBUFFER;
                op.mt_count = MT_ST_SET_LONG_TIMEOUT | defs->long_timeout;
                if (ioctl(tape, MTIOCTOP, &op) != 0) {
                    fails++;
                    fprintf(stderr, "Can't set device long timeout %d s.\n",
                            defs->long_timeout);
                }
            }

            if (defs->cleaning >= 0) {
                op.mt_op = MTSETDRVBUFFER;
                op.mt_count = MT_ST_SET_CLN | defs->cleaning;
                if (ioctl(tape, MTIOCTOP, &op) != 0) {
                    fails++;
                    fprintf(stderr, "Can't set cleaning request parameter to %x\n",
                            defs->cleaning);
                }
            }
        }

        op.mt_op = MTSETDRVBUFFER;

        clear_set[0] = clear_set[1] = 0;
        if (defs->nowait >= 0)
            clear_set[defs->nowait != 0] |= MT_ST_NOWAIT;
        if (defs->weof_nowait >= 0)
            clear_set[defs->weof_nowait != 0] |= MT_ST_NOWAIT_EOF;
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
                fails++;
                fprintf(stderr, "Can't clear the tape options (bits 0x%x, mode %d).\n",
                        clear_set[0], i);
            }
        }
        if (clear_set[1] != 0) {
            op.mt_count = MT_ST_SETBOOLEANS | clear_set[1];
            if (ioctl(tape, MTIOCTOP, &op) != 0) {
                fails++;
                fprintf(stderr, "Can't set the tape options (bits 0x%x, mode %d).\n",
                        clear_set[1], i);
            }
        }

        if (defs->modedefs[i].blocksize >= 0) {
            op.mt_count = MT_ST_DEF_BLKSIZE | defs->modedefs[i].blocksize;
            if (ioctl(tape, MTIOCTOP, &op) != 0) {
                fails++;
                fprintf(stderr, "Can't set blocksize %d for mode %d.\n",
                        defs->modedefs[i].blocksize, i);
            }
        }
        if (defs->modedefs[i].density >= 0) {
            op.mt_count = MT_ST_DEF_DENSITY | defs->modedefs[i].density;
            if (ioctl(tape, MTIOCTOP, &op) != 0) {
                fails++;
                fprintf(stderr, "Can't set density %x for mode %d.\n",
                        defs->modedefs[i].density, i);
            }
        }
        if (defs->modedefs[i].compression >= 0) {
            op.mt_count = MT_ST_DEF_COMPRESSION | defs->modedefs[i].compression;
            if (ioctl(tape, MTIOCTOP, &op) != 0) {
                fails++;
                fprintf(stderr, "Can't set compression %d for mode %d.\n",
                        defs->modedefs[i].compression, i);
            }
        }

        close(tape);
    }
    return (fails == 0);
}


static int define_tape(int tapeno, FILE *dbf, devdef_tr *defptr, int print_non_found)
{
    int i;
    char company[10], product[20], rev[5], *tname, *fnames[NBR_MODES];

    if (verbose > 0)
        printf("\nstinit, processing tape %d\n", tapeno);

    if ((fnames[0] = calloc(NBR_MODES, PATH_MAX)) == NULL) {
        fprintf(stderr, "Can't allocate name buffers.\n");
        return FALSE;
    }
    for (i = 1; i < NBR_MODES; i++)
        fnames[i] = fnames[i - 1] + PATH_MAX;

    if (!find_devfiles(tapeno, fnames) || *fnames[0] == '\0') {
        if (print_non_found)
            fprintf(stderr, "Can't find any device files for tape %d.\n", tapeno);
        free(fnames[0]);
        return FALSE;
    }
    if (verbose > 1)
        for (i = 0; i < NBR_MODES; i++)
            printf("Mode %d, name '%s'\n", i + 1, fnames[i]);

    tname = fnames[0];
    if (!do_inquiry(tname, company, product, rev, print_non_found)) {
        free(fnames[0]);
        return FALSE;
    }
    if (verbose > 0)
        printf("The manufacturer is '%s', product is '%s', and revision "
               "'%s'.\n",
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


static char usage(int retval)
{
    fprintf(stderr, "Usage: stinit [-h] [-v] [--version] [-f dbname] [-p] [-r] "
                    "[drivename_or_number ...]\n");
    exit(retval);
}


int main(int argc, char **argv)
{
    FILE *dbf = NULL;
    int argn, retval = 0;
    int tapeno, parse_only = FALSE;
    char *dbname = NULL;
    char *convp;
    devdef_tr defs;

    defs.do_rewind = FALSE;
    for (argn = 1; argn < argc && *argv[argn] == '-'; argn++) {
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
        } else if (strcmp(argv[argn], "--version") == 0) {
            printf("stinit v. %s\n", VERSION);
            exit(0);
            break;
        } else
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

    if (argc > argn) { /* Initialize specific drives */
        for (; argn < argc; argn++) {
            if (*argv[argn] == '-') {
                usage(1);
            } else if (isdigit(*argv[argn])) {
                tapeno = strtol(argv[argn], &convp, 0);
                if (*argv[argn] != '\0' && *convp != '\0') {
                    fprintf(stderr,
                            "Invalid tape device index '%s': don't "
                            "know how to parse '%s'\n",
                            argv[argn], convp);
                    continue;
                }
            } else if ((tapeno = tapenum(argv[argn])) < 0) {
                fprintf(stderr, "Can't find tape number for name '%s'.\n", argv[argn]);
                continue;
            }
            if (!define_tape(tapeno, dbf, &defs, TRUE)) {
                fprintf(stderr, "Definition for '%s' failed.\n", argv[argn]);
                retval = 1;
            }
        }
    } else { /* Initialize all SCSI tapes */
        for (tapeno = 0; tapeno < MAX_TAPES; tapeno++)
            if (!define_tape(tapeno, dbf, &defs, FALSE)) {
                fprintf(stderr, "Initialized %d tape device%s.\n", tapeno,
                        (tapeno != 1 ? "s" : ""));
                return 0; /* Process tapes until failure */
            }
    }

    return retval;
}
