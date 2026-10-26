#ifndef FILM_H
#define FILM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Film {
    int    id;
    char   title[256];
    char   director[256];
    int    year;
    float  rating;
    char   genre[64];
    char   notes[512];
    int    watched;          /* 0 = watchlist, 1 = watched */
    char   date_watched[16]; /* YYYY-MM-DD or empty */
    char   journal[512];     /* personal journal entry */
    struct Film *next;
} Film;

/* Archive state */
typedef struct {
    Film *head;
    int   next_id;
    int   count;
    int   modified;  /* dirty flag for save optimization */
} Archive;

/* Initialize an empty archive */
void archive_init(Archive *ar);

/* Free all films in the archive */
void archive_free(Archive *ar);

/* Add a film to the end of the list. Returns the new film or NULL on failure. */
Film *archive_add(Archive *ar, const Film *film);

/* Find a film by ID. Returns NULL if not found. */
Film *archive_find_by_id(Archive *ar, int id);

/* Delete a film by ID. Returns 1 on success, 0 if not found. */
int archive_delete(Archive *ar, int id);

/* Count films in archive */
int archive_count(Archive *ar);

/* Count only watched films */
int archive_count_watched(Archive *ar);

/* Count only watchlist (unwatched) films */
int archive_count_watchlist(Archive *ar);

/* Get film at index (0-based). Returns NULL if out of range. */
Film *archive_get_at(Archive *ar, int index);

#endif /* FILM_H */
