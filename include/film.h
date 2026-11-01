/*
 * film.h — the film model, the archive and the journal.
 *
 * The archive is a doubly linked list of heap-allocated films. A linked
 * list suits this collection: films are added and deleted one at a time,
 * deletion from the middle is O(1) once a film is found, pointers to films
 * stay valid while others are added or removed, and the list can be merge
 * sorted in place (see search.h).
 *
 * Journal entries (dated viewings with notes) live in a separate dynamic
 * array and refer to films by id, so a film can be watched many times.
 */
#ifndef CINEPHILIA_FILM_H
#define CINEPHILIA_FILM_H

#include <stdbool.h>
#include <stddef.h>

#include "text.h"

#define FILM_TITLE_MAX 160
#define FILM_DIRECTOR_MAX 96
#define FILM_GENRE_MAX 48
#define FILM_NOTES_MAX 1200
#define FILM_YEAR_MIN 1878 /* Muybridge's "The Horse in Motion" */
#define FILM_YEAR_MAX 2200
#define RATING_MIN 1
#define RATING_MAX 10

typedef enum { FILM_WATCHLIST = 0, FILM_WATCHED = 1 } FilmStatus;

typedef struct Film {
    unsigned id;
    char title[FILM_TITLE_MAX];
    char director[FILM_DIRECTOR_MAX]; /* may be empty */
    char genre[FILM_GENRE_MAX];       /* may be empty */
    char notes[FILM_NOTES_MAX];       /* may be empty */
    int year;                         /* 0 = unknown */
    int rating;                       /* 0 = unrated, else 1..10 */
    FilmStatus status;
    char added_on[DATE_LEN];   /* when it entered the archive, may be empty */
    char watched_on[DATE_LEN]; /* most recent viewing, may be empty */
    struct Film *prev;
    struct Film *next;
} Film;

typedef struct {
    unsigned id;
    unsigned film_id;
    char date[DATE_LEN];
    char text[FILM_NOTES_MAX]; /* may be empty */
} JournalEntry;

typedef struct {
    Film *head;
    Film *tail;
    size_t count;
    unsigned next_film_id;
    JournalEntry *journal;
    size_t journal_count;
    size_t journal_cap;
    unsigned next_entry_id;
} Archive;

/* A deleted film kept for "undo", together with its journal entries. */
typedef struct {
    Film *film;
    JournalEntry *entries;
    size_t entry_count;
} DeletedFilm;

void archive_init(Archive *a);
void archive_free(Archive *a);

Film *film_new(void); /* zeroed, status = watchlist; NULL on failure */
bool film_validate(const Film *f, char *err, size_t errcap);

/* Takes ownership of f. Assigns a fresh id when f->id is 0. */
bool archive_append(Archive *a, Film *f);
void archive_unlink(Archive *a, Film *f); /* detaches without freeing */
Film *archive_find(const Archive *a, unsigned id);
/* A film with the same title (case-insensitive) and a compatible year
 * (equal, or either unknown), ignoring `exclude`. */
Film *archive_find_duplicate(const Archive *a, const char *title, int year,
                             const Film *exclude);
size_t archive_count_status(const Archive *a, FilmStatus status);

/* Journal. archive_add_entry assigns an id when id is 0 and returns the
 * entry's id, or 0 on allocation failure. Pointers into the journal are
 * invalidated by later additions. */
unsigned archive_add_entry(Archive *a, unsigned id, unsigned film_id,
                           const char *date, const char *text);
JournalEntry *archive_entry_find(const Archive *a, unsigned id);
bool archive_remove_entry(Archive *a, unsigned id);
size_t archive_entry_count(const Archive *a, unsigned film_id);

/* Records a viewing: marks the film watched, updates its "last seen" date,
 * sets the rating when rating is 1..10, and adds a journal entry.
 * Returns the new entry id, or 0 on allocation failure. */
unsigned film_record_viewing(Archive *a, Film *f, const char *date, int rating,
                             const char *note);
/* Sets watched_on to the latest journal date for the film ("" if none). */
void film_recompute_last_seen(const Archive *a, Film *f);

/* Removes a film and its journal entries. Returns an undo record, or NULL
 * if one could not be allocated (the film is deleted either way). */
DeletedFilm *archive_delete_film(Archive *a, Film *f);
/* Puts a deleted film back and frees the record. False if some journal
 * entries could not be restored. */
bool archive_restore(Archive *a, DeletedFilm *d);
void deleted_film_free(void *record);

/* "8/10", "seen" (watched, unrated) or "to see". */
void film_rating_label(const Film *f, char *out, size_t cap);

#endif
