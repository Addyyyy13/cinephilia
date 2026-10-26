#ifndef STATS_H
#define STATS_H

#include "film.h"

/* Genre distribution entry */
typedef struct {
    char genre[64];
    int  count;
} GenreCount;

/* Director entry for stats */
typedef struct {
    char director[256];
    int  count;
    float avg_rating;
} DirectorStat;

/* Compute and display all statistics */
void stats_display(Archive *ar);

/* Individual stat computations */
float stats_average_rating(Archive *ar);
void  stats_genre_distribution(Archive *ar, GenreCount *genres, int *count, int max);
void  stats_top_rated(Archive *ar, Film **top, int *count, int max);
void  stats_director_breakdown(Archive *ar, DirectorStat *dirs, int *count, int max);

#endif /* STATS_H */
