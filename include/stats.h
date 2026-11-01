/*
 * stats.h — statistics computed from the stored archive and journal.
 */
#ifndef CINEPHILIA_STATS_H
#define CINEPHILIA_STATS_H

#include <stdbool.h>
#include <stddef.h>

#include "film.h"

#define STATS_TOP 5
#define STATS_MONTHS 12
#define STATS_DECADE_BASE 1870
#define STATS_DECADES ((FILM_YEAR_MAX - STATS_DECADE_BASE) / 10 + 1)

typedef struct {
    char name[FILM_DIRECTOR_MAX];
    int count;  /* films */
    int rated;  /* of which rated */
    double avg; /* average rating of the rated ones, 0 if none */
} NamedStat;

typedef struct {
    size_t total;
    size_t watched;
    size_t watchlist;
    size_t rated;
    size_t viewings; /* journal entries, plus watched films with a date but no entry */
    double avg_rating;

    const Film *top[STATS_TOP]; /* highest rated, best first */
    size_t top_count;

    NamedStat *genres; /* every genre, most films first */
    size_t genre_count;
    size_t no_genre;
    NamedStat *directors; /* every director, most films first */
    size_t director_count;

    int decades[STATS_DECADES]; /* index (year - 1870) / 10 */
    size_t unknown_year;
    int rating_counts[RATING_MAX + 1]; /* index = rating */

    /* Viewings per month for the 12 months ending with `today`'s month,
     * oldest first. */
    int months[STATS_MONTHS];
    int month_number[STATS_MONTHS]; /* 1..12 */
    int month_year[STATS_MONTHS];

    char first_viewing[DATE_LEN];
    char last_viewing[DATE_LEN];
} Stats;

/* today is "YYYY-MM-DD". False only on allocation failure. */
bool stats_compute(const Archive *a, const char *today, Stats *s);
void stats_free(Stats *s);

#endif
