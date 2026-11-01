/*
 * tmdb.h — optional film details from The Movie Database (TMDB).
 *
 * Cinephilia works fully offline; this is used only when the environment
 * variable TMDB_API_KEY holds a TMDB API key. Requests are made by the
 * system's /usr/bin/curl (present on every Mac), with the key passed on
 * curl's standard input so it never appears in the process list. Replies
 * are parsed with json.h.
 *
 * Attribution required by TMDB wherever its data is shown:
 *   "This product uses the TMDB API but is not endorsed or certified by TMDB."
 */
#ifndef CINEPHILIA_TMDB_H
#define CINEPHILIA_TMDB_H

#include <stdbool.h>
#include <stddef.h>

#define TMDB_NOTICE "This product uses the TMDB API but is not endorsed or certified by TMDB."

typedef enum {
    TMDB_OK,
    TMDB_UNAVAILABLE,  /* /usr/bin/curl was not found */
    TMDB_NO_KEY,       /* TMDB_API_KEY is not set, or is not a valid key */
    TMDB_BAD_KEY,      /* TMDB rejected the key (HTTP 401) */
    TMDB_NOT_FOUND,    /* no film matched */
    TMDB_RATE_LIMITED, /* HTTP 429 */
    TMDB_NETWORK_ERROR,
    TMDB_BAD_RESPONSE
} TmdbResult;

typedef struct {
    char title[160];
    int year;           /* 0 if unknown */
    char director[96];  /* first credited director, or "" */
    char genre[48];     /* first listed genre, or "" */
    char synopsis[1200];
} TmdbFilm;

/* True when a key is configured and curl is present. Makes no request. */
bool tmdb_available(void);
/* Searches TMDB for the title (and year, if non-zero) and fills `out`
 * with the best match. Blocks for at most about ten seconds per request. */
TmdbResult tmdb_lookup(const char *title, int year, TmdbFilm *out);
const char *tmdb_result_text(TmdbResult r);

/* The two reply parsers, exposed so they can be tested without a network. */
TmdbResult tmdb_parse_search(const char *json, long *film_id);
TmdbResult tmdb_parse_details(const char *json, TmdbFilm *out);

#endif
