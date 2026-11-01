/*
 * search.c — merge sort on the film list, search and filters. See search.h.
 */
#include "search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sb.h"
#include "text.h"

const char *film_sort_title(const char *title)
{
    static const char *const articles[] = {"the ", "a ", "an "};
    for (size_t i = 0; i < sizeof articles / sizeof articles[0]; i++) {
        size_t n = strlen(articles[i]);
        if (text_ncasecmp(title, articles[i], n) == 0 && title[n] != '\0')
            return title + n;
    }
    return title;
}

static int cmp_int(int a, int b)
{
    return (a > b) - (a < b);
}

/* Total order used by the sort: primary key (possibly reversed), then title,
 * then id. Missing values are placed last regardless of direction. */
static int film_compare(const Film *a, const Film *b, SortKey key, bool desc)
{
    int c = 0;
    switch (key) {
    case SORT_TITLE:
        c = text_casecmp(film_sort_title(a->title), film_sort_title(b->title));
        break;
    case SORT_DIRECTOR: {
        bool ea = a->director[0] == '\0';
        bool eb = b->director[0] == '\0';
        if (ea != eb)
            return ea ? 1 : -1;
        c = text_casecmp(a->director, b->director);
        if (c == 0)
            c = cmp_int(a->year, b->year);
        break;
    }
    case SORT_YEAR:
        if ((a->year == 0) != (b->year == 0))
            return a->year == 0 ? 1 : -1;
        c = cmp_int(a->year, b->year);
        break;
    case SORT_RATING:
        if ((a->rating == 0) != (b->rating == 0))
            return a->rating == 0 ? 1 : -1;
        c = cmp_int(a->rating, b->rating);
        break;
    case SORT_ADDED:
        c = strcmp(a->added_on, b->added_on);
        if (c == 0) /* same day: the later id was added later */
            c = (a->id > b->id) - (a->id < b->id);
        break;
    }
    if (desc)
        c = -c;
    if (c != 0)
        return c;
    c = text_casecmp(film_sort_title(a->title), film_sort_title(b->title));
    if (c != 0)
        return c;
    return (a->id > b->id) - (a->id < b->id);
}

/* Merges two sorted singly linked runs (using only `next`). Taking from the
 * left run on ties keeps the sort stable. */
static Film *merge(Film *left, Film *right, SortKey key, bool desc)
{
    Film head;
    Film *tail = &head;
    head.next = NULL;
    while (left && right) {
        if (film_compare(left, right, key, desc) <= 0) {
            tail->next = left;
            left = left->next;
        } else {
            tail->next = right;
            right = right->next;
        }
        tail = tail->next;
    }
    tail->next = left ? left : right;
    return head.next;
}

static Film *merge_sort(Film *list, SortKey key, bool desc)
{
    if (!list || !list->next)
        return list;
    /* Find the middle with the slow/fast pointer technique. */
    Film *slow = list;
    Film *fast = list->next;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
    }
    Film *second = slow->next;
    slow->next = NULL;
    return merge(merge_sort(list, key, desc), merge_sort(second, key, desc), key, desc);
}

void archive_sort(Archive *a, SortKey key, bool descending)
{
    a->head = merge_sort(a->head, key, descending);
    /* The sort only maintained `next`; rebuild `prev` and `tail`. */
    Film *prev = NULL;
    for (Film *f = a->head; f; f = f->next) {
        f->prev = prev;
        prev = f;
    }
    a->tail = prev;
}

bool sort_default_descending(SortKey key)
{
    return key == SORT_RATING || key == SORT_ADDED;
}

const char *sort_key_name(SortKey key)
{
    switch (key) {
    case SORT_TITLE: return "title";
    case SORT_DIRECTOR: return "director";
    case SORT_YEAR: return "year";
    case SORT_RATING: return "rating";
    case SORT_ADDED: return "date added";
    }
    return "title";
}

void film_filter_init(FilmFilter *f)
{
    memset(f, 0, sizeof *f);
    f->status = -1;
}

bool film_filter_active(const FilmFilter *f)
{
    return f->query[0] || f->genre[0] || f->min_rating > 0 || f->year_from > 0 ||
           f->year_to > 0 || f->status >= 0;
}

bool film_matches(const Film *film, const FilmFilter *f)
{
    if (!f)
        return true;
    if (f->query[0] && !text_contains_ci(film->title, f->query) &&
        !text_contains_ci(film->director, f->query))
        return false;
    if (f->genre[0] && text_casecmp(film->genre, f->genre) != 0)
        return false;
    if (f->min_rating > 0 && film->rating < f->min_rating)
        return false;
    if (f->year_from > 0 && (film->year == 0 || film->year < f->year_from))
        return false;
    if (f->year_to > 0 && (film->year == 0 || film->year > f->year_to))
        return false;
    if (f->status >= 0 && (int)film->status != f->status)
        return false;
    return true;
}

void film_filter_describe(const FilmFilter *f, char *out, size_t cap)
{
    Sb sb;
    sb_init(&sb);
    const char *sep = "";
    if (f->query[0]) {
        sb_printf(&sb, "%s\xE2\x80\x9C%s\xE2\x80\x9D", sep, f->query);
        sep = " \xC2\xB7 ";
    }
    if (f->genre[0]) {
        sb_printf(&sb, "%s%s", sep, f->genre);
        sep = " \xC2\xB7 ";
    }
    if (f->min_rating > 0) {
        sb_printf(&sb, "%srated %d+", sep, f->min_rating);
        sep = " \xC2\xB7 ";
    }
    if (f->year_from > 0 && f->year_to > 0)
        sb_printf(&sb, "%s%d\xE2\x80\x93%d", sep, f->year_from, f->year_to);
    else if (f->year_from > 0)
        sb_printf(&sb, "%sfrom %d", sep, f->year_from);
    else if (f->year_to > 0)
        sb_printf(&sb, "%sup to %d", sep, f->year_to);
    if (f->year_from > 0 || f->year_to > 0)
        sep = " \xC2\xB7 ";
    if (f->status >= 0)
        sb_printf(&sb, "%s%s", sep, f->status == FILM_WATCHED ? "watched" : "watchlist");
    text_copy(out, cap, sb_str(&sb));
    sb_free(&sb);
}

bool archive_collect(const Archive *a, const FilmFilter *f, const Film ***out, size_t *count)
{
    *out = NULL;
    *count = 0;
    size_t n = 0;
    for (const Film *x = a->head; x; x = x->next)
        if (film_matches(x, f))
            n++;
    if (n == 0)
        return true;
    const Film **arr = malloc(n * sizeof *arr);
    if (!arr)
        return false;
    size_t i = 0;
    for (const Film *x = a->head; x; x = x->next)
        if (film_matches(x, f))
            arr[i++] = x;
    *out = arr;
    *count = n;
    return true;
}
