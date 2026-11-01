/*
 * sb.h — a growable string builder and a list of owned strings.
 *
 * Screens are composed into a builder and written to the terminal in one
 * call, which is what keeps redraws free of flicker.
 */
#ifndef CINEPHILIA_SB_H
#define CINEPHILIA_SB_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    bool failed; /* set once an allocation fails; further writes are ignored */
} Sb;

void sb_init(Sb *sb);
void sb_free(Sb *sb);
void sb_clear(Sb *sb);
void sb_putn(Sb *sb, const char *s, size_t n);
void sb_puts(Sb *sb, const char *s);
void sb_putc(Sb *sb, char c);
void sb_printf(Sb *sb, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void sb_spaces(Sb *sb, int n);
void sb_repeat(Sb *sb, const char *s, int n);
/* Appends s shortened to `width` columns (with an ellipsis if cut). When
 * pad is true, pads with spaces to exactly `width`. Returns columns used. */
int sb_fit(Sb *sb, const char *s, int width, bool pad);
const char *sb_str(const Sb *sb);   /* never NULL */
char *sb_take(Sb *sb);              /* hands over the buffer; NULL on failure */

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} StrList;

void strlist_init(StrList *l);
void strlist_free(StrList *l);
bool strlist_push(StrList *l, const char *s); /* stores a copy */
bool strlist_push_sb(StrList *l, Sb *sb);    /* takes the builder's text and resets it */

#endif
