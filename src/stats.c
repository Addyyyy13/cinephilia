/*
 * stats.c — statistics over the archive. See stats.h.
 */
#include "stats.h"

#include <stdlib.h>
#include <string.h>

#include "dsa/tally.h"
#include "text.h"

static int by_rating(const void *pa, const void *pb)
{
    const Film *a = *(const Film *const *)pa;
    const Film *b = *(const Film *const *)pb;
    if (a->rating != b->rating)
        return b->rating - a->rating;
    int c = strcmp(b->watched_on, a->watched_on); /* most recently seen first */
    if (c != 0)
        return c;
    return text_casecmp(a->title, b->title);
}

static bool copy_tally(const Tally *t, NamedStat **out, size_t *count)
{
    *out = NULL;
    *count = 0;
    if (t->count == 0)
        return true;
    NamedStat *arr = calloc(t->count, sizeof *arr);
    if (!arr)
        return false;
    for (size_t i = 0; i < t->count; i++) {
        text_copy(arr[i].name, sizeof arr[i].name, t->items[i].key);
        arr[i].count = t->items[i].count;
        arr[i].rated = t->items[i].rated;
        arr[i].avg = t->items[i].rated ? t->items[i].rating_sum / t->items[i].rated : 0.0;
    }
    *out = arr;
    *count = t->count;
    return true;
}

static void count_viewing(Stats *s, const char *date, int ty, int tm)
{
    if (!date_valid(date))
        return;
    s->viewings++;
    if (s->first_viewing[0] == '\0' || strcmp(date, s->first_viewing) < 0)
        text_copy(s->first_viewing, sizeof s->first_viewing, date);
    if (strcmp(date, s->last_viewing) > 0)
        text_copy(s->last_viewing, sizeof s->last_viewing, date);
    int diff = (ty * 12 + tm) - (date_year(date) * 12 + date_month(date));
    if (diff >= 0 && diff < STATS_MONTHS)
        s->months[STATS_MONTHS - 1 - diff]++;
}

bool stats_compute(const Archive *a, const char *today, Stats *s)
{
    memset(s, 0, sizeof *s);
    Tally genres;
    Tally directors;
    tally_init(&genres);
    tally_init(&directors);
    const Film **rated = NULL;
    bool ok = false;
    double sum = 0;

    if (a->count > 0) {
        rated = malloc(a->count * sizeof *rated);
        if (!rated)
            goto done;
    }

    for (const Film *f = a->head; f; f = f->next) {
        s->total++;
        bool is_rated = f->status == FILM_WATCHED && f->rating > 0;
        if (f->status == FILM_WATCHED)
            s->watched++;
        else
            s->watchlist++;
        if (is_rated) {
            rated[s->rated++] = f;
            sum += f->rating;
            s->rating_counts[f->rating]++;
        }
        int r = is_rated ? f->rating : 0;
        if (f->genre[0]) {
            if (!tally_add(&genres, f->genre, r))
                goto done;
        } else {
            s->no_genre++;
        }
        if (f->director[0] && !tally_add(&directors, f->director, r))
            goto done;
        if (f->year >= STATS_DECADE_BASE && f->year <= FILM_YEAR_MAX)
            s->decades[(f->year - STATS_DECADE_BASE) / 10]++;
        else
            s->unknown_year++;
    }
    s->avg_rating = s->rated ? sum / (double)s->rated : 0.0;

    if (s->rated > 0) {
        qsort(rated, s->rated, sizeof *rated, by_rating);
        s->top_count = s->rated < STATS_TOP ? s->rated : STATS_TOP;
        for (size_t i = 0; i < s->top_count; i++)
            s->top[i] = rated[i];
    }

    /* Viewings by month. A watched film with a date but no journal entry
     * (e.g. imported, or added with only a "last seen" date) counts once. */
    int ty = date_year(today);
    int tm = date_month(today);
    for (int i = 0; i < STATS_MONTHS; i++) {
        int back = STATS_MONTHS - 1 - i;
        int idx = ty * 12 + (tm - 1) - back;
        s->month_year[i] = idx / 12;
        s->month_number[i] = idx % 12 + 1;
    }
    for (size_t i = 0; i < a->journal_count; i++)
        count_viewing(s, a->journal[i].date, ty, tm);
    for (const Film *f = a->head; f; f = f->next)
        if (f->status == FILM_WATCHED && f->watched_on[0] && archive_entry_count(a, f->id) == 0)
            count_viewing(s, f->watched_on, ty, tm);

    tally_sort_by_count(&genres);
    tally_sort_by_count(&directors);
    if (!copy_tally(&genres, &s->genres, &s->genre_count) ||
        !copy_tally(&directors, &s->directors, &s->director_count))
        goto done;
    ok = true;

done:
    free(rated);
    tally_free(&genres);
    tally_free(&directors);
    if (!ok)
        stats_free(s);
    return ok;
}

void stats_free(Stats *s)
{
    free(s->genres);
    free(s->directors);
    s->genres = NULL;
    s->directors = NULL;
    s->genre_count = 0;
    s->director_count = 0;
}
