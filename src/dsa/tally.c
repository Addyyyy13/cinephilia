/*
 * tally.c — frequency table. See dsa/tally.h.
 */
#include "dsa/tally.h"

#include <stdint.h>
#include <stdlib.h>

#include "text.h"

void tally_init(Tally *t)
{
    t->items = NULL;
    t->count = 0;
    t->cap = 0;
}

void tally_free(Tally *t)
{
    free(t->items);
    tally_init(t);
}

TallyEntry *tally_find(const Tally *t, const char *key)
{
    for (size_t i = 0; i < t->count; i++)
        if (text_casecmp(t->items[i].key, key) == 0)
            return &t->items[i];
    return NULL;
}

TallyEntry *tally_add(Tally *t, const char *key, int rating)
{
    TallyEntry *e = tally_find(t, key);
    if (!e) {
        if (t->count == t->cap) {
            size_t cap = t->cap ? t->cap * 2 : 16;
            if (cap > SIZE_MAX / sizeof(TallyEntry))
                return NULL;
            TallyEntry *grown = realloc(t->items, cap * sizeof(TallyEntry));
            if (!grown)
                return NULL;
            t->items = grown;
            t->cap = cap;
        }
        e = &t->items[t->count++];
        e->key = key;
        e->count = 0;
        e->rated = 0;
        e->rating_sum = 0.0;
    }
    e->count++;
    if (rating > 0) {
        e->rated++;
        e->rating_sum += rating;
    }
    return e;
}

static int by_count(const void *a, const void *b)
{
    const TallyEntry *x = a;
    const TallyEntry *y = b;
    if (x->count != y->count)
        return y->count - x->count;
    return text_casecmp(x->key, y->key);
}

void tally_sort_by_count(Tally *t)
{
    if (t->count > 1)
        qsort(t->items, t->count, sizeof(TallyEntry), by_count);
}
