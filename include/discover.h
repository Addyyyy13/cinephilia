/*
 * discover.h — random picks, recommendations and double features.
 *
 * Nothing here involves a network service or machine learning. Every
 * suggestion is computed from the films and ratings in the local archive,
 * and each one carries a plain-language reason saying why it was chosen.
 */
#ifndef CINEPHILIA_DISCOVER_H
#define CINEPHILIA_DISCOVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "film.h"

/* Small, seedable pseudo-random generator (xorshift64*), so tests can be
 * deterministic (CINEPHILIA_SEED) and picks are not biased by modulo. */
typedef struct {
    uint64_t state;
} Rng;

void rng_seed(Rng *r, uint64_t seed);
uint32_t rng_below(Rng *r, uint32_t n); /* uniform in [0, n); 0 when n is 0 */

/* A uniformly random film with the given status (-1 = any status).
 * *pool receives how many films were eligible. NULL when there are none. */
const Film *discover_random(const Archive *a, int status, Rng *r, size_t *pool);

typedef struct {
    const Film *film;
    double score;
    char reason[200];
} Recommendation;

/* Ranks watchlist films by how highly you have rated films sharing their
 * genre and director (averages are shrunk toward your overall mean so one
 * film does not dominate). With an empty watchlist it suggests favourites
 * (rated 8+) to revisit, least recently seen first, and sets *rewatch.
 * Returns the number of recommendations written (at most cap). */
size_t discover_recommend(const Archive *a, Recommendation *out, size_t cap, bool *rewatch);

typedef struct {
    const Film *first;
    const Film *second;
    bool from_watchlist;
    char reason[200];
} DoubleFeature;

/* Pairs two films (from the watchlist when it holds at least two, otherwise
 * from the whole archive) that share a director, a genre or a decade,
 * choosing randomly among the best-matched pairs. False if fewer than two
 * films are available. */
bool discover_double_feature(const Archive *a, Rng *r, DoubleFeature *out);

#endif
