/*
 * storage.c — Persistence layer for film archive data
 * Part of CINEPHILIA: a personal film archive
 *
 * File format (v2):
 *   First line:  #CINEPHILIA_V2
 *   Each record:  id|title|director|year|rating|genre|notes|watched|date_watched|journal
 *
 * Supports migration from v1 (5-field) format and atomic writes via
 * temporary file + rename.
 */

#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── paths ─────────────────────────────────────────────────────────── */

#define DATA_DIR        "data"
#define V2_PATH         DATA_DIR "/films.dat"
#define V2_TMP_PATH     DATA_DIR "/films.dat.tmp"
#define V1_PATH         DATA_DIR "/films.txt"
#define V1_BAK_PATH     DATA_DIR "/films.txt.bak"
#define V2_HEADER       "#CINEPHILIA_V2"

/* ── helpers ───────────────────────────────────────────────────────── */

/*
 * sanitize_string — strip control characters (< 0x20) from a string,
 * preserving tabs.  Modifies the buffer in place.
 */
static void sanitize_string(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if ((unsigned char)*r >= 0x20 || *r == '\t') {
            *w++ = *r;
        }
        r++;
    }
    *w = '\0';
}

/*
 * parse_field — extract the next pipe-delimited field from `line`
 * starting at byte offset *pos.  Copies up to maxlen-1 characters into
 * `buf` and null-terminates.  Advances *pos past the delimiter.
 *
 * Returns 1 if a field was consumed (even if empty), 0 at end-of-line.
 */
static int parse_field(const char *line, int *pos, char *buf, int maxlen) {
    int start = *pos;
    int i = 0;

    while (line[*pos] && line[*pos] != '|' && line[*pos] != '\n') {
        if (i < maxlen - 1) {
            buf[i++] = line[*pos];
        }
        (*pos)++;
    }
    buf[i] = '\0';

    /* advance past the pipe delimiter if present */
    if (line[*pos] == '|') {
        (*pos)++;
    }

    /* a field was consumed if we moved at all or hit a pipe */
    return (*pos > start) ? 1 : 0;
}

/* ── directory management ──────────────────────────────────────────── */

/*
 * storage_ensure_dir — create the data/ directory if it does not
 * already exist.  Returns 0 on success, -1 on error.
 */
int storage_ensure_dir(void) {
    struct stat st;
    if (stat(DATA_DIR, &st) == 0) {
        /* exists — make sure it's a directory */
        if (S_ISDIR(st.st_mode)) return 0;
        return -1;  /* exists but is not a directory */
    }

    if (mkdir(DATA_DIR, 0755) != 0) {
        return -1;
    }
    return 0;
}

/* ── v1 loader (migration) ─────────────────────────────────────────── */

/*
 * load_v1 — read the legacy 5-field format from V1_PATH into the
 * archive, filling missing v2 fields with sensible defaults:
 *   genre        = "Unclassified"
 *   notes        = ""
 *   watched      = 1
 *   date_watched = ""
 *   journal      = ""
 *
 * After loading, backs up films.txt → films.txt.bak and saves the
 * archive in v2 format.
 *
 * Returns: number of records loaded, or -1 on error.
 */
static int load_v1(Archive *ar) {
    FILE *fp = fopen(V1_PATH, "r");
    if (fp == NULL) return -1;

    char line[4096];
    int loaded = 0;
    int max_id = 0;

    while (fgets(line, sizeof(line), fp)) {
        /* skip blank lines */
        if (line[0] == '\n' || line[0] == '\0') continue;

        Film film;
        memset(&film, 0, sizeof(film));

        int pos = 0;
        char id_buf[16];

        /* v1 format: id|title|director|year|rating */
        if (!parse_field(line, &pos, id_buf, sizeof(id_buf)))       continue;
        if (!parse_field(line, &pos, film.title, sizeof(film.title))) continue;
        if (!parse_field(line, &pos, film.director, sizeof(film.director))) continue;

        char year_buf[16];
        if (!parse_field(line, &pos, year_buf, sizeof(year_buf)))   continue;

        char rating_buf[16];
        if (!parse_field(line, &pos, rating_buf, sizeof(rating_buf))) continue;

        film.id   = atoi(id_buf);
        film.year = atoi(year_buf);
        film.rating = (float)atof(rating_buf);

        /* sanitize text fields */
        sanitize_string(film.title);
        sanitize_string(film.director);

        /* fill in v2 defaults */
        strncpy(film.genre, "Unclassified", sizeof(film.genre) - 1);
        film.genre[sizeof(film.genre) - 1] = '\0';
        film.notes[0]        = '\0';
        film.watched         = 1;
        film.date_watched[0] = '\0';
        film.journal[0]      = '\0';

        if (film.id <= 0) continue;  /* skip malformed ids */

        /* track the highest id so we can set next_id correctly */
        if (film.id > max_id) max_id = film.id;

        /* insert directly: allocate and append to list */
        Film *new_film = malloc(sizeof(Film));
        if (new_film == NULL) continue;

        *new_film = film;
        new_film->next = NULL;

        if (ar->head == NULL) {
            ar->head = new_film;
        } else {
            Film *cur = ar->head;
            while (cur->next) cur = cur->next;
            cur->next = new_film;
        }
        ar->count++;
        loaded++;
    }

    fclose(fp);

    ar->next_id  = max_id + 1;
    ar->modified = 1;  /* mark dirty so caller saves as v2 */

    /* back up the old v1 file */
    rename(V1_PATH, V1_BAK_PATH);

    /* immediately persist in v2 format */
    storage_save(ar);

    return loaded;
}

/* ── v2 loader ─────────────────────────────────────────────────────── */

/*
 * load_v2 — read v2 pipe-delimited records from V2_PATH.
 * Returns: number of records loaded, or -1 on error.
 */
static int load_v2(Archive *ar) {
    FILE *fp = fopen(V2_PATH, "r");
    if (fp == NULL) return -1;

    char line[4096];
    int loaded = 0;
    int max_id = 0;

    /* first line must be the format header */
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return -1;
    }

    /* strip trailing newline for comparison */
    line[strcspn(line, "\r\n")] = '\0';
    if (strcmp(line, V2_HEADER) != 0) {
        fclose(fp);
        return -1;  /* unrecognised format */
    }

    /* read records */
    while (fgets(line, sizeof(line), fp)) {
        /* skip blank lines */
        if (line[0] == '\n' || line[0] == '\0') continue;

        Film film;
        memset(&film, 0, sizeof(film));

        int pos = 0;
        char id_buf[16], year_buf[16], rating_buf[16];
        char watched_buf[8];

        /* 10 fields: id|title|director|year|rating|genre|notes|watched|date_watched|journal */
        if (!parse_field(line, &pos, id_buf,    sizeof(id_buf)))            continue;
        if (!parse_field(line, &pos, film.title, sizeof(film.title)))       continue;
        if (!parse_field(line, &pos, film.director, sizeof(film.director))) continue;
        if (!parse_field(line, &pos, year_buf,   sizeof(year_buf)))         continue;
        if (!parse_field(line, &pos, rating_buf, sizeof(rating_buf)))       continue;
        if (!parse_field(line, &pos, film.genre,  sizeof(film.genre)))      continue;
        if (!parse_field(line, &pos, film.notes,  sizeof(film.notes)))      continue;
        if (!parse_field(line, &pos, watched_buf, sizeof(watched_buf)))     continue;
        if (!parse_field(line, &pos, film.date_watched, sizeof(film.date_watched))) continue;
        /* journal is the last field — may be empty */
        parse_field(line, &pos, film.journal, sizeof(film.journal));

        film.id      = atoi(id_buf);
        film.year    = atoi(year_buf);
        film.rating  = (float)atof(rating_buf);
        film.watched = atoi(watched_buf);

        /* sanitize all text fields */
        sanitize_string(film.title);
        sanitize_string(film.director);
        sanitize_string(film.genre);
        sanitize_string(film.notes);
        sanitize_string(film.date_watched);
        sanitize_string(film.journal);

        if (film.id <= 0) continue;  /* skip malformed records */

        if (film.id > max_id) max_id = film.id;

        /* allocate and append */
        Film *new_film = malloc(sizeof(Film));
        if (new_film == NULL) continue;

        *new_film = film;
        new_film->next = NULL;

        if (ar->head == NULL) {
            ar->head = new_film;
        } else {
            Film *cur = ar->head;
            while (cur->next) cur = cur->next;
            cur->next = new_film;
        }
        ar->count++;
        loaded++;
    }

    fclose(fp);

    ar->next_id  = max_id + 1;
    ar->modified = 0;  /* freshly loaded — no unsaved changes */

    return loaded;
}

/* ── public API ────────────────────────────────────────────────────── */

/*
 * storage_load — populate the archive from persistent storage.
 *
 *   1. Try v2 file (data/films.dat)
 *   2. If absent, try migrating from v1 (data/films.txt)
 *   3. If neither exists, return 0 (first launch — empty archive)
 *
 * Returns: number of records loaded, or -1 on I/O error.
 */
int storage_load(Archive *ar) {
    archive_init(ar);

    if (storage_ensure_dir() != 0) return -1;

    /* attempt v2 load */
    int result = load_v2(ar);
    if (result >= 0) return result;

    /* v2 not found — try v1 migration */
    if (access(V1_PATH, F_OK) == 0) {
        result = load_v1(ar);
        if (result >= 0) return result;
    }

    /* neither file exists: first launch */
    return 0;
}

/*
 * storage_save — persist the archive to disk using atomic write.
 *
 * Writes all records to a temporary file (films.dat.tmp), then renames
 * it to films.dat.  This ensures readers never see a partially-written
 * file.
 *
 * Returns: 0 on success, -1 on error.
 */
int storage_save(Archive *ar) {
    if (storage_ensure_dir() != 0) return -1;

    FILE *fp = fopen(V2_TMP_PATH, "w");
    if (fp == NULL) return -1;

    /* write format header */
    fprintf(fp, "%s\n", V2_HEADER);

    /* write each record as a pipe-delimited line */
    Film *f = ar->head;
    while (f) {
        fprintf(fp, "%d|%s|%s|%d|%.1f|%s|%s|%d|%s|%s\n",
                f->id,
                f->title,
                f->director,
                f->year,
                f->rating,
                f->genre,
                f->notes,
                f->watched,
                f->date_watched,
                f->journal);
        f = f->next;
    }

    /* flush and check for write errors before renaming */
    if (fflush(fp) != 0 || ferror(fp)) {
        fclose(fp);
        unlink(V2_TMP_PATH);
        return -1;
    }
    fclose(fp);

    /* atomic rename: tmp → final */
    if (rename(V2_TMP_PATH, V2_PATH) != 0) {
        unlink(V2_TMP_PATH);
        return -1;
    }

    ar->modified = 0;
    return 0;
}
