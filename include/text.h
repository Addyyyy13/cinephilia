/*
 * text.h — small, dependency-free text utilities.
 *
 * Everything in Cinephilia is stored and displayed as UTF-8. These helpers
 * know enough about UTF-8 to measure display width (including wide CJK
 * characters), truncate without splitting a character, wrap words, and strip
 * control characters so that stored text can never inject terminal escape
 * sequences.
 */
#ifndef CINEPHILIA_TEXT_H
#define CINEPHILIA_TEXT_H

#include <stdbool.h>
#include <stddef.h>

#define DATE_LEN 11 /* "YYYY-MM-DD" plus terminator */

/* Decodes one UTF-8 sequence. Returns its length in bytes, or -1 if the
 * bytes at s are not valid UTF-8 (cp is then set to U+FFFD). */
int text_utf8_decode(const char *s, unsigned *cp);

/* Columns a code point occupies in a terminal: 0, 1 or 2. */
int text_codepoint_width(unsigned cp);

/* Display width of a UTF-8 string in terminal columns. */
int text_width(const char *s);

/* Bounded copy that never splits a UTF-8 sequence. Always terminates dst
 * when cap > 0. Returns the number of bytes copied. */
size_t text_copy(char *dst, size_t cap, const char *src);

/* Removes leading and trailing ASCII whitespace in place. */
void text_trim(char *s);

/* Replaces control characters (C0, DEL, C1) with spaces, drops bidi
 * override characters and replaces invalid UTF-8 bytes with '?'. */
void text_sanitize(char *s);

/* Copies src into dst so that it fits in `width` columns, ending with an
 * ellipsis when shortened. Returns the width actually used. */
int text_fit(char *dst, size_t cap, const char *src, int width);

/* Word wrapping. Writes the next line (at most `width` columns) of s into
 * out and returns how many bytes of s were consumed, or 0 when nothing but
 * whitespace remains. Typical use:
 *     while ((n = text_wrap_next(p, w, line, sizeof line)) > 0) { ...; p += n; }
 */
size_t text_wrap_next(const char *s, int width, char *out, size_t cap);

/* ASCII case-insensitive comparison and search. Non-ASCII bytes compare
 * exactly (see README: accented letters are not folded). */
int text_casecmp(const char *a, const char *b);
int text_ncasecmp(const char *a, const char *b, size_t n);
bool text_contains_ci(const char *haystack, const char *needle);

/* Upper-cases ASCII and the common accented Latin letters (é -> É), leaving
 * everything else untouched, for editorial titles. */
void text_upper(char *dst, size_t cap, const char *src);

/* Parses a whole decimal integer (surrounding spaces allowed) within
 * [min, max]. Rejects trailing junk and overflow. */
bool text_parse_int(const char *s, long min, long max, long *out);

/* Dates are ISO-8601 calendar dates ("2026-10-22"), which also sort
 * correctly as plain strings. */
bool date_valid(const char *s);
void date_today(char out[DATE_LEN]);
int date_year(const char *date); /* 0 when invalid */
int date_month(const char *date); /* 0 when invalid */
/* "2026-10-22" -> "22 Oct 2026"; copies the input unchanged if invalid. */
void date_pretty(const char *iso, char *out, size_t cap);
const char *month_abbrev(int month); /* 1..12 -> "Jan".."Dec" */

#endif
