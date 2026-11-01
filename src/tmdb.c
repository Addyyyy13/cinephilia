/*
 * tmdb.c — placeholder for the TMDB interface in tmdb.h.
 *
 * Deliberately performs no network access and never claims success; the
 * application does not offer the feature while tmdb_available() is false.
 */
#include "tmdb.h"

#include <string.h>

bool tmdb_available(void)
{
    return false;
}

TmdbResult tmdb_lookup(const char *title, int year, TmdbFilm *out)
{
    (void)title;
    (void)year;
    if (out)
        memset(out, 0, sizeof *out);
    return TMDB_UNAVAILABLE;
}

const char *tmdb_result_text(TmdbResult r)
{
    switch (r) {
    case TMDB_OK: return "found";
    case TMDB_UNAVAILABLE: return "TMDB lookups are not available in this version";
    case TMDB_NO_KEY: return "TMDB_API_KEY is not set";
    case TMDB_NOT_FOUND: return "no matching film on TMDB";
    case TMDB_RATE_LIMITED: return "TMDB asked us to slow down; try again shortly";
    case TMDB_NETWORK_ERROR: return "could not reach TMDB";
    case TMDB_BAD_RESPONSE: return "TMDB sent a response that could not be read";
    }
    return "unknown";
}
