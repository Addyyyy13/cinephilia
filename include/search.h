/*
 * search.h — sorting, searching and filtering the archive.
 *
 * Sorting uses merge sort directly on the linked list: it is stable, runs in
 * O(n log n) with O(log n) stack space, and needs no auxiliary array, which
 * makes it the natural choice for a linked list (quicksort needs random
 * access; insertion sort is O(n^2)).
 *
 * Searching is a linear scan with case-insensitive substring matching. A
 * substring query ("mood" finding "In the Mood for Love") cannot use a sort
 * order or binary search, and a personal archive is small enough that a
 * scan is instantaneous.
 */
#ifndef CINEPHILIA_SEARCH_H
#define CINEPHILIA_SEARCH_H

#include <stdbool.h>
#include <stddef.h>

#include "film.h"

typedef enum { SORT_TITLE, SORT_DIRECTOR, SORT_YEAR, SORT_RATING, SORT_ADDED } SortKey;

typedef struct {
    char query[FILM_TITLE_MAX]; /* matched against title and director */
    char genre[FILM_GENRE_MAX]; /* exact, case-insensitive */
    int min_rating;             /* 0 = any */
    int year_from;              /* 0 = no lower bound */
    int year_to;                /* 0 = no upper bound */
    int status;                 /* -1 = any, else FilmStatus */
} FilmFilter;

/* Sorts the list in place. Unknown years, missing directors and unrated
 * films always sort last; ties fall back to title, then id. Titles sort
 * without a leading "The", "A" or "An". */
void archive_sort(Archive *a, SortKey key, bool descending);
bool sort_default_descending(SortKey key);
const char *sort_key_name(SortKey key);
/* Title used for ordering: "The Godfather" -> "Godfather". */
const char *film_sort_title(const char *title);

void film_filter_init(FilmFilter *f);
bool film_filter_active(const FilmFilter *f);
bool film_matches(const Film *film, const FilmFilter *f);
/* e.g. "“mood” · Drama · rated 8+ · 1960–1979 · watched" */
void film_filter_describe(const FilmFilter *f, char *out, size_t cap);

/* Collects films matching f (NULL matches all) in list order into a newly
 * allocated array (NULL when none). False on allocation failure. */
bool archive_collect(const Archive *a, const FilmFilter *f, const Film ***out, size_t *count);

#endif
