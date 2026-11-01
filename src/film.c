/*
 * film.c — archive (doubly linked list) and journal operations. See film.h.
 */
#include "film.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void archive_init(Archive *a)
{
    memset(a, 0, sizeof *a);
    a->next_film_id = 1;
    a->next_entry_id = 1;
}

void archive_free(Archive *a)
{
    Film *f = a->head;
    while (f) {
        Film *next = f->next;
        free(f);
        f = next;
    }
    free(a->journal);
    archive_init(a);
}

Film *film_new(void)
{
    Film *f = calloc(1, sizeof *f);
    if (f)
        f->status = FILM_WATCHLIST;
    return f;
}

bool film_validate(const Film *f, char *err, size_t errcap)
{
    const char *problem = NULL;
    if (f->title[0] == '\0')
        problem = "a film needs a title";
    else if (f->year != 0 && (f->year < FILM_YEAR_MIN || f->year > FILM_YEAR_MAX))
        problem = "the year is out of range";
    else if (f->rating != 0 && (f->rating < RATING_MIN || f->rating > RATING_MAX))
        problem = "the rating must be between 1 and 10";
    else if (f->status != FILM_WATCHED && f->status != FILM_WATCHLIST)
        problem = "unknown status";
    else if (f->added_on[0] && !date_valid(f->added_on))
        problem = "the date added is not a valid date";
    else if (f->watched_on[0] && !date_valid(f->watched_on))
        problem = "the date watched is not a valid date";
    if (problem && errcap > 0)
        text_copy(err, errcap, problem);
    return problem == NULL;
}

bool archive_append(Archive *a, Film *f)
{
    if (f->id == 0) {
        if (a->next_film_id == 0) /* every id has been used */
            return false;
        f->id = a->next_film_id++;
    } else if (f->id >= a->next_film_id) {
        a->next_film_id = f->id + 1; /* wraps to 0 only at UINT_MAX */
    }
    f->prev = a->tail;
    f->next = NULL;
    if (a->tail)
        a->tail->next = f;
    else
        a->head = f;
    a->tail = f;
    a->count++;
    return true;
}

void archive_unlink(Archive *a, Film *f)
{
    if (f->prev)
        f->prev->next = f->next;
    else
        a->head = f->next;
    if (f->next)
        f->next->prev = f->prev;
    else
        a->tail = f->prev;
    f->prev = NULL;
    f->next = NULL;
    a->count--;
}

Film *archive_find(const Archive *a, unsigned id)
{
    for (Film *f = a->head; f; f = f->next)
        if (f->id == id)
            return f;
    return NULL;
}

Film *archive_find_duplicate(const Archive *a, const char *title, int year,
                             const Film *exclude)
{
    for (Film *f = a->head; f; f = f->next) {
        if (f == exclude || text_casecmp(f->title, title) != 0)
            continue;
        if (year == 0 || f->year == 0 || year == f->year)
            return f;
    }
    return NULL;
}

size_t archive_count_status(const Archive *a, FilmStatus status)
{
    size_t n = 0;
    for (const Film *f = a->head; f; f = f->next)
        if (f->status == status)
            n++;
    return n;
}

static bool journal_reserve(Archive *a)
{
    if (a->journal_count < a->journal_cap)
        return true;
    size_t cap = a->journal_cap ? a->journal_cap * 2 : 16;
    if (cap > SIZE_MAX / sizeof(JournalEntry))
        return false;
    JournalEntry *grown = realloc(a->journal, cap * sizeof(JournalEntry));
    if (!grown)
        return false;
    a->journal = grown;
    a->journal_cap = cap;
    return true;
}

unsigned archive_add_entry(Archive *a, unsigned id, unsigned film_id,
                           const char *date, const char *text)
{
    if (!journal_reserve(a))
        return 0;
    if (id == 0) {
        if (a->next_entry_id == 0)
            return 0;
        id = a->next_entry_id++;
    } else if (id >= a->next_entry_id) {
        a->next_entry_id = id + 1;
    }
    JournalEntry *e = &a->journal[a->journal_count++];
    memset(e, 0, sizeof *e);
    e->id = id;
    e->film_id = film_id;
    text_copy(e->date, sizeof e->date, date ? date : "");
    text_copy(e->text, sizeof e->text, text ? text : "");
    return id;
}

JournalEntry *archive_entry_find(const Archive *a, unsigned id)
{
    for (size_t i = 0; i < a->journal_count; i++)
        if (a->journal[i].id == id)
            return &a->journal[i];
    return NULL;
}

bool archive_remove_entry(Archive *a, unsigned id)
{
    for (size_t i = 0; i < a->journal_count; i++) {
        if (a->journal[i].id == id) {
            memmove(&a->journal[i], &a->journal[i + 1],
                    (a->journal_count - i - 1) * sizeof(JournalEntry));
            a->journal_count--;
            return true;
        }
    }
    return false;
}

size_t archive_entry_count(const Archive *a, unsigned film_id)
{
    size_t n = 0;
    for (size_t i = 0; i < a->journal_count; i++)
        if (a->journal[i].film_id == film_id)
            n++;
    return n;
}

unsigned film_record_viewing(Archive *a, Film *f, const char *date, int rating,
                             const char *note)
{
    unsigned id = archive_add_entry(a, 0, f->id, date, note);
    if (id == 0)
        return 0;
    f->status = FILM_WATCHED;
    if (f->watched_on[0] == '\0' || strcmp(date, f->watched_on) > 0)
        text_copy(f->watched_on, sizeof f->watched_on, date);
    if (rating >= RATING_MIN && rating <= RATING_MAX)
        f->rating = rating;
    return id;
}

void film_recompute_last_seen(const Archive *a, Film *f)
{
    const char *latest = "";
    for (size_t i = 0; i < a->journal_count; i++) {
        const JournalEntry *e = &a->journal[i];
        if (e->film_id == f->id && strcmp(e->date, latest) > 0)
            latest = e->date;
    }
    text_copy(f->watched_on, sizeof f->watched_on, latest);
}

DeletedFilm *archive_delete_film(Archive *a, Film *f)
{
    DeletedFilm *d = calloc(1, sizeof *d);
    size_t n = archive_entry_count(a, f->id);
    if (d && n > 0) {
        d->entries = malloc(n * sizeof(JournalEntry));
        if (!d->entries) {
            free(d);
            d = NULL;
        }
    }

    /* Compact the journal, moving this film's entries into the record. */
    size_t kept = 0;
    size_t moved = 0;
    for (size_t i = 0; i < a->journal_count; i++) {
        if (a->journal[i].film_id == f->id) {
            if (d)
                d->entries[moved++] = a->journal[i];
        } else {
            a->journal[kept++] = a->journal[i];
        }
    }
    a->journal_count = kept;

    archive_unlink(a, f);
    if (!d) {
        free(f);
        return NULL;
    }
    d->film = f;
    d->entry_count = moved;
    return d;
}

bool archive_restore(Archive *a, DeletedFilm *d)
{
    bool ok = true;
    Film *f = d->film;
    if (archive_find(a, f->id)) /* never expected: ids are not reused */
        f->id = 0;
    if (!archive_append(a, f)) {
        free(f);
        ok = false;
    } else {
        for (size_t i = 0; i < d->entry_count; i++) {
            const JournalEntry *e = &d->entries[i];
            if (archive_add_entry(a, e->id, f->id, e->date, e->text) == 0)
                ok = false;
        }
    }
    free(d->entries);
    free(d);
    return ok;
}

void deleted_film_free(void *record)
{
    DeletedFilm *d = record;
    if (!d)
        return;
    free(d->film);
    free(d->entries);
    free(d);
}

void film_rating_label(const Film *f, char *out, size_t cap)
{
    if (f->status == FILM_WATCHLIST)
        snprintf(out, cap, "to see");
    else if (f->rating > 0)
        snprintf(out, cap, "%d/10", f->rating);
    else
        snprintf(out, cap, "seen");
}
