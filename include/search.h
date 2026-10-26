#ifndef SEARCH_H
#define SEARCH_H

#include "film.h"

/* Search results: a simple array of film pointers */
typedef struct {
    Film **films;
    int    count;
    int    capacity;
} SearchResults;

void search_results_init(SearchResults *sr);
void search_results_free(SearchResults *sr);
void search_results_add(SearchResults *sr, Film *film);

/* Search by title (case-insensitive substring match) */
void search_by_title(Archive *ar, const char *query, SearchResults *out);

/* Search by director (case-insensitive substring match) */
void search_by_director(Archive *ar, const char *query, SearchResults *out);

/* Filter by genre (case-insensitive exact match) */
void search_by_genre(Archive *ar, const char *genre, SearchResults *out);

/* Filter by minimum rating */
void search_by_min_rating(Archive *ar, float min_rating, SearchResults *out);

/* Filter by year range */
void search_by_year_range(Archive *ar, int min_year, int max_year, SearchResults *out);

/* Sort films array by rating descending */
void search_sort_by_rating(SearchResults *sr);

/* Sort films array by year */
void search_sort_by_year(SearchResults *sr);

/* Sort films array by title alphabetically */
void search_sort_by_title(SearchResults *sr);

/* Interactive search menu */
void search_menu(Archive *ar);

#endif /* SEARCH_H */
