/*
 * tally.h — a small frequency table: key -> (count, rating sum).
 *
 * Keys are matched case-insensitively ("drama" and "Drama" are one genre)
 * and are borrowed, not copied: they must outlive the tally. It is an
 * unsorted array with linear lookup, which is the simplest structure that is
 * fast enough for the few dozen genres or directors a personal archive has.
 */
#ifndef CINEPHILIA_DSA_TALLY_H
#define CINEPHILIA_DSA_TALLY_H

#include <stddef.h>

typedef struct {
    const char *key;
    int count;         /* how many times the key was added */
    int rated;         /* how many of those carried a rating */
    double rating_sum; /* sum of those ratings */
} TallyEntry;

typedef struct {
    TallyEntry *items;
    size_t count;
    size_t cap;
} Tally;

void tally_init(Tally *t);
void tally_free(Tally *t);
TallyEntry *tally_find(const Tally *t, const char *key);
/* Counts one occurrence of key; rating > 0 also adds to the rating sum.
 * Returns NULL only on allocation failure. */
TallyEntry *tally_add(Tally *t, const char *key, int rating);
/* Orders entries by count (descending), then key (A-Z). */
void tally_sort_by_count(Tally *t);

#endif
