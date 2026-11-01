/*
 * storage.h — loading and saving the archive.
 *
 * Format (version 1) is UTF-8 plain text, one record per line, fields
 * separated by TAB. Inside fields, backslash, tab, newline and carriage
 * return are escaped as \\ \t \n \r. Lines starting with '#' are comments.
 *
 *   CINEPHILIA 1
 *   F <id> <w|l> <year> <rating> <added> <watched> <title> <director> <genre> <notes>
 *   J <id> <film-id> <date> <text>
 *
 * w = watched, l = on the watchlist; year and rating 0 = unknown/unrated;
 * dates are YYYY-MM-DD or empty. The full description is in README.md.
 *
 * Loading is forgiving: a malformed line is skipped and counted rather than
 * failing the whole archive. Saving is careful: the new archive is written
 * to "<path>.tmp", flushed to disk, and renamed over the old file, which is
 * first preserved as "<path>.bak". An interrupted save therefore leaves
 * either the old archive or the new one, never half of each.
 */
#ifndef CINEPHILIA_STORAGE_H
#define CINEPHILIA_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

#include "film.h"

#define STORAGE_FORMAT_VERSION 1

typedef enum {
    STORAGE_OK,          /* loaded cleanly */
    STORAGE_NEW,         /* no file yet, or an empty file: start fresh */
    STORAGE_RECOVERED,   /* loaded, but some lines were unreadable and skipped */
    STORAGE_UNSUPPORTED, /* not a Cinephilia archive, or a newer format */
    STORAGE_IO_ERROR     /* the file exists but could not be read */
} StorageStatus;

typedef struct {
    size_t films;
    size_t entries;
    size_t skipped_lines;  /* malformed or invalid records */
    size_t orphan_entries; /* journal lines whose film is missing (also skipped) */
    long first_bad_line;   /* 1-based line number, 0 if none */
    char message[512];
} StorageReport;

/* Replaces the contents of `out` (which must be initialised) only when the
 * result is STORAGE_OK or STORAGE_RECOVERED; otherwise `out` is untouched. */
StorageStatus storage_load(const char *path, Archive *out, StorageReport *report);

/* Saves atomically as described above. On failure the previous file is left
 * as it was and err holds a human-readable reason. */
bool storage_save(const char *path, const Archive *a, char *err, size_t errcap);

/* Copies a file byte for byte (used to keep a copy of a damaged archive
 * before it is rewritten). */
bool storage_copy_file(const char *from, const char *to, char *err, size_t errcap);

#endif
