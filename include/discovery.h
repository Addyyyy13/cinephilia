#ifndef DISCOVERY_H
#define DISCOVERY_H

#include "film.h"

/* Pick a random film from the archive */
Film *discovery_random_pick(Archive *ar);

/* Pick a random film from the watchlist only */
Film *discovery_random_watchlist(Archive *ar);

/* Pick two films for a double feature (tries to match genre) */
void discovery_double_feature(Archive *ar, Film **first, Film **second);

/* Recommend films based on a reference film (same genre/director/decade) */
void discovery_recommend(Archive *ar, Film *reference, Film **results, int *count, int max);

/* Interactive discovery menu */
void discovery_menu(Archive *ar);

#endif /* DISCOVERY_H */
