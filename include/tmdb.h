/*
 * tmdb.h — the planned interface for optional metadata from The Movie
 * Database (TMDB). NOT IMPLEMENTED in this version.
 *
 * Cinephilia works entirely offline and never needs an account or API key.
 * This header fixes the shape of a future, opt-in lookup so it can be added
 * without touching the rest of the code. The current implementation
 * (src/tmdb.c) performs no network access: it reports TMDB_UNAVAILABLE and
 * the interface does not offer the feature. See README.md, "TMDB".
 *
 * Intended design for whoever implements it:
 *   - credentials from the TMDB_API_KEY environment variable only (never
 *     stored in source code or in the archive);
 *   - HTTPS through the system libcurl (present on every Mac) behind this
 *     header, so nothing else depends on networking;
 *   - responses parsed defensively, with bounded copies into the fields
 *     below; failures, empty results and HTTP 429 (rate limit) reported as
 *     distinct results, never as success;
 *   - TMDB's attribution requirement shown wherever its data appears:
 *     "This product uses the TMDB API but is not endorsed or certified by
 *     TMDB." and use limited to non-commercial purposes per TMDB's terms.
 */
#ifndef CINEPHILIA_TMDB_H
#define CINEPHILIA_TMDB_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    TMDB_OK,
    TMDB_UNAVAILABLE,  /* not built with TMDB support (always, in 1.0) */
    TMDB_NO_KEY,       /* TMDB_API_KEY is not set */
    TMDB_NOT_FOUND,
    TMDB_RATE_LIMITED,
    TMDB_NETWORK_ERROR,
    TMDB_BAD_RESPONSE
} TmdbResult;

typedef struct {
    char title[160];
    int year;          /* 0 if unknown */
    char genre[48];    /* first listed genre */
    char synopsis[1200];
} TmdbFilm;

/* True only when lookups are compiled in and a key is configured. */
bool tmdb_available(void);
/* Looks up a film by title (and year, if non-zero). */
TmdbResult tmdb_lookup(const char *title, int year, TmdbFilm *out);
const char *tmdb_result_text(TmdbResult r);

#endif
