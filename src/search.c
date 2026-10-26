/*
 * search.c — Search & Filter Engine for CINEPHILIA
 *
 * Provides case-insensitive text search (title, director), exact-match
 * genre filtering, numeric range filters (rating, year), and qsort-based
 * result ordering.  An interactive submenu drives all operations.
 */

#include "search.h"
#include "ui.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ────────────────────────────────────────────────────────────────────── */
/*  Internal helpers                                                     */
/* ────────────────────────────────────────────────────────────────────── */

/*
 * strcasestr_match — case-insensitive substring search.
 *
 * Returns 1 if `needle` appears anywhere inside `haystack` (ignoring
 * case), 0 otherwise.  An empty needle always matches.
 */
static int strcasestr_match(const char *haystack, const char *needle)
{
    if (!haystack || !needle)
        return 0;

    if (*needle == '\0')
        return 1;

    size_t nlen = strlen(needle);

    for (size_t i = 0; haystack[i] != '\0'; i++) {
        if (strlen(haystack + i) < nlen)
            return 0;

        size_t j;
        for (j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) !=
                tolower((unsigned char)needle[j]))
                break;
        }

        if (j == nlen)
            return 1;
    }

    return 0;
}

/* ────────────────────────────────────────────────────────────────────── */
/*  SearchResults lifecycle                                              */
/* ────────────────────────────────────────────────────────────────────── */

/*
 * search_results_init — allocate a SearchResults with initial capacity 16.
 */
SearchResults search_results_init(void)
{
    SearchResults sr;
    sr.count    = 0;
    sr.capacity = 16;
    sr.films    = malloc(sizeof(Film *) * sr.capacity);

    if (!sr.films) {
        sr.capacity = 0;
    }

    return sr;
}

/*
 * search_results_free — release the pointer array.
 *
 * The Film structs themselves belong to the Archive and are NOT freed.
 */
void search_results_free(SearchResults *sr)
{
    if (!sr)
        return;

    free(sr->films);
    sr->films    = NULL;
    sr->count    = 0;
    sr->capacity = 0;
}

/*
 * search_results_add — append a Film pointer, doubling capacity as needed.
 */
void search_results_add(SearchResults *sr, Film *f)
{
    if (!sr || !f)
        return;

    if (sr->count >= sr->capacity) {
        size_t new_cap = (sr->capacity == 0) ? 16 : sr->capacity * 2;
        Film **tmp = realloc(sr->films, sizeof(Film *) * new_cap);

        if (!tmp) {
            fprintf(stderr, "  search: memory allocation failed\n");
            return;
        }

        sr->films    = tmp;
        sr->capacity = new_cap;
    }

    sr->films[sr->count++] = f;
}

/* ────────────────────────────────────────────────────────────────────── */
/*  Search / filter functions                                            */
/* ────────────────────────────────────────────────────────────────────── */

/*
 * search_by_title — collect every film whose title contains `query`
 *                   (case-insensitive substring match).
 */
void search_by_title(Archive *ar, const char *query, SearchResults *sr)
{
    if (!ar || !query || !sr)
        return;

    Film *cur = ar->head;

    while (cur) {
        if (strcasestr_match(cur->title, query))
            search_results_add(sr, cur);

        cur = cur->next;
    }
}

/*
 * search_by_director — collect every film whose director field contains
 *                      `query` (case-insensitive substring match).
 */
void search_by_director(Archive *ar, const char *query, SearchResults *sr)
{
    if (!ar || !query || !sr)
        return;

    Film *cur = ar->head;

    while (cur) {
        if (strcasestr_match(cur->director, query))
            search_results_add(sr, cur);

        cur = cur->next;
    }
}

/*
 * search_by_genre — collect every film whose genre matches `genre`
 *                   exactly (case-insensitive).
 */
void search_by_genre(Archive *ar, const char *genre, SearchResults *sr)
{
    if (!ar || !genre || !sr)
        return;

    Film *cur = ar->head;

    while (cur) {
        if (strcasecmp(cur->genre, genre) == 0)
            search_results_add(sr, cur);

        cur = cur->next;
    }
}

/*
 * search_by_min_rating — collect films rated at or above `min_rating`.
 */
void search_by_min_rating(Archive *ar, float min_rating, SearchResults *sr)
{
    if (!ar || !sr)
        return;

    Film *cur = ar->head;

    while (cur) {
        if (cur->rating >= min_rating)
            search_results_add(sr, cur);

        cur = cur->next;
    }
}

/*
 * search_by_year_range — collect films released within [min_year, max_year].
 */
void search_by_year_range(Archive *ar, int min_year, int max_year,
                          SearchResults *sr)
{
    if (!ar || !sr)
        return;

    Film *cur = ar->head;

    while (cur) {
        if (cur->year >= min_year && cur->year <= max_year)
            search_results_add(sr, cur);

        cur = cur->next;
    }
}

/* ────────────────────────────────────────────────────────────────────── */
/*  Sorting comparators (for qsort)                                      */
/* ────────────────────────────────────────────────────────────────────── */

/* Descending by rating */
static int cmp_rating_desc(const void *a, const void *b)
{
    const Film *fa = *(const Film **)a;
    const Film *fb = *(const Film **)b;

    if (fb->rating > fa->rating)  return  1;
    if (fb->rating < fa->rating)  return -1;
    return 0;
}

/* Ascending by year */
static int cmp_year_asc(const void *a, const void *b)
{
    const Film *fa = *(const Film **)a;
    const Film *fb = *(const Film **)b;

    return fa->year - fb->year;
}

/* Alphabetical ascending, case-insensitive */
static int cmp_title_asc(const void *a, const void *b)
{
    const Film *fa = *(const Film **)a;
    const Film *fb = *(const Film **)b;

    return strcasecmp(fa->title, fb->title);
}

/*
 * Public sort wrappers — each delegates to qsort with the right comparator.
 */
void search_sort_by_rating(SearchResults *sr)
{
    if (!sr || sr->count < 2)
        return;
    qsort(sr->films, sr->count, sizeof(Film *), cmp_rating_desc);
}

void search_sort_by_year(SearchResults *sr)
{
    if (!sr || sr->count < 2)
        return;
    qsort(sr->films, sr->count, sizeof(Film *), cmp_year_asc);
}

void search_sort_by_title(SearchResults *sr)
{
    if (!sr || sr->count < 2)
        return;
    qsort(sr->films, sr->count, sizeof(Film *), cmp_title_asc);
}

/* ────────────────────────────────────────────────────────────────────── */
/*  Helper: display search results or "not found" message                */
/* ────────────────────────────────────────────────────────────────────── */

static void display_results(SearchResults *sr)
{
    if (sr->count == 0) {
        printf("\n  %sNo films found.%s\n", MUTED, RESET);
    } else {
        printf("\n  %s%zu film(s) found:%s\n", GOLD, sr->count, RESET);

        for (size_t i = 0; i < sr->count; i++)
            ui_print_film_card(sr->films[i]);
    }
}

/* ────────────────────────────────────────────────────────────────────── */
/*  Interactive search menu                                              */
/* ────────────────────────────────────────────────────────────────────── */

void search_menu(Archive *ar)
{
    int choice;

    do {
        printf("\n");
        printf("  %sSearch & Filter%s\n", GOLD, RESET);
        printf("  %s──────────────────%s\n", MUTED, RESET);
        printf("  %s1 ─── Search by title%s\n",    BURGUNDY, RESET);
        printf("  %s2 ─── Search by director%s\n",  BURGUNDY, RESET);
        printf("  %s3 ─── Filter by genre%s\n",     BURGUNDY, RESET);
        printf("  %s4 ─── Filter by rating%s\n",    BURGUNDY, RESET);
        printf("  %s5 ─── Filter by year%s\n",      BURGUNDY, RESET);
        printf("  %s0 ─── Back%s\n",                MUTED,    RESET);
        printf("\n  %s> %s", GOLD, RESET);

        if (scanf("%d", &choice) != 1) {
            printf("\n  Please enter a number.\n");
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF) {}
            continue;
        }

        /* Flush the trailing newline so fgets-style reads work later */
        while (getchar() != '\n') {}

        char query[256];
        SearchResults sr;

        switch (choice) {
        /* ── Search by title ─────────────────────────────────────── */
        case 1:
            printf("\n  %sEnter title to search: %s", CREAM, RESET);
            if (fgets(query, sizeof(query), stdin) == NULL)
                break;
            query[strcspn(query, "\n")] = '\0';

            sr = search_results_init();
            search_by_title(ar, query, &sr);
            search_sort_by_title(&sr);
            display_results(&sr);
            ui_press_enter();
            search_results_free(&sr);
            break;

        /* ── Search by director ──────────────────────────────────── */
        case 2:
            printf("\n  %sEnter director to search: %s", CREAM, RESET);
            if (fgets(query, sizeof(query), stdin) == NULL)
                break;
            query[strcspn(query, "\n")] = '\0';

            sr = search_results_init();
            search_by_director(ar, query, &sr);
            search_sort_by_title(&sr);
            display_results(&sr);
            ui_press_enter();
            search_results_free(&sr);
            break;

        /* ── Filter by genre ─────────────────────────────────────── */
        case 3:
            printf("\n  %sEnter genre: %s", CREAM, RESET);
            if (fgets(query, sizeof(query), stdin) == NULL)
                break;
            query[strcspn(query, "\n")] = '\0';

            sr = search_results_init();
            search_by_genre(ar, query, &sr);
            search_sort_by_rating(&sr);
            display_results(&sr);
            ui_press_enter();
            search_results_free(&sr);
            break;

        /* ── Filter by minimum rating ────────────────────────────── */
        case 4: {
            float min_rating;
            printf("\n  %sMinimum rating (0-10): %s", CREAM, RESET);

            if (scanf("%f", &min_rating) != 1) {
                printf("\n  Invalid rating.\n");
                while (getchar() != '\n') {}
                break;
            }
            while (getchar() != '\n') {}

            sr = search_results_init();
            search_by_min_rating(ar, min_rating, &sr);
            search_sort_by_rating(&sr);
            display_results(&sr);
            ui_press_enter();
            search_results_free(&sr);
            break;
        }

        /* ── Filter by year range ────────────────────────────────── */
        case 5: {
            int min_year, max_year;
            printf("\n  %sStart year: %s", CREAM, RESET);

            if (scanf("%d", &min_year) != 1) {
                printf("\n  Invalid year.\n");
                while (getchar() != '\n') {}
                break;
            }
            while (getchar() != '\n') {}

            printf("  %sEnd year:   %s", CREAM, RESET);

            if (scanf("%d", &max_year) != 1) {
                printf("\n  Invalid year.\n");
                while (getchar() != '\n') {}
                break;
            }
            while (getchar() != '\n') {}

            sr = search_results_init();
            search_by_year_range(ar, min_year, max_year, &sr);
            search_sort_by_year(&sr);
            display_results(&sr);
            ui_press_enter();
            search_results_free(&sr);
            break;
        }

        case 0:
            break;

        default:
            printf("\n  %sPlease choose a valid option.%s\n", MUTED, RESET);
            break;
        }

    } while (choice != 0);
}
