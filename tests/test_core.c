/*
 * test_core.c — unit tests for everything below the screens: text, the
 * wordmark, the linked-list archive, merge sort, search, storage (including
 * damaged files and failed writes), discovery, statistics, settings and the
 * audio process controller.
 *
 * Build and run with `make test`. Exit status is the number of failures.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "discover.h"
#include "dsa/stack.h"
#include "dsa/tally.h"
#include "film.h"
#include "json.h"
#include "search.h"
#include "settings.h"
#include "stats.h"
#include "storage.h"
#include "text.h"
#include "tmdb.h"
#include "wordmark.h"

static int g_checks = 0;
static int g_failures = 0;
static const char *g_group = "";

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        g_checks++;                                                                       \
        if (!(cond)) {                                                                    \
            g_failures++;                                                                 \
            fprintf(stderr, "  FAIL [%s] %s:%d: %s\n", g_group, __FILE__, __LINE__, #cond); \
        }                                                                                 \
    } while (0)

#define CHECK_STR(a, b)                                                                   \
    do {                                                                                  \
        g_checks++;                                                                       \
        if (strcmp((a), (b)) != 0) {                                                      \
            g_failures++;                                                                 \
            fprintf(stderr, "  FAIL [%s] %s:%d: \"%s\" != \"%s\"\n", g_group, __FILE__,   \
                    __LINE__, (a), (b));                                                  \
        }                                                                                 \
    } while (0)

static char g_tmp[200];

static void group(const char *name)
{
    g_group = name;
    printf("  %s\n", name);
}

static void tmp_path(char *out, size_t cap, const char *name)
{
    snprintf(out, cap, "%s/%s", g_tmp, name);
}

static void write_file(const char *path, const char *content)
{
    FILE *fp = fopen(path, "w");
    if (!fp) {
        perror(path);
        exit(1);
    }
    fputs(content, fp);
    fclose(fp);
}

static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return NULL;
    static char buf[1 << 16];
    size_t n = fread(buf, 1, sizeof buf - 1, fp);
    buf[n] = '\0';
    fclose(fp);
    return buf;
}

static Film *add(Archive *a, const char *title, const char *director, int year, const char *genre,
                 FilmStatus status, int rating)
{
    Film *f = film_new();
    text_copy(f->title, sizeof f->title, title);
    text_copy(f->director, sizeof f->director, director);
    text_copy(f->genre, sizeof f->genre, genre);
    f->year = year;
    f->status = status;
    f->rating = rating;
    text_copy(f->added_on, sizeof f->added_on, "2026-01-01");
    archive_append(a, f);
    return f;
}

/* ------------------------------------------------------------------ */

static void test_text(void)
{
    group("text: width, truncation, wrapping, sanitising");
    char buf[256];
    CHECK(text_width("Cinema") == 6);
    CHECK(text_width("Am\xC3\xA9lie") == 6);               /* é is one column */
    CHECK(text_width("\xE8\x8A\xB1\xE6\xA7\x98") == 4);    /* 花様: two wide characters */

    CHECK(text_fit(buf, sizeof buf, "In the Mood for Love", 10) == 10);
    CHECK_STR(buf, "In the Mo\xE2\x80\xA6");
    CHECK(text_fit(buf, sizeof buf, "Short", 10) == 5);
    CHECK_STR(buf, "Short");
    text_fit(buf, sizeof buf, "\xE8\x8A\xB1\xE6\xA7\x98\xE5\xB9\xB4\xE8\x8F\xAF", 5); /* 花様年華 */
    CHECK(text_width(buf) <= 5);

    /* text_copy never splits a UTF-8 sequence. */
    char small[4];
    text_copy(small, sizeof small, "a\xC3\xA9\xC3\xA9");
    CHECK_STR(small, "a\xC3\xA9");
    text_copy(small, sizeof small, "ab\xC3\xA9");
    CHECK_STR(small, "ab");

    /* Escape sequences and control characters cannot survive. */
    char evil[] = "Title\033[31m\twith\nbreaks\x7f\xC2\x9B" "end\xFF";
    text_sanitize(evil);
    CHECK(strchr(evil, '\033') == NULL);
    CHECK(strchr(evil, '\n') == NULL && strchr(evil, '\t') == NULL);
    CHECK(strstr(evil, "\xC2\x9B") == NULL);
    CHECK(evil[strlen(evil) - 1] == '?');

    char trimmed[] = "  padded  ";
    text_trim(trimmed);
    CHECK_STR(trimmed, "padded");

    const char *para = "Two lovers who never quite meet, in corridors of rain and cigarette smoke";
    const char *p = para;
    size_t n;
    int lines = 0;
    bool all_fit = true;
    while ((n = text_wrap_next(p, 20, buf, sizeof buf)) > 0) {
        if (text_width(buf) > 20)
            all_fit = false;
        lines++;
        p += n;
    }
    CHECK(all_fit && lines >= 4 && lines <= 5);
    CHECK(text_wrap_next("supercalifragilistic", 5, buf, sizeof buf) == 5); /* hard break */
    CHECK(text_wrap_next("     ", 10, buf, sizeof buf) == 0);

    text_upper(buf, sizeof buf, "Cl\xC3\xA9o de 5 \xC3\xA0 7 \xC5\x82\xC3\xB3" "d\xC5\xBA");
    CHECK_STR(buf, "CL\xC3\x89O DE 5 \xC3\x80 7 \xC5\x81\xC3\x93" "D\xC5\xB9");

    CHECK(text_casecmp("Stalker", "STALKER") == 0);
    CHECK(text_contains_ci("Mulholland Drive", "HOLL"));
    CHECK(!text_contains_ci("Mulholland Drive", "Lynch"));
    long v;
    CHECK(text_parse_int(" 42 ", 1, 100, &v) && v == 42);
    CHECK(!text_parse_int("42abc", 1, 100, &v));
    CHECK(!text_parse_int("101", 1, 100, &v));
    CHECK(!text_parse_int("", 1, 100, &v));
    CHECK(!text_parse_int("99999999999999999999999", 1, 100, &v));

    group("text: dates");
    CHECK(date_valid("2024-02-29"));
    CHECK(!date_valid("2023-02-29"));
    CHECK(!date_valid("1900-02-29"));
    CHECK(date_valid("2000-02-29"));
    CHECK(!date_valid("2024-13-01"));
    CHECK(!date_valid("2024-1-01"));
    CHECK(!date_valid("yesterday"));
    date_pretty("2026-10-22", buf, sizeof buf);
    CHECK_STR(buf, "22 Oct 2026");
}

static void test_wordmark(void)
{
    group("wordmark spells C-I-N-E-P-H-I-L-I-A");
    for (int gap = 1; gap <= 3; gap++) {
        char rows[WM_ROWS][WM_MAX_COLS + 1];
        int w = wordmark_render(WM_WORD, gap, rows);
        CHECK(w > 0);
        char decoded[32];
        int n = wordmark_decode(rows, w, decoded, sizeof decoded);
        CHECK(n == 10);
        CHECK_STR(decoded, "CINEPHILIA");
    }
    CHECK_STR(WM_WORD, "CINEPHILIA");
    CHECK(wordmark_width("CINEMA!", 2) == -1); /* unsupported letters are refused */
    /* Every glyph's rows really are its declared width. */
    for (const char *p = "ACEHILNP"; *p; p++) {
        const WmGlyph *g = wordmark_glyph(*p);
        CHECK(g != NULL);
        for (int r = 0; g && r < WM_ROWS; r++)
            CHECK((int)strlen(g->rows[r]) == g->width);
    }
}

static void test_archive(void)
{
    group("archive: linked list, ids, duplicates");
    Archive a;
    archive_init(&a);
    Film *f1 = add(&a, "Stalker", "Andrei Tarkovsky", 1979, "Science Fiction", FILM_WATCHED, 10);
    Film *f2 = add(&a, "Mirror", "Andrei Tarkovsky", 1975, "Drama", FILM_WATCHLIST, 0);
    Film *f3 = add(&a, "Persona", "Ingmar Bergman", 1966, "Drama", FILM_WATCHED, 9);
    CHECK(a.count == 3 && f1->id == 1 && f2->id == 2 && f3->id == 3);
    CHECK(a.head == f1 && a.tail == f3 && f2->prev == f1 && f2->next == f3);
    CHECK(archive_find(&a, 2) == f2 && archive_find(&a, 99) == NULL);
    CHECK(archive_find_duplicate(&a, "STALKER", 1979, NULL) == f1);
    CHECK(archive_find_duplicate(&a, "Stalker", 0, NULL) == f1);    /* unknown year matches */
    CHECK(archive_find_duplicate(&a, "Stalker", 2030, NULL) == NULL); /* a remake does not */
    CHECK(archive_find_duplicate(&a, "Stalker", 1979, f1) == NULL);
    CHECK(archive_count_status(&a, FILM_WATCHLIST) == 1);

    char err[64];
    Film bad;
    memset(&bad, 0, sizeof bad);
    CHECK(!film_validate(&bad, err, sizeof err)); /* no title */
    text_copy(bad.title, sizeof bad.title, "X");
    bad.rating = 11;
    CHECK(!film_validate(&bad, err, sizeof err));
    bad.rating = 0;
    bad.year = 1700;
    CHECK(!film_validate(&bad, err, sizeof err));

    group("archive: journal and viewings");
    CHECK(film_record_viewing(&a, f2, "2026-03-01", 8, "Memory as architecture.") > 0);
    CHECK(f2->status == FILM_WATCHED && f2->rating == 8);
    CHECK_STR(f2->watched_on, "2026-03-01");
    CHECK(film_record_viewing(&a, f2, "2025-01-01", 0, "") > 0); /* older viewing */
    CHECK_STR(f2->watched_on, "2026-03-01");                     /* last seen unchanged */
    CHECK(f2->rating == 8);                                      /* 0 keeps the rating */
    CHECK(archive_entry_count(&a, f2->id) == 2);

    group("archive: delete, undo stack, restore");
    Stack undo;
    stack_init(&undo);
    unsigned id2 = f2->id;
    DeletedFilm *d = archive_delete_film(&a, f2);
    CHECK(d != NULL && d->entry_count == 2);
    CHECK(a.count == 2 && a.journal_count == 0 && archive_find(&a, id2) == NULL);
    CHECK(f1->next == f3 && f3->prev == f1);
    CHECK(stack_push(&undo, d));
    DeletedFilm *d3 = archive_delete_film(&a, f3);
    CHECK(stack_push(&undo, d3));
    CHECK(a.count == 1 && a.head == f1 && a.tail == f1);
    /* LIFO: the most recent deletion comes back first. */
    DeletedFilm *top = stack_pop(&undo);
    CHECK(top == d3);
    CHECK(archive_restore(&a, top));
    top = stack_pop(&undo);
    CHECK(archive_restore(&a, top));
    CHECK(a.count == 3 && archive_entry_count(&a, id2) == 2);
    CHECK(stack_pop(&undo) == NULL);
    stack_free(&undo, deleted_film_free);
    /* Ids are never reused. */
    Film *f4 = add(&a, "Close-Up", "Abbas Kiarostami", 1990, "Drama", FILM_WATCHLIST, 0);
    CHECK(f4->id == 4);
    archive_free(&a);
}

static int sorted_by(const Archive *a, SortKey key, bool desc)
{
    for (const Film *f = a->head; f && f->next; f = f->next) {
        const Film *g = f->next;
        int c = 0;
        switch (key) {
        case SORT_YEAR:
            if (f->year && !g->year)
                continue;
            if (!f->year && g->year)
                return 0;
            c = (f->year > g->year) - (f->year < g->year);
            break;
        case SORT_RATING:
            if (f->rating && !g->rating)
                continue;
            if (!f->rating && g->rating)
                return 0;
            c = (f->rating > g->rating) - (f->rating < g->rating);
            break;
        case SORT_TITLE:
            c = text_casecmp(film_sort_title(f->title), film_sort_title(g->title));
            break;
        default:
            continue;
        }
        if (desc ? c < 0 : c > 0)
            return 0;
    }
    return 1;
}

static void test_sort_search(void)
{
    group("merge sort on the linked list");
    Archive a;
    archive_init(&a);
    srand(7);
    for (int i = 0; i < 600; i++) {
        char title[32];
        snprintf(title, sizeof title, "%sFilm %04d", (i % 5 == 0) ? "The " : "", rand() % 2000);
        add(&a, title, (i % 3) ? "Someone" : "", (i % 11) ? 1900 + rand() % 120 : 0, "Drama",
            (i % 2) ? FILM_WATCHED : FILM_WATCHLIST, (i % 7) ? 1 + rand() % 10 : 0);
    }
    const SortKey keys[] = {SORT_TITLE, SORT_YEAR, SORT_RATING};
    for (int k = 0; k < 3; k++) {
        for (int desc = 0; desc <= 1; desc++) {
            archive_sort(&a, keys[k], desc);
            CHECK(sorted_by(&a, keys[k], desc));
            size_t n = 0;
            const Film *prev = NULL;
            bool links_ok = true;
            for (const Film *f = a.head; f; f = f->next) {
                if (f->prev != prev)
                    links_ok = false;
                prev = f;
                n++;
            }
            CHECK(links_ok && a.tail == prev && n == 600);
        }
    }
    CHECK_STR(film_sort_title("The Godfather"), "Godfather");
    CHECK_STR(film_sort_title("A"), "A");
    archive_free(&a);

    /* Stability: equal titles keep their relative (id) order. */
    archive_init(&a);
    for (int i = 0; i < 20; i++)
        add(&a, "Same", "", 2000, "", FILM_WATCHED, 5);
    archive_sort(&a, SORT_RATING, true);
    unsigned last = 0;
    bool stable = true;
    for (const Film *f = a.head; f; f = f->next) {
        if (f->id < last)
            stable = false;
        last = f->id;
    }
    CHECK(stable);
    archive_free(&a);

    group("search and filters");
    archive_init(&a);
    add(&a, "In the Mood for Love", "Wong Kar-wai", 2000, "Romance", FILM_WATCHED, 9);
    add(&a, "Chungking Express", "Wong Kar-wai", 1994, "Romance", FILM_WATCHED, 8);
    add(&a, "Stalker", "Andrei Tarkovsky", 1979, "Science Fiction", FILM_WATCHED, 10);
    add(&a, "Playtime", "Jacques Tati", 1967, "Comedy", FILM_WATCHLIST, 0);
    FilmFilter fl;
    film_filter_init(&fl);
    const Film **out;
    size_t n;
    text_copy(fl.query, sizeof fl.query, "wong");
    CHECK(archive_collect(&a, &fl, &out, &n) && n == 2);
    free(out);
    text_copy(fl.query, sizeof fl.query, "MOOD");
    CHECK(archive_collect(&a, &fl, &out, &n) && n == 1 && strcmp(out[0]->title, "In the Mood for Love") == 0);
    free(out);
    text_copy(fl.query, sizeof fl.query, "Kubrick");
    CHECK(archive_collect(&a, &fl, &out, &n) && n == 0 && out == NULL);
    film_filter_init(&fl);
    text_copy(fl.genre, sizeof fl.genre, "romance");
    fl.min_rating = 9;
    CHECK(archive_collect(&a, &fl, &out, &n) && n == 1);
    free(out);
    film_filter_init(&fl);
    fl.year_from = 1970;
    fl.year_to = 1999;
    CHECK(archive_collect(&a, &fl, &out, &n) && n == 2);
    free(out);
    film_filter_init(&fl);
    fl.status = FILM_WATCHLIST;
    CHECK(archive_collect(&a, &fl, &out, &n) && n == 1);
    free(out);
    CHECK(film_filter_active(&fl));
    char desc[128];
    film_filter_describe(&fl, desc, sizeof desc);
    CHECK(strstr(desc, "watchlist") != NULL);
    archive_free(&a);
}

static void test_storage(void)
{
    char path[600], bak[700], text[4096];
    StorageReport rep;
    Archive a, b;

    group("storage: first launch");
    tmp_path(path, sizeof path, "new/sub/archive.db");
    archive_init(&a);
    CHECK(storage_load(path, &a, &rep) == STORAGE_NEW && a.count == 0);
    tmp_path(path, sizeof path, "empty.db");
    write_file(path, "");
    CHECK(storage_load(path, &a, &rep) == STORAGE_NEW);

    group("storage: round trip with awkward text");
    add(&a, "Jeanne Dielman, 23 quai du Commerce, 1080 Bruxelles", "Chantal Akerman", 1975, "Drama",
        FILM_WATCHED, 10);
    Film *f = add(&a, "\xE8\x8A\xB1\xE6\xA7\x98\xE5\xB9\xB4\xE8\x8F\xAF", "Wong Kar-wai", 2000, "Romance",
                  FILM_WATCHLIST, 0);
    text_copy(f->notes, sizeof f->notes, "Back\\slash and C:\\path");
    Film *g = add(&a, "Untitled", "", 0, "", FILM_WATCHED, 0);
    film_record_viewing(&a, g, "2026-10-20", 7, "Second viewing \\n not a newline");
    tmp_path(path, sizeof path, "new/sub/archive.db"); /* folders are created as needed */
    char err[512];
    CHECK(storage_save(path, &a, err, sizeof err));
    archive_init(&b);
    CHECK(storage_load(path, &b, &rep) == STORAGE_OK);
    CHECK(b.count == 3 && b.journal_count == 1);
    const Film *x = archive_find(&b, f->id);
    CHECK(x && strcmp(x->title, f->title) == 0 && strcmp(x->notes, f->notes) == 0);
    const Film *y = archive_find(&b, g->id);
    CHECK(y && y->rating == 7 && y->year == 0 && strcmp(y->watched_on, "2026-10-20") == 0);
    CHECK(strcmp(b.journal[0].text, "Second viewing \\n not a newline") == 0);
    CHECK(b.next_film_id == a.next_film_id);
    struct stat st;
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0077) == 0); /* private: 0600 */
    snprintf(text, sizeof text, "%s.tmp", path);
    CHECK(access(text, F_OK) != 0); /* no temporary file left behind */

    group("storage: backup of the previous version");
    add(&b, "Persona", "Ingmar Bergman", 1966, "Drama", FILM_WATCHED, 9);
    CHECK(storage_save(path, &b, err, sizeof err));
    snprintf(bak, sizeof bak, "%s.bak", path);
    archive_free(&a);
    archive_init(&a);
    CHECK(storage_load(bak, &a, &rep) == STORAGE_OK && a.count == 3);
    archive_free(&a);
    archive_free(&b);

    group("storage: malformed records are skipped, not fatal");
    tmp_path(path, sizeof path, "damaged.db");
    write_file(path,
               "# comment\n"
               "CINEPHILIA 1\n"
               "F\t1\tw\t2000\t9\t2026-01-01\t2026-01-02\tGood One\tDir\tDrama\tnotes\n"
               "F\t2\tw\t2000\t11\t\t\tBad rating\t\t\t\n"          /* rating 11 */
               "F\tx\tw\t2000\t5\t\t\tBad id\t\t\t\n"               /* id */
               "F\t1\tw\t2000\t5\t\t\tDuplicate id\t\t\t\n"         /* duplicate id */
               "F\t3\tq\t2000\t5\t\t\tBad status\t\t\t\n"           /* status */
               "F\t4\tl\t2000\t0\t2026-02-30\t\tBad date\t\t\t\n"   /* impossible date */
               "F\t5\tl\t1999\t0\t\t\t   \t\t\t\n"                  /* blank title */
               "garbage line\n"
               "J\t1\t1\t2026-01-02\tfine\n"
               "J\t2\t99\t2026-01-02\torphan\n"                    /* no such film */
               "J\t3\t1\tnot-a-date\tbad\n"
               "F\t6\tl\t2010\t0\t\t\tTruncated in the mid");      /* cut short, no newline */
    archive_init(&a);
    StorageStatus s = storage_load(path, &a, &rep);
    CHECK(s == STORAGE_RECOVERED);
    CHECK(a.count == 1 && a.journal_count == 1);
    CHECK(rep.skipped_lines == 10 && rep.orphan_entries == 1 && rep.first_bad_line == 4);
    archive_free(&a);

    group("storage: foreign or newer files are refused, never overwritten");
    tmp_path(path, sizeof path, "foreign.db");
    write_file(path, "Some unrelated notes\nthat are not an archive\n");
    archive_init(&a);
    add(&a, "Sentinel", "", 0, "", FILM_WATCHED, 0);
    CHECK(storage_load(path, &a, &rep) == STORAGE_UNSUPPORTED);
    CHECK(a.count == 1); /* untouched on refusal */
    tmp_path(path, sizeof path, "future.db");
    write_file(path, "CINEPHILIA 9\nF\t1\tw\t2000\t9\t\t\tX\t\t\t\n");
    CHECK(storage_load(path, &a, &rep) == STORAGE_UNSUPPORTED && strstr(rep.message, "newer") != NULL);
    archive_free(&a);

    group("storage: failed writes leave the old archive intact");
    char dir[300];
    tmp_path(dir, sizeof dir, "locked");
    mkdir(dir, 0700);
    snprintf(path, sizeof path, "%s/archive.db", dir);
    archive_init(&a);
    add(&a, "Original", "", 2000, "", FILM_WATCHED, 5);
    CHECK(storage_save(path, &a, err, sizeof err));
    char before[4096];
    text_copy(before, sizeof before, read_file(path));
    chmod(dir, 0500); /* no new files may be created */
    add(&a, "Never saved", "", 2001, "", FILM_WATCHED, 5);
    bool saved = storage_save(path, &a, err, sizeof err);
    if (geteuid() == 0) {
        printf("    (running as root: permission test not meaningful, skipped)\n");
    } else {
        CHECK(!saved);
        CHECK(strstr(err, "Permission denied") != NULL);
        CHECK(strcmp(read_file(path), before) == 0);
    }
    chmod(dir, 0700);
    /* A failure that holds even for root: the temporary file's name is
     * taken by a folder, so the new archive cannot be written. */
    snprintf(path, sizeof path, "%s/archive.db", dir);
    char tmpdir[700];
    snprintf(tmpdir, sizeof tmpdir, "%s.tmp", path);
    text_copy(before, sizeof before, read_file(path));
    archive_init(&b);
    storage_load(path, &b, &rep);
    size_t count_before = b.count;
    archive_free(&b);
    add(&a, "Also never saved", "", 2002, "", FILM_WATCHED, 5);
    CHECK(mkdir(tmpdir, 0700) == 0);
    CHECK(!storage_save(path, &a, err, sizeof err));
    CHECK(strstr(err, "cannot write") != NULL);
    CHECK(strcmp(read_file(path), before) == 0);
    archive_init(&b);
    CHECK(storage_load(path, &b, &rep) == STORAGE_OK && b.count == count_before);
    archive_free(&b);
    rmdir(tmpdir);
    /* A path whose parent is a regular file cannot be created. */
    tmp_path(path, sizeof path, "empty.db/inside.db");
    CHECK(!storage_save(path, &a, err, sizeof err));
    archive_free(&a);
}

static void test_discover(void)
{
    Rng r;
    rng_seed(&r, 1234);
    Archive a;
    archive_init(&a);
    size_t pool;

    group("discover: random picks with zero, one and many films");
    CHECK(discover_random(&a, -1, &r, &pool) == NULL && pool == 0);
    Film *only = add(&a, "Only", "Dir", 2000, "Drama", FILM_WATCHED, 8);
    for (int i = 0; i < 20; i++)
        CHECK(discover_random(&a, -1, &r, &pool) == only && pool == 1);
    CHECK(discover_random(&a, FILM_WATCHLIST, &r, &pool) == NULL);
    for (int i = 0; i < 9; i++) {
        char t[24];
        snprintf(t, sizeof t, "Film %d", i);
        add(&a, t, "Dir", 2000, "Drama", FILM_WATCHED, 5);
    }
    int seen[11] = {0};
    for (int i = 0; i < 10000; i++) {
        const Film *f = discover_random(&a, -1, &r, &pool);
        seen[f->id]++;
    }
    bool uniform = true;
    for (int id = 1; id <= 10; id++)
        if (seen[id] < 800 || seen[id] > 1200)
            uniform = false;
    CHECK(uniform && pool == 10);
    for (int i = 0; i < 1000; i++)
        CHECK(rng_below(&r, 3) < 3);
    archive_free(&a);

    group("discover: recommendations are ranked and explained");
    archive_init(&a);
    add(&a, "In the Mood for Love", "Wong Kar-wai", 2000, "Romance", FILM_WATCHED, 10);
    add(&a, "Chungking Express", "Wong Kar-wai", 1994, "Romance", FILM_WATCHED, 9);
    add(&a, "Some Comedy", "Someone", 2010, "Comedy", FILM_WATCHED, 3);
    add(&a, "Another Comedy", "Someone Else", 2012, "Comedy", FILM_WATCHED, 4);
    add(&a, "Dull Comedy", "Nobody", 2015, "Comedy", FILM_WATCHLIST, 0);
    add(&a, "Fallen Angels", "Wong Kar-wai", 1995, "Crime", FILM_WATCHLIST, 0);
    add(&a, "Unknown Horror", "New Person", 2020, "Horror", FILM_WATCHLIST, 0);
    Recommendation recs[8];
    bool rewatch = true;
    size_t n = discover_recommend(&a, recs, 8, &rewatch);
    CHECK(n == 3 && !rewatch);
    CHECK(n > 0 && strcmp(recs[0].film->title, "Fallen Angels") == 0);
    CHECK(n > 0 && strstr(recs[0].reason, "Wong Kar-wai") != NULL);
    CHECK(n > 2 && strcmp(recs[2].film->title, "Dull Comedy") == 0);
    CHECK(n > 0 && recs[0].score >= recs[1].score && recs[1].score >= recs[2].score);
    archive_free(&a);

    archive_init(&a);
    add(&a, "Loved", "A", 2000, "Drama", FILM_WATCHED, 9);
    add(&a, "Liked", "B", 2000, "Drama", FILM_WATCHED, 6);
    n = discover_recommend(&a, recs, 8, &rewatch);
    CHECK(rewatch && n == 1 && strcmp(recs[0].film->title, "Loved") == 0);
    archive_free(&a);
    archive_init(&a);
    CHECK(discover_recommend(&a, recs, 8, &rewatch) == 0);

    group("discover: double features");
    DoubleFeature df;
    CHECK(!discover_double_feature(&a, &r, &df));
    add(&a, "Solo", "X", 2000, "Drama", FILM_WATCHLIST, 0);
    CHECK(!discover_double_feature(&a, &r, &df));
    add(&a, "Stalker", "Andrei Tarkovsky", 1979, "Science Fiction", FILM_WATCHLIST, 0);
    add(&a, "Mirror", "Andrei Tarkovsky", 1975, "Drama", FILM_WATCHLIST, 0);
    add(&a, "Playtime", "Jacques Tati", 1967, "Comedy", FILM_WATCHLIST, 0);
    for (int i = 0; i < 20; i++) {
        CHECK(discover_double_feature(&a, &r, &df));
        CHECK(strcmp(df.first->director, "Andrei Tarkovsky") == 0 &&
              strcmp(df.second->director, "Andrei Tarkovsky") == 0);
        CHECK(df.first->year <= df.second->year && df.from_watchlist);
        CHECK(strstr(df.reason, "Tarkovsky") != NULL);
    }
    archive_free(&a);
}

static void test_stats(void)
{
    group("statistics: empty and populated archives");
    Archive a;
    archive_init(&a);
    Stats s;
    CHECK(stats_compute(&a, "2026-10-22", &s));
    CHECK(s.total == 0 && s.rated == 0 && s.top_count == 0 && s.genre_count == 0 && s.viewings == 0);
    stats_free(&s);

    Film *f1 = add(&a, "A", "Wong Kar-wai", 2000, "Romance", FILM_WATCHED, 9);
    add(&a, "B", "Wong Kar-wai", 1994, "romance", FILM_WATCHED, 7);
    add(&a, "C", "Tati", 1967, "Comedy", FILM_WATCHLIST, 0);
    add(&a, "D", "", 0, "", FILM_WATCHED, 0);
    film_record_viewing(&a, f1, "2026-10-01", 0, "");
    film_record_viewing(&a, f1, "2026-09-15", 0, "");
    film_record_viewing(&a, f1, "2024-01-01", 0, ""); /* outside the 12 months */
    Film *d = archive_find(&a, 4);
    text_copy(d->watched_on, sizeof d->watched_on, "2025-11-05"); /* dated, no entry */
    CHECK(stats_compute(&a, "2026-10-22", &s));
    CHECK(s.total == 4 && s.watched == 3 && s.watchlist == 1 && s.rated == 2);
    CHECK(s.avg_rating > 7.99 && s.avg_rating < 8.01);
    CHECK(s.top_count == 2 && s.top[0] == f1);
    CHECK(s.genre_count == 2 && s.genres[0].count == 2); /* "Romance" and "romance" are one */
    CHECK(s.no_genre == 1);
    CHECK(s.director_count == 2 && strcmp(s.directors[0].name, "Wong Kar-wai") == 0);
    CHECK(s.viewings == 4);
    CHECK(s.months[11] == 1 && s.months[10] == 1 && s.months[0] == 1); /* Oct, Sep, Nov 2025 */
    CHECK(s.month_number[11] == 10 && s.month_year[11] == 2026 && s.month_number[0] == 11);
    CHECK(s.decades[(2000 - STATS_DECADE_BASE) / 10] == 1 && s.unknown_year == 1);
    CHECK(strcmp(s.first_viewing, "2024-01-01") == 0 && strcmp(s.last_viewing, "2026-10-01") == 0);
    CHECK(s.rating_counts[9] == 1 && s.rating_counts[7] == 1);
    stats_free(&s);
    archive_free(&a);
}

static void test_settings(void)
{
    group("settings: defaults, persistence, malformed files");
    char path[600], err[256];
    Settings s;
    tmp_path(path, sizeof path, "prefs/settings.conf");
    CHECK(settings_load(path, &s)); /* missing file: defaults */
    CHECK(s.animation && s.sound && s.volume == SETTINGS_VOLUME_DEFAULT);
    s.animation = false;
    s.volume = 9;
    CHECK(settings_save(path, &s, err, sizeof err));
    Settings t;
    CHECK(settings_load(path, &t) && !t.animation && t.sound && t.volume == 9);
    write_file(path, "volume=99\nanimation=maybe\nsound=off\nnonsense\n=\n");
    CHECK(settings_load(path, &t));
    CHECK(t.volume == SETTINGS_VOLUME_DEFAULT && t.animation && !t.sound);
    settings_path_for("data/cinephilia.db", path, sizeof path);
    CHECK_STR(path, "data/settings.conf");
    settings_path_for("archive.db", path, sizeof path);
    CHECK_STR(path, "settings.conf");
}

static int count_lines(const char *path)
{
    char *s = read_file(path);
    int n = 0;
    for (; s && *s; s++)
        if (*s == '\n')
            n++;
    return n;
}

static void test_audio(void)
{
    group("audio: players are controlled, throttled and cleaned up");
    char sounds[300], player[300], log[300], cmd[1400];
    /* A folder with a space in its name, holding the real sound files. */
    tmp_path(sounds, sizeof sounds, "my sounds");
    mkdir(sounds, 0700);
    snprintf(cmd, sizeof cmd, "cp assets/audio/*.wav '%s'/ 2>/dev/null", sounds);
    if (system(cmd) != 0) {
        printf("    (assets/audio missing: run make sounds; audio tests skipped)\n");
        return;
    }
    tmp_path(log, sizeof log, "player.log");
    tmp_path(player, sizeof player, "fake-player.sh");
    snprintf(cmd, sizeof cmd, "#!/bin/sh\nprintf '%%s|%%s|%%s\\n' \"$1\" \"$2\" \"$3\" >> '%s'\nsleep 0.4\n", log);
    write_file(player, cmd);
    chmod(player, 0700);

    /* No player at all: everything is a silent no-op. */
    setenv("CINEPHILIA_AUDIO_PLAYER", "/nonexistent/afplay", 1);
    setenv("CINEPHILIA_AUDIO_DIR", sounds, 1);
    CHECK(audio_init(NULL) == AUDIO_NO_PLAYER);
    audio_play(SOUND_STARTUP);
    CHECK(audio_spawn_count() == 0 && !audio_busy());

    /* No sound files: also silent. */
    char empty[600];
    tmp_path(empty, sizeof empty, "no-sounds");
    mkdir(empty, 0700);
    setenv("CINEPHILIA_AUDIO_PLAYER", player, 1);
    setenv("CINEPHILIA_AUDIO_DIR", empty, 1);
    CHECK(audio_init(NULL) == AUDIO_NO_ASSETS);
    audio_play(SOUND_STARTUP);
    CHECK(audio_spawn_count() == 0);

    setenv("CINEPHILIA_AUDIO_DIR", sounds, 1);
    CHECK(audio_init(NULL) == AUDIO_READY && audio_available() == SOUND_COUNT);
    audio_set_enabled(true);
    audio_set_volume(4);
    audio_play(SOUND_FILED);
    CHECK(audio_spawn_count() == 1 && audio_busy());

    /* Fifty rapid navigation clicks start at most one player. */
    size_t before = audio_spawn_count();
    for (int i = 0; i < 50; i++)
        audio_play(SOUND_MOVE);
    CHECK(audio_spawn_count() - before == 1);
    /* Never more than four at once, however many sounds are asked for. */
    const Sound burst[] = {SOUND_CONFIRM, SOUND_REVEAL, SOUND_DELETE, SOUND_ERROR, SOUND_STOP, SOUND_REEL};
    for (size_t i = 0; i < 6; i++)
        audio_play(burst[i]);
    CHECK(audio_spawn_count() <= 4);

    /* Stopping terminates and reaps every player: no zombies, no orphans. */
    audio_stop_all();
    CHECK(!audio_busy());
    CHECK(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);

    /* Disabled means silent. */
    audio_set_enabled(false);
    before = audio_spawn_count();
    audio_play(SOUND_CONFIRM);
    CHECK(audio_spawn_count() == before);
    audio_set_enabled(true);

    /* The player receives volume and the full path, spaces intact. */
    audio_play(SOUND_ERROR);
    audio_wait(2000);
    CHECK(!audio_busy());
    char *s = read_file(log);
    char expect[700];
    snprintf(expect, sizeof expect, "-v|0.40|%s/error.wav", sounds);
    CHECK(s && strstr(s, expect) != NULL);
    CHECK(count_lines(log) >= 1);

    /* The signal-time cleanup terminates running players. */
    audio_play(SOUND_STOP);
    CHECK(audio_busy());
    audio_signal_cleanup();
    audio_wait(1000);
    CHECK(!audio_busy());
    unsetenv("CINEPHILIA_AUDIO_PLAYER");
    unsetenv("CINEPHILIA_AUDIO_DIR");
}

static void test_json(void)
{
    group("json: values, escapes, nesting, errors");
    JsonNode *j = json_parse(" {\"a\": [1, 2.5, -3e2], \"s\": \"caf\\u00e9 \\\"q\\\" \\ud83c\\udfac\\n\","
                             " \"t\": true, \"f\": false, \"n\": null, \"o\": {\"x\": {}}} ");
    CHECK(j && j->type == JSON_OBJECT && json_count(j) == 6);
    const JsonNode *a = json_get(j, "a");
    CHECK(json_count(a) == 3 && json_at(a, 1)->number == 2.5 && json_at(a, 2)->number == -300);
    CHECK(json_at(a, 3) == NULL);
    CHECK_STR(json_text(json_get(j, "s"), "?"), "caf\xC3\xA9 \"q\" \xF0\x9F\x8E\xAC\n");
    CHECK(json_get(j, "t")->boolean && !json_get(j, "f")->boolean);
    CHECK(json_get(j, "n")->type == JSON_NULL);
    CHECK(json_get(json_get(j, "o"), "x")->type == JSON_OBJECT);
    CHECK(json_get(j, "missing") == NULL && json_get(NULL, "a") == NULL);
    CHECK_STR(json_text(json_get(j, "t"), "fallback"), "fallback");
    json_free(j);

    const char *bad[] = {"", "{", "[1,]", "{\"a\" 1}", "{\"a\":1,}", "tru", "\"unterminated",
                         "[1] x", "\"bad \\q escape\"", "\"\\ud800 lone\"", "\"raw\ttab\"", "01x"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        JsonNode *b = json_parse(bad[i]);
        CHECK(b == NULL);
        json_free(b);
    }
    char deep[300];
    memset(deep, '[', 200);
    memset(deep + 200, ']', 99);
    deep[299] = '\0';
    CHECK(json_parse(deep) == NULL); /* too deep: refused, not a crash */
}

static void test_tmdb(void)
{
    group("tmdb: reading search and detail replies");
    long id = 0;
    CHECK(tmdb_parse_search("{\"page\":1,\"results\":[{\"id\":843,\"title\":\"In the Mood for Love\"},"
                            "{\"id\":1,\"title\":\"Other\"}],\"total_results\":2}", &id) == TMDB_OK);
    CHECK(id == 843);
    CHECK(tmdb_parse_search("{\"page\":1,\"results\":[],\"total_results\":0}", &id) == TMDB_NOT_FOUND);
    CHECK(tmdb_parse_search("{\"status_message\":\"Invalid API key\"}", &id) == TMDB_BAD_RESPONSE);
    CHECK(tmdb_parse_search("<html>Gateway timeout</html>", &id) == TMDB_BAD_RESPONSE);

    TmdbFilm f;
    CHECK(tmdb_parse_details(
              "{\"id\":843,\"title\":\"In the Mood for Love\",\"release_date\":\"2000-09-29\","
              "\"overview\":\"Hong Kong, 1962.\\nTwo neighbours\\u2026\","
              "\"genres\":[{\"id\":18,\"name\":\"Drama\"},{\"id\":10749,\"name\":\"Romance\"}],"
              "\"credits\":{\"cast\":[],\"crew\":[{\"job\":\"Producer\",\"name\":\"Someone\"},"
              "{\"job\":\"Director\",\"name\":\"Wong Kar-wai\"}]}}",
              &f) == TMDB_OK);
    CHECK_STR(f.title, "In the Mood for Love");
    CHECK(f.year == 2000);
    CHECK_STR(f.genre, "Drama");
    CHECK_STR(f.director, "Wong Kar-wai");
    CHECK(strchr(f.synopsis, '\n') == NULL); /* control characters removed */
    CHECK(strstr(f.synopsis, "Two neighbours\xE2\x80\xA6") != NULL);

    CHECK(tmdb_parse_details("{\"title\":\"Unreleased\",\"release_date\":\"\",\"genres\":[]}", &f) == TMDB_OK);
    CHECK(f.year == 0 && f.genre[0] == '\0' && f.director[0] == '\0');
    CHECK(tmdb_parse_details("{\"title\":\"Evil\\u001b[31m\"}", &f) == TMDB_OK);
    CHECK(strchr(f.title, '\033') == NULL); /* no terminal escapes from the network */
    CHECK(tmdb_parse_details("{\"id\":1}", &f) == TMDB_BAD_RESPONSE);
    CHECK(tmdb_parse_details("[]", &f) == TMDB_BAD_RESPONSE);

    group("tmdb: without a key nothing is requested");
    unsetenv("TMDB_API_KEY");
    CHECK(!tmdb_available());
    CHECK(tmdb_lookup("Stalker", 0, &f) == TMDB_NO_KEY || tmdb_lookup("Stalker", 0, &f) == TMDB_UNAVAILABLE);
    setenv("TMDB_API_KEY", "abc\"; url = \"http://evil", 1); /* not a key: refused */
    CHECK(!tmdb_available());
    unsetenv("TMDB_API_KEY");
}

int main(void)
{
    snprintf(g_tmp, sizeof g_tmp, "/tmp/cinephilia-test-%d", (int)getpid());
    if (mkdir(g_tmp, 0700) != 0) {
        perror(g_tmp);
        return 1;
    }
    printf("Cinephilia unit tests\n");
    test_text();
    test_wordmark();
    test_archive();
    test_sort_search();
    test_storage();
    test_discover();
    test_stats();
    test_settings();
    test_audio();
    test_json();
    test_tmdb();

    char cmd[600];
    snprintf(cmd, sizeof cmd, "chmod -R u+w '%s' && rm -rf '%s'", g_tmp, g_tmp);
    if (system(cmd) != 0)
        fprintf(stderr, "warning: could not remove %s\n", g_tmp);
    printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
