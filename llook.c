/*
 * llook version 1.00
 * based on sgrep version 1.00
 *
 * Copyright 2009 Stephen C. Losen.
 * Copyright 2025 Aaron Clovsky.
 *
 * Distributed under the terms of the GNU General Public License (GPL)
 *
 * llook (limited look) is similar to look, but with additional features,
 * including numeric search, reverse search, limiting maximum results, and more.
 *
 *
 * llook uses a binary search algorithm, which is very fast, but
 * requires sorted input.  Each iteration of the search eliminates
 * half of the remaining input.  In other words, doubling the size
 * of the file adds just one iteration.
 *
 * llook seeks to the center of the file and then reads characters
 * until it encounters a newline, which places the file pointer at
 * the start of the next line.  llook compares the search key with the
 * beginning of the line.  If the key is greater than the line, then
 * the process repeats with the second half of the file.  If less than,
 * then the process repeats with the first half of the file.  If equal,
 * then the line matches, but it may not be the earliest match, so the
 * process repeats with the first half of the file.  Eventually all
 * of the input is eliminated and llook finds either no matching line
 * or the first matching line.  llook outputs matching lines until it
 * encounters a non matching line.
 *
 * Usage: llook [-i | -n] [-c] [-b] [-r] [-m num] key [ sorted_file ... ]
 *
 * If no input file is specified, then llook uses stdin.
 *
 * The -i flag uses case insensitive byte comparison.  The file must
 * be sorted with "sort -f".
 *
 * The -n flag uses numeric comparison.  The file must be sorted
 * with "sort -n".
 *
 * The -b flag causes llook to ignore white space at the beginning
 * of lines and at the beginning of the search key.  The file must
 * be sorted with "sort -b"
 *
 * The -c flag outputs the number of matching lines instead of the
 * lines themselves.
 *
 * The -r flag specifies that the file is sorted in reverse
 * (descending) order using "sort -r".
 *
 * The -m flag causes llook to limit the number of lines that it outputs to at
 * most num lines, this flag has no effect when used with the -c flag.
 *
 * Author:  Stephen C. Losen   University of Virginia
 * Additional Author:  Aaron Clovsky
 */

/* large file support */

#ifdef _AIX
#define _LARGE_FILES
#else
#define _FILE_OFFSET_BITS 64
#endif

#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* We need different comparison functions for different sort orderings */

/* exact comparison */

static int
cmp_exact(const char *key, FILE *fp) {
    const unsigned char *k = (unsigned char *) key;
    int c;

    while (*k != 0 && (c = getc(fp)) == *k) {
        k++;
    }
    return
        *k == 0   ?  0 :
        c == '\n' ?  1 :
        c == EOF  ? -1 : *k - c;
}

/* case insensitive comparison */

static int
cmp_case(const char *key, FILE *fp) {
    const unsigned char *k = (unsigned char *) key;
    int c;

    while (*k != 0 && tolower(c = getc(fp)) == tolower(*k)) {
        k++;
    }
    return
        *k == 0   ?  0 :
        c == '\n' ?  1 :
        c == EOF  ? -1 :
        tolower(*k) - tolower(c);
}

/* exact comparison ignoring leading white space */

static int
cmp_exact_white(const char *key, FILE *fp) {
    const unsigned char *k = (unsigned char *) key;
    int c;

    while (isspace(*k)) {
        k++;
    }
    while ((c = getc(fp)) != '\n' && isspace(c))
        ;
    while (*k != 0 && c == *k) {
        k++;
        c = getc(fp);
    }
    return
        *k == 0   ?  0 :
        c == '\n' ?  1 :
        c == EOF  ? -1 : *k - c;
}

/* case insensitive comparison ignoring leading white space */

static int
cmp_case_white(const char *key, FILE *fp) {
    const unsigned char *k = (unsigned char *) key;
    int c;

    while (isspace(*k)) {
        k++;
    }
    while ((c = getc(fp)) != '\n' && isspace(c))
        ;
    while (*k != 0 && tolower(c) == tolower(*k)) {
        k++;
        c = getc(fp);
    }
    return
        *k == 0   ?  0 :
        c == '\n' ?  1 :
        c == EOF  ? -1 :
        tolower(*k) - tolower(c);
}

/* numeric comparison */

static int
cmp_num(const char *key, FILE *fp) {
    int c, i = 0;
    char buf[128], *cp = 0;
    double low, high, x;

    /* read numeric string into buf */

    while((c = getc(fp)) != '\n' && c != EOF) {
        if (i == 0 && isspace(c)) {
            continue;
        }
        if (i + 1 >= sizeof(buf) ||
            (c != '-' && c != '.' && !isdigit(c)))
        {
            break;
        }
        buf[i++] = c;
    }
    buf[i] = 0;
    if (c == EOF && i == 0) {
        return -1;
    }

    /* convert to double and use numeric comparison */

    x = strtod(buf, 0);
    low = high = strtod(key, &cp);
    if (*cp == ':') {
        high = strtod(cp + 1, 0);
    }
    return
        high < x ? -1 :
        low  > x ?  1 : 0;
}

static int (*compare)(const char *key, FILE *fp);

/*
 * Use binary search to find the first matching line and return
 * its byte position.
 */

static off_t
binsrch(const char *key, FILE *fp, int reverse) {
    off_t low, med, high, start, prev = -1, ret = -1;
    int cmp, c;
    struct stat st;

    fstat(fileno(fp), &st);
    high = st.st_size - 1;
    low = 0;
    while (low <= high) {
        med = (high + low) / 2;
        fseeko(fp, med, SEEK_SET);

        /* scan to start of next line if not at beginning of file */

        if ((start = med) != 0)  {
            do {
                start++;
            } while ((c = getc(fp)) != '\n' && c != EOF);
        }

        /* compare key with current line */

        if (start != prev) {        /* avoid unnecessary compares */
            cmp = compare(key, fp);
            if (reverse != 0) {
                cmp = -cmp;
            }
            prev = start;
        }

        /* eliminate half of input */

        if (cmp < 0) {
            high = med - 1;
        }
        else if (cmp > 0) {
            low = start + 1;
        }
        else {             /* success, look for earlier match */
            ret = start;
            high = med - 1;
        }
    }
    return ret;
}

/* print all lines that match the key or else just the number of matches */

static void
printmatch(const char *key, FILE *fp, off_t start,
    const char *fname, int cflag, int mflag)
{
    int c, count;

    if (start >= 0) {
        fseeko(fp, start, SEEK_SET);
    }
    for (count = 0; start >= 0 && compare(key, fp) == 0; count++) {
        fseeko(fp, start, SEEK_SET);
        if (cflag == 0 && fname != 0) {
            fputs(fname, stdout);
            fputc(':', stdout);
        }
        if (cflag == 0 && mflag > 0 && mflag == count) {
            break;
        }
        while ((c = getc(fp)) != EOF) {
            start++;
            if (cflag == 0) {
                fputc(c, stdout);
            }
            if (c == '\n') {
                break;
            }
        }
        if (c == EOF) {
            break;
        }
    }
    if (cflag != 0) {
        if (fname != 0) {
            fputs(fname, stdout);
            fputc(':', stdout);
        }
        printf("%d\n", count);
    }
}

int
main(int argc, char **argv) {
    FILE *fp;
    const char *key = 0;
    int i, numfile, status;
    int bflag = 0, cflag = 0, iflag = 0, nflag = 0, rflag = 0, mflag = -1;
    off_t where;
    struct stat st;
    extern int optind, opterr;

    /* parse command line options */

    opterr = 0;
    while ((i = getopt(argc, argv, "bcfinrm:")) > 0 && i != '?') {
        switch(i) {
        case 'b':
            bflag++;
            break;
        case 'c':
            cflag++;
            break;
        case 'f':
        case 'i':
            iflag++;
            nflag = 0;
            break;
        case 'n':
            nflag++;
            iflag = 0;
            break;
        case 'r':
            rflag++;
            break;
        case 'm':
            mflag=atoi(optarg);
            break;
        }
    }
    if (i == '?' || optind >= argc) {
        fputs ("Usage: llook [ -i | -n ] [ -c ] [ -b ] [ -r ] [ -m num ] key "
            "[ sorted_file ... ]\n", stderr);
        exit(2);
    }
    i = optind;
    key = argv[i++];

    /* select the comparison function */

    if (iflag != 0) {
        compare = bflag == 0 ? cmp_case : cmp_case_white;
    }
    else if (nflag != 0) {
        compare = cmp_num;
    }
    else {
        compare = bflag == 0 ? cmp_exact : cmp_exact_white;
    }

    /* if no input files, then search stdin */

    if ((numfile = argc - i) == 0) {
        fstat(fileno(stdin), &st);
        if ((st.st_mode & S_IFREG) == 0) {
            fputs("llook: STDIN is not a regular file\n", stderr);
            exit(2);
        }
        where = binsrch(key, stdin, rflag);
        printmatch(key, stdin, where, 0, cflag, mflag);
        exit(where < 0);
    }

    /* search each input file */

    for (status = 1; i < argc; i++) {
        if ((fp = fopen(argv[i], "r")) == 0) {
            fprintf(stderr, "llook:  could not open %s\n", argv[i]);
            status = 2;
            continue;
        }
        fstat(fileno(fp), &st);
        if ((st.st_mode & S_IFREG) == 0) {
            fprintf(stderr, "llook: %s is not a regular file\n", argv[i]);
            status = 2;
            fclose(fp);
            continue;
        }
        where = binsrch(key, fp, rflag);
        printmatch(key, fp, where, numfile == 1 ? 0 : argv[i], cflag, mflag);
        if (status == 1 && where >= 0) {
            status = 0;
        }
        fclose(fp);
    }
    exit(status);
}
