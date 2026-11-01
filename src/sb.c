/*
 * sb.c — string builder and string list. See sb.h.
 */
#include "sb.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"

static bool sb_reserve(Sb *sb, size_t extra)
{
    if (sb->failed)
        return false;
    if (extra > SIZE_MAX - sb->len - 1) {
        sb->failed = true;
        return false;
    }
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap)
        return true;
    size_t cap = sb->cap ? sb->cap : 64;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) {
            cap = need;
            break;
        }
        cap *= 2;
    }
    char *grown = realloc(sb->buf, cap);
    if (!grown) {
        sb->failed = true;
        return false;
    }
    sb->buf = grown;
    sb->cap = cap;
    return true;
}

void sb_init(Sb *sb)
{
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
    sb->failed = false;
}

void sb_free(Sb *sb)
{
    free(sb->buf);
    sb_init(sb);
}

void sb_clear(Sb *sb)
{
    sb->len = 0;
    if (sb->buf)
        sb->buf[0] = '\0';
    sb->failed = false;
}

void sb_putn(Sb *sb, const char *s, size_t n)
{
    if (!sb_reserve(sb, n))
        return;
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

void sb_puts(Sb *sb, const char *s)
{
    sb_putn(sb, s, strlen(s));
}

void sb_putc(Sb *sb, char c)
{
    sb_putn(sb, &c, 1);
}

void sb_printf(Sb *sb, const char *fmt, ...)
{
    va_list ap;
    va_list copy;
    va_start(ap, fmt);
    va_copy(copy, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        sb->failed = true;
    } else if (sb_reserve(sb, (size_t)n)) {
        vsnprintf(sb->buf + sb->len, (size_t)n + 1, fmt, copy);
        sb->len += (size_t)n;
    }
    va_end(copy);
}

void sb_spaces(Sb *sb, int n)
{
    if (n <= 0 || !sb_reserve(sb, (size_t)n))
        return;
    memset(sb->buf + sb->len, ' ', (size_t)n);
    sb->len += (size_t)n;
    sb->buf[sb->len] = '\0';
}

void sb_repeat(Sb *sb, const char *s, int n)
{
    for (int i = 0; i < n; i++)
        sb_puts(sb, s);
}

int sb_fit(Sb *sb, const char *s, int width, bool pad)
{
    if (width <= 0)
        return 0;
    if (width > 400)
        width = 400;
    char tmp[400 * 4 + 8];
    int w = text_fit(tmp, sizeof tmp, s ? s : "", width);
    sb_puts(sb, tmp);
    if (pad && w < width) {
        sb_spaces(sb, width - w);
        w = width;
    }
    return w;
}

const char *sb_str(const Sb *sb)
{
    return sb->buf ? sb->buf : "";
}

char *sb_take(Sb *sb)
{
    if (sb->failed) {
        sb_free(sb);
        return NULL;
    }
    char *out = sb->buf;
    if (!out) {
        out = malloc(1);
        if (out)
            out[0] = '\0';
    }
    sb_init(sb);
    return out;
}

void strlist_init(StrList *l)
{
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

void strlist_free(StrList *l)
{
    for (size_t i = 0; i < l->count; i++)
        free(l->items[i]);
    free(l->items);
    strlist_init(l);
}

static bool strlist_append_owned(StrList *l, char *s)
{
    if (l->count == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 16;
        if (cap > SIZE_MAX / sizeof(char *))
            return false;
        char **grown = realloc(l->items, cap * sizeof(char *));
        if (!grown)
            return false;
        l->items = grown;
        l->cap = cap;
    }
    l->items[l->count++] = s;
    return true;
}

bool strlist_push(StrList *l, const char *s)
{
    size_t n = strlen(s);
    char *copy = malloc(n + 1);
    if (!copy)
        return false;
    memcpy(copy, s, n + 1);
    if (!strlist_append_owned(l, copy)) {
        free(copy);
        return false;
    }
    return true;
}

bool strlist_push_sb(StrList *l, Sb *sb)
{
    char *s = sb_take(sb);
    if (!s)
        return false;
    if (!strlist_append_owned(l, s)) {
        free(s);
        return false;
    }
    return true;
}
