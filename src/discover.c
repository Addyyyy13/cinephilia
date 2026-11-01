/*
 * discover.c — transparent, local discovery features. See discover.h.
 */
#include "discover.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsa/tally.h"
#include "text.h"

/* How many "average" ratings a genre's or director's own average is blended
 * with. With 2, one 10/10 film moves a 7.0 mean only to 8.0, not to 10. */
#define PRIOR_WEIGHT 2.0
#define DOUBLE_FEATURE_POOL_MAX 2000

void rng_seed(Rng *r, uint64_t seed)
{
    r->state = seed ? seed : 0x9E3779B97F4A7C15ULL;
}

static uint64_t rng_next(Rng *r)
{
    uint64_t x = r->state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    r->state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

uint32_t rng_below(Rng *r, uint32_t n)
{
    if (n == 0)
        return 0;
    /* Rejection sampling removes the bias a plain modulo would introduce. */
    uint32_t limit = UINT32_MAX - UINT32_MAX % n;
    uint32_t v;
    do {
        v = (uint32_t)(rng_next(r) >> 32);
    } while (v >= limit);
    return v % n;
}

const Film *discover_random(const Archive *a, int status, Rng *r, size_t *pool)
{
    size_t n = 0;
    for (const Film *f = a->head; f; f = f->next)
        if (status < 0 || (int)f->status == status)
            n++;
    if (pool)
        *pool = n;
    if (n == 0)
        return NULL;
    size_t k = rng_below(r, n > UINT32_MAX ? UINT32_MAX : (uint32_t)n);
    for (const Film *f = a->head; f; f = f->next)
        if (status < 0 || (int)f->status == status) {
            if (k == 0)
                return f;
            k--;
        }
    return NULL;
}

static double shrunk_average(const TallyEntry *e, double mean)
{
    if (!e || e->rated == 0)
        return mean;
    return (e->rating_sum + mean * PRIOR_WEIGHT) / (e->rated + PRIOR_WEIGHT);
}

static double absd(double x)
{
    return x < 0 ? -x : x;
}

typedef struct {
    Recommendation rec;
    const char *tiebreak; /* earlier date first */
} Candidate;

static int by_score(const void *pa, const void *pb)
{
    const Candidate *a = pa;
    const Candidate *b = pb;
    if (a->rec.score != b->rec.score)
        return a->rec.score < b->rec.score ? 1 : -1;
    /* Empty dates sort after real ones. */
    bool ea = a->tiebreak[0] == '\0';
    bool eb = b->tiebreak[0] == '\0';
    if (ea != eb)
        return ea ? 1 : -1;
    int c = strcmp(a->tiebreak, b->tiebreak);
    if (c != 0)
        return c;
    return text_casecmp(a->rec.film->title, b->rec.film->title);
}

size_t discover_recommend(const Archive *a, Recommendation *out, size_t cap, bool *rewatch)
{
    Tally genres;
    Tally directors;
    tally_init(&genres);
    tally_init(&directors);
    double sum = 0;
    int rated = 0;
    size_t watchlist = 0;
    size_t favourites = 0;
    size_t written = 0;
    Candidate *cands = NULL;

    for (const Film *f = a->head; f; f = f->next) {
        if (f->status == FILM_WATCHLIST) {
            watchlist++;
            continue;
        }
        if (f->rating <= 0)
            continue;
        sum += f->rating;
        rated++;
        if (f->rating >= 8)
            favourites++;
        if (f->genre[0] && !tally_add(&genres, f->genre, f->rating))
            goto done;
        if (f->director[0] && !tally_add(&directors, f->director, f->rating))
            goto done;
    }

    *rewatch = watchlist == 0;
    size_t total = *rewatch ? favourites : watchlist;
    if (total == 0 || cap == 0)
        goto done;
    cands = calloc(total, sizeof *cands);
    if (!cands)
        goto done;

    double mean = rated ? sum / rated : 0.0;
    size_t k = 0;
    char pretty[24];

    for (const Film *f = a->head; f && k < total; f = f->next) {
        Candidate *c = &cands[k];
        if (*rewatch) {
            if (f->status != FILM_WATCHED || f->rating < 8)
                continue;
            c->rec.film = f;
            c->rec.score = f->rating;
            c->tiebreak = f->watched_on; /* longest unseen first */
            if (f->watched_on[0]) {
                date_pretty(f->watched_on, pretty, sizeof pretty);
                snprintf(c->rec.reason, sizeof c->rec.reason, "You gave it %d/10 \xC2\xB7 last seen %s",
                         f->rating, pretty);
            } else {
                snprintf(c->rec.reason, sizeof c->rec.reason, "You gave it %d/10", f->rating);
            }
            k++;
            continue;
        }

        if (f->status != FILM_WATCHLIST)
            continue;
        const TallyEntry *g = f->genre[0] ? tally_find(&genres, f->genre) : NULL;
        const TallyEntry *d = f->director[0] ? tally_find(&directors, f->director) : NULL;
        double gs = shrunk_average(g, mean);
        double ds = shrunk_average(d, mean);
        c->rec.film = f;
        c->rec.score = rated ? 0.55 * gs + 0.45 * ds : 0.0;
        c->tiebreak = f->added_on; /* waiting longest first */

        double gdev = g ? absd(gs - mean) : -1.0;
        double ddev = d ? absd(ds - mean) : -1.0;
        if (rated == 0) {
            if (f->added_on[0]) {
                date_pretty(f->added_on, pretty, sizeof pretty);
                snprintf(c->rec.reason, sizeof c->rec.reason,
                         "Waiting since %s \xC2\xB7 rate what you watch to sharpen these", pretty);
            } else {
                snprintf(c->rec.reason, sizeof c->rec.reason,
                         "Rate what you watch to sharpen these suggestions");
            }
        } else if (d && ddev >= gdev) {
            snprintf(c->rec.reason, sizeof c->rec.reason,
                     "%s \xE2\x80\x94 you average %.1f across %d of their films", f->director,
                     d->rating_sum / d->rated, d->rated);
        } else if (g) {
            snprintf(c->rec.reason, sizeof c->rec.reason,
                     "%s \xE2\x80\x94 you average %.1f across %d rated film%s", f->genre,
                     g->rating_sum / g->rated, g->rated, g->rated == 1 ? "" : "s");
        } else if (f->genre[0]) {
            snprintf(c->rec.reason, sizeof c->rec.reason,
                     "New territory: you haven't rated any %s yet", f->genre);
        } else {
            snprintf(c->rec.reason, sizeof c->rec.reason,
                     "Add a genre or director so it can be compared with your taste");
        }
        k++;
    }

    qsort(cands, k, sizeof *cands, by_score);
    for (size_t i = 0; i < k && written < cap; i++)
        out[written++] = cands[i].rec;

done:
    free(cands);
    tally_free(&genres);
    tally_free(&directors);
    return written;
}

static int pair_score(const Film *a, const Film *b)
{
    int s = 0;
    if (a->director[0] && text_casecmp(a->director, b->director) == 0)
        s += 4;
    if (a->genre[0] && text_casecmp(a->genre, b->genre) == 0)
        s += 2;
    if (a->year && b->year && a->year / 10 == b->year / 10)
        s += 1;
    return s;
}

static void describe_pair(const Film *a, const Film *b, char *out, size_t cap)
{
    bool same_dir = a->director[0] && text_casecmp(a->director, b->director) == 0;
    bool same_genre = a->genre[0] && text_casecmp(a->genre, b->genre) == 0;
    bool same_decade = a->year && b->year && a->year / 10 == b->year / 10;
    int decade = a->year / 10 * 10;

    if (same_dir && same_decade)
        snprintf(out, cap, "Both directed by %s, both from the %ds", a->director, decade);
    else if (same_dir)
        snprintf(out, cap, "Both directed by %s, years apart", a->director);
    else if (same_genre && same_decade)
        snprintf(out, cap, "Two %s films from the %ds", a->genre, decade);
    else if (same_genre)
        snprintf(out, cap, "Two %s films from different eras", a->genre);
    else if (same_decade)
        snprintf(out, cap, "Both from the %ds, in different registers", decade);
    else
        snprintf(out, cap, "A study in contrast: nothing in common but you");
}

bool discover_double_feature(const Archive *a, Rng *r, DoubleFeature *out)
{
    memset(out, 0, sizeof *out);
    size_t watchlist = archive_count_status(a, FILM_WATCHLIST);
    int status = watchlist >= 2 ? FILM_WATCHLIST : -1;

    size_t n = 0;
    for (const Film *f = a->head; f; f = f->next)
        if (status < 0 || (int)f->status == status)
            n++;
    if (n > DOUBLE_FEATURE_POOL_MAX)
        n = DOUBLE_FEATURE_POOL_MAX;
    if (n < 2)
        return false;

    const Film **pool = malloc(n * sizeof *pool);
    if (!pool)
        return false;
    size_t k = 0;
    for (const Film *f = a->head; f && k < n; f = f->next)
        if (status < 0 || (int)f->status == status)
            pool[k++] = f;

    /* Reservoir sampling: among all pairs with the best score, each is
     * equally likely to be chosen, without storing them. */
    int best = -1;
    uint32_t ties = 0;
    size_t pick_i = 0;
    size_t pick_j = 1;
    for (size_t i = 0; i < k; i++) {
        for (size_t j = i + 1; j < k; j++) {
            int s = pair_score(pool[i], pool[j]);
            if (s > best) {
                best = s;
                ties = 0;
            }
            if (s == best) {
                ties++;
                if (rng_below(r, ties) == 0) {
                    pick_i = i;
                    pick_j = j;
                }
            }
        }
    }

    /* Older film first: it reads like a programme. */
    const Film *x = pool[pick_i];
    const Film *y = pool[pick_j];
    if (y->year && (x->year == 0 || y->year < x->year)) {
        const Film *t = x;
        x = y;
        y = t;
    }
    out->first = x;
    out->second = y;
    out->from_watchlist = status == FILM_WATCHLIST;
    describe_pair(x, y, out->reason, sizeof out->reason);
    free(pool);
    return true;
}
