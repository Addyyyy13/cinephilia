/*
 * storage.c — versioned plain-text persistence with atomic saves.
 * See storage.h for the format.
 */
#include "storage.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "text.h"

#define MAGIC "CINEPHILIA"
#define FILM_FIELDS 11
#define ENTRY_FIELDS 5
#define MAX_FIELDS 16

/* ------------------------------------------------------------------ */
/* Reading                                                            */
/* ------------------------------------------------------------------ */

static void unescape(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '\\' && r[1] != '\0') {
            r++;
            switch (*r) {
            case 't': *w++ = '\t'; break;
            case 'n': *w++ = '\n'; break;
            case 'r': *w++ = '\r'; break;
            default: *w++ = *r; break; /* "\\" and unknown escapes */
            }
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
}

/* Splits on TAB in place. Returns the field count, or -1 if there are more
 * than max fields. */
static int split_tabs(char *line, char **fields, int max)
{
    int n = 0;
    fields[n++] = line;
    for (char *p = line; *p; p++) {
        if (*p == '\t') {
            if (n == max)
                return -1;
            *p = '\0';
            fields[n++] = p + 1;
        }
    }
    return n;
}

/* Cleans a field for storage in a fixed buffer: unescape, strip control
 * characters (so no stored text can emit terminal escapes), trim. */
static void take_field(char *dst, size_t cap, char *src)
{
    unescape(src);
    text_sanitize(src);
    text_trim(src);
    text_copy(dst, cap, src);
}

static bool parse_uint(const char *s, unsigned long max, unsigned long *out)
{
    if (*s == '\0')
        return false;
    unsigned long v = 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9')
            return false;
        unsigned d = (unsigned)(*p - '0');
        if (v > (max - d) / 10)
            return false;
        v = v * 10 + d;
    }
    *out = v;
    return true;
}

static bool parse_date_field(char *field, char out[DATE_LEN])
{
    text_trim(field);
    if (field[0] == '\0') {
        out[0] = '\0';
        return true;
    }
    if (!date_valid(field))
        return false;
    text_copy(out, DATE_LEN, field);
    return true;
}

/* Parses an F record. Returns NULL (and frees nothing) if invalid. */
static Film *parse_film(char **fld, const Archive *a)
{
    unsigned long id, year, rating;
    if (!parse_uint(fld[1], UINT_MAX, &id) || id == 0 || archive_find(a, (unsigned)id))
        return NULL;
    if (strcmp(fld[2], "w") != 0 && strcmp(fld[2], "l") != 0)
        return NULL;
    if (!parse_uint(fld[3], FILM_YEAR_MAX, &year) || (year != 0 && year < FILM_YEAR_MIN))
        return NULL;
    if (!parse_uint(fld[4], RATING_MAX, &rating))
        return NULL;

    Film *f = film_new();
    if (!f)
        return NULL;
    f->id = (unsigned)id;
    f->status = fld[2][0] == 'w' ? FILM_WATCHED : FILM_WATCHLIST;
    f->year = (int)year;
    f->rating = (int)rating;
    if (!parse_date_field(fld[5], f->added_on) || !parse_date_field(fld[6], f->watched_on)) {
        free(f);
        return NULL;
    }
    take_field(f->title, sizeof f->title, fld[7]);
    take_field(f->director, sizeof f->director, fld[8]);
    take_field(f->genre, sizeof f->genre, fld[9]);
    take_field(f->notes, sizeof f->notes, fld[10]);

    char err[64];
    if (!film_validate(f, err, sizeof err)) {
        free(f);
        return NULL;
    }
    return f;
}

typedef struct {
    unsigned id;
    unsigned film_id;
    char date[DATE_LEN];
    char *text;
} PendingEntry;

static void note_bad_line(StorageReport *r, long lineno)
{
    r->skipped_lines++;
    if (r->first_bad_line == 0)
        r->first_bad_line = lineno;
}

StorageStatus storage_load(const char *path, Archive *out, StorageReport *report)
{
    memset(report, 0, sizeof *report);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        if (errno == ENOENT) {
            snprintf(report->message, sizeof report->message,
                     "No archive yet at %s; a new one will be created.", path);
            return STORAGE_NEW;
        }
        snprintf(report->message, sizeof report->message, "Cannot open %s: %s",
                 path, strerror(errno));
        return STORAGE_IO_ERROR;
    }

    Archive tmp;
    archive_init(&tmp);
    PendingEntry *pending = NULL;
    size_t pending_n = 0;
    size_t pending_cap = 0;
    StorageStatus status = STORAGE_OK;
    bool header = false;
    bool any_content = false;
    long lineno = 0;
    char *line = NULL;
    size_t linecap = 0;
    ssize_t len;

    while ((len = getline(&line, &linecap, fp)) != -1) {
        lineno++;
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (line[0] == '\0' || line[0] == '#')
            continue;

        if (!header) {
            any_content = true;
            unsigned long version;
            size_t mlen = strlen(MAGIC);
            if (strncmp(line, MAGIC, mlen) == 0 && line[mlen] == ' ' &&
                parse_uint(line + mlen + 1, 1000000, &version)) {
                if (version == STORAGE_FORMAT_VERSION) {
                    header = true;
                    continue;
                }
                snprintf(report->message, sizeof report->message,
                         "%s uses archive format %lu, which is newer than this version "
                         "of Cinephilia understands (format %d). It was opened read-only.",
                         path, version, STORAGE_FORMAT_VERSION);
            } else {
                snprintf(report->message, sizeof report->message,
                         "%s does not look like a Cinephilia archive (line %ld). "
                         "It was opened read-only and will not be overwritten.",
                         path, lineno);
            }
            status = STORAGE_UNSUPPORTED;
            break;
        }

        char *fld[MAX_FIELDS];
        int n = split_tabs(line, fld, MAX_FIELDS);
        if (n == FILM_FIELDS && strcmp(fld[0], "F") == 0) {
            Film *f = parse_film(fld, &tmp);
            if (f && archive_append(&tmp, f))
                report->films++;
            else {
                free(f);
                note_bad_line(report, lineno);
            }
        } else if (n == ENTRY_FIELDS && strcmp(fld[0], "J") == 0) {
            unsigned long id, film_id;
            char date[DATE_LEN];
            if (!parse_uint(fld[1], UINT_MAX, &id) || id == 0 ||
                !parse_uint(fld[2], UINT_MAX, &film_id) || film_id == 0 ||
                !parse_date_field(fld[3], date) || date[0] == '\0') {
                note_bad_line(report, lineno);
                continue;
            }
            if (pending_n == pending_cap) {
                size_t cap = pending_cap ? pending_cap * 2 : 32;
                PendingEntry *grown = realloc(pending, cap * sizeof *grown);
                if (!grown) {
                    note_bad_line(report, lineno);
                    continue;
                }
                pending = grown;
                pending_cap = cap;
            }
            char text[FILM_NOTES_MAX];
            take_field(text, sizeof text, fld[4]);
            char *copy = malloc(strlen(text) + 1);
            if (!copy) {
                note_bad_line(report, lineno);
                continue;
            }
            strcpy(copy, text);
            PendingEntry *pe = &pending[pending_n++];
            pe->id = (unsigned)id;
            pe->film_id = (unsigned)film_id;
            text_copy(pe->date, sizeof pe->date, date);
            pe->text = copy;
        } else {
            note_bad_line(report, lineno);
        }
    }

    bool read_error = ferror(fp) != 0;
    free(line);
    fclose(fp);

    /* Journal lines are resolved after all films are known, so their order
     * in the file does not matter. */
    if (status == STORAGE_OK && !read_error) {
        for (size_t i = 0; i < pending_n; i++) {
            PendingEntry *pe = &pending[i];
            if (!archive_find(&tmp, pe->film_id)) {
                report->orphan_entries++;
                report->skipped_lines++;
            } else if (archive_entry_find(&tmp, pe->id) ||
                       archive_add_entry(&tmp, pe->id, pe->film_id, pe->date, pe->text) == 0) {
                report->skipped_lines++;
            } else {
                report->entries++;
            }
        }
    }
    for (size_t i = 0; i < pending_n; i++)
        free(pending[i].text);
    free(pending);

    if (read_error) {
        snprintf(report->message, sizeof report->message, "Error while reading %s.", path);
        archive_free(&tmp);
        return STORAGE_IO_ERROR;
    }
    if (status == STORAGE_UNSUPPORTED) {
        archive_free(&tmp);
        return status;
    }
    if (!header) {
        archive_free(&tmp);
        if (any_content) /* unreachable: content without header is unsupported */
            return STORAGE_UNSUPPORTED;
        snprintf(report->message, sizeof report->message,
                 "%s is empty; starting a new archive.", path);
        return STORAGE_NEW;
    }

    if (report->skipped_lines > 0) {
        status = STORAGE_RECOVERED;
        snprintf(report->message, sizeof report->message,
                 "Skipped %zu unreadable line%s in %s (first at line %ld).",
                 report->skipped_lines, report->skipped_lines == 1 ? "" : "s", path,
                 report->first_bad_line ? report->first_bad_line : 0L);
    } else {
        snprintf(report->message, sizeof report->message, "Loaded %zu films.", report->films);
    }
    archive_free(out);
    *out = tmp;
    return status;
}

/* ------------------------------------------------------------------ */
/* Writing                                                            */
/* ------------------------------------------------------------------ */

static bool write_field(FILE *fp, const char *s)
{
    for (; *s; s++) {
        int rc;
        switch (*s) {
        case '\\': rc = fputs("\\\\", fp); break;
        case '\t': rc = fputs("\\t", fp); break;
        case '\n': rc = fputs("\\n", fp); break;
        case '\r': rc = fputs("\\r", fp); break;
        default: rc = fputc((unsigned char)*s, fp); break;
        }
        if (rc == EOF)
            return false;
    }
    return true;
}

/* Flushes file contents to stable storage. On macOS plain fsync() only
 * reaches the drive's cache; F_FULLFSYNC asks the drive to flush too. */
static int full_sync(int fd)
{
#ifdef F_FULLFSYNC
    if (fcntl(fd, F_FULLFSYNC) == 0)
        return 0;
#endif
    return fsync(fd);
}

static bool ensure_parent_dir(const char *path, char *err, size_t errcap)
{
    char buf[PATH_MAX];
    if (text_copy(buf, sizeof buf, path) != strlen(path)) {
        snprintf(err, errcap, "path is too long");
        return false;
    }
    char *slash = strrchr(buf, '/');
    if (!slash || slash == buf)
        return true; /* current directory or root */
    *slash = '\0';
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buf, 0700) != 0 && errno != EEXIST) {
                snprintf(err, errcap, "cannot create folder %s: %s", buf, strerror(errno));
                return false;
            }
            *p = '/';
        }
    }
    if (mkdir(buf, 0700) != 0 && errno != EEXIST) {
        snprintf(err, errcap, "cannot create folder %s: %s", buf, strerror(errno));
        return false;
    }
    struct stat st;
    if (stat(buf, &st) != 0 || !S_ISDIR(st.st_mode)) {
        snprintf(err, errcap, "%s is not a folder", buf);
        return false;
    }
    return true;
}

static void sync_parent_dir(const char *path)
{
    char buf[PATH_MAX];
    text_copy(buf, sizeof buf, path);
    char *slash = strrchr(buf, '/');
    if (slash == buf)
        slash[1] = '\0';
    else if (slash)
        *slash = '\0';
    else
        strcpy(buf, ".");
    int fd = open(buf, O_RDONLY);
    if (fd >= 0) {
        (void)fsync(fd); /* best effort; not supported everywhere */
        close(fd);
    }
}

bool storage_copy_file(const char *from, const char *to, char *err, size_t errcap)
{
    int in = open(from, O_RDONLY);
    if (in < 0) {
        snprintf(err, errcap, "cannot read %s: %s", from, strerror(errno));
        return false;
    }
    int outfd = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (outfd < 0) {
        snprintf(err, errcap, "cannot create %s: %s", to, strerror(errno));
        close(in);
        return false;
    }
    char buf[8192];
    bool ok = true;
    for (;;) {
        ssize_t n = read(in, buf, sizeof buf);
        if (n == 0)
            break;
        if (n < 0) {
            if (errno == EINTR)
                continue;
            ok = false;
            break;
        }
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(outfd, buf + off, (size_t)(n - off));
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                ok = false;
                break;
            }
            off += w;
        }
        if (!ok)
            break;
    }
    if (ok && full_sync(outfd) != 0)
        ok = false;
    if (!ok)
        snprintf(err, errcap, "copying %s failed: %s", from, strerror(errno));
    close(in);
    if (close(outfd) != 0)
        ok = false;
    return ok;
}

/* Keeps the archive being replaced as "<path>.bak". A hard link is instant
 * and atomic; if the filesystem refuses one we fall back to a copy. */
static void keep_backup(const char *path)
{
    char bak[PATH_MAX];
    if (snprintf(bak, sizeof bak, "%s.bak", path) >= (int)sizeof bak)
        return;
    if (access(path, F_OK) != 0)
        return;
    if (unlink(bak) != 0 && errno != ENOENT)
        return;
    if (link(path, bak) != 0) {
        char err[256];
        (void)storage_copy_file(path, bak, err, sizeof err);
    }
}

bool storage_save(const char *path, const Archive *a, char *err, size_t errcap)
{
    if (!ensure_parent_dir(path, err, errcap))
        return false;

    char tmp[PATH_MAX];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) {
        snprintf(err, errcap, "path is too long");
        return false;
    }

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        snprintf(err, errcap, "cannot write %s: %s", tmp, strerror(errno));
        return false;
    }
    FILE *fp = fdopen(fd, "w");
    if (!fp) {
        snprintf(err, errcap, "cannot write %s: %s", tmp, strerror(errno));
        close(fd);
        unlink(tmp);
        return false;
    }

    bool ok = true;
    int saved_errno = 0;
#define CHECK(expr)                                  \
    do {                                             \
        if (ok && !(expr)) {                         \
            ok = false;                              \
            saved_errno = errno ? errno : EIO;       \
        }                                            \
    } while (0)

    errno = 0;
    CHECK(fputs("# Cinephilia archive. Plain UTF-8 text; one record per line.\n"
                "# Format described in README.md. Edit only while Cinephilia is closed.\n",
                fp) != EOF);
    CHECK(fprintf(fp, "%s %d\n", MAGIC, STORAGE_FORMAT_VERSION) > 0);

    for (const Film *f = a->head; f && ok; f = f->next) {
        CHECK(fprintf(fp, "F\t%u\t%c\t%d\t%d\t%s\t%s\t", f->id,
                      f->status == FILM_WATCHED ? 'w' : 'l', f->year, f->rating,
                      f->added_on, f->watched_on) > 0);
        CHECK(write_field(fp, f->title));
        CHECK(fputc('\t', fp) != EOF);
        CHECK(write_field(fp, f->director));
        CHECK(fputc('\t', fp) != EOF);
        CHECK(write_field(fp, f->genre));
        CHECK(fputc('\t', fp) != EOF);
        CHECK(write_field(fp, f->notes));
        CHECK(fputc('\n', fp) != EOF);
    }
    for (size_t i = 0; i < a->journal_count && ok; i++) {
        const JournalEntry *e = &a->journal[i];
        CHECK(fprintf(fp, "J\t%u\t%u\t%s\t", e->id, e->film_id, e->date) > 0);
        CHECK(write_field(fp, e->text));
        CHECK(fputc('\n', fp) != EOF);
    }
    CHECK(fflush(fp) == 0);
    CHECK(full_sync(fileno(fp)) == 0);
    if (fclose(fp) != 0 && ok) {
        ok = false;
        saved_errno = errno ? errno : EIO;
    }
#undef CHECK

    if (!ok) {
        snprintf(err, errcap, "writing %s failed: %s", tmp, strerror(saved_errno));
        unlink(tmp);
        return false;
    }

    keep_backup(path);
    if (rename(tmp, path) != 0) {
        snprintf(err, errcap, "cannot replace %s: %s", path, strerror(errno));
        unlink(tmp);
        return false;
    }
    sync_parent_dir(path);
    return true;
}
