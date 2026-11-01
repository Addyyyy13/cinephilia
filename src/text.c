/*
 * text.c — UTF-8 aware text helpers. See text.h.
 */
#include "text.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int text_utf8_decode(const char *s, unsigned *cp)
{
    const unsigned char *u = (const unsigned char *)s;
    unsigned c;
    int len;

    if (u[0] < 0x80) {
        *cp = u[0];
        return 1;
    }
    if ((u[0] & 0xE0) == 0xC0) {
        len = 2;
        c = u[0] & 0x1F;
    } else if ((u[0] & 0xF0) == 0xE0) {
        len = 3;
        c = u[0] & 0x0F;
    } else if ((u[0] & 0xF8) == 0xF0) {
        len = 4;
        c = u[0] & 0x07;
    } else {
        *cp = 0xFFFD;
        return -1;
    }
    for (int i = 1; i < len; i++) {
        /* A NUL terminator fails this test too, so we never read past it. */
        if ((u[i] & 0xC0) != 0x80) {
            *cp = 0xFFFD;
            return -1;
        }
        c = (c << 6) | (u[i] & 0x3F);
    }
    if ((len == 2 && c < 0x80) || (len == 3 && c < 0x800) ||
        (len == 4 && c < 0x10000) || c > 0x10FFFF ||
        (c >= 0xD800 && c <= 0xDFFF)) {
        *cp = 0xFFFD;
        return -1;
    }
    *cp = c;
    return len;
}

/* Like text_utf8_decode but always advances at least one byte. */
static int utf8_step(const char *s, unsigned *cp)
{
    int n = text_utf8_decode(s, cp);
    return n < 0 ? 1 : n;
}

int text_codepoint_width(unsigned cp)
{
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0))
        return 0;
    /* Combining marks and zero-width characters. */
    if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x20D0 && cp <= 0x20FF) ||
        (cp >= 0xFE00 && cp <= 0xFE0F))
        return 0;
    /* East Asian wide and full-width ranges. */
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) ||
        (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
        (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
        (cp >= 0x1F900 && cp <= 0x1F9FF) || (cp >= 0x20000 && cp <= 0x3FFFD))
        return 2;
    return 1;
}

int text_width(const char *s)
{
    int w = 0;
    while (*s) {
        unsigned cp;
        s += utf8_step(s, &cp);
        w += text_codepoint_width(cp);
    }
    return w;
}

size_t text_copy(char *dst, size_t cap, const char *src)
{
    if (cap == 0)
        return 0;
    if (!src)
        src = "";
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        /* Step back to the start of a character so we never split one. */
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80)
            n--;
    }
    memmove(dst, src, n);
    dst[n] = '\0';
    return n;
}

void text_trim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
    size_t i = 0;
    while (s[i] && isspace((unsigned char)s[i]))
        i++;
    if (i > 0)
        memmove(s, s + i, n - i + 1);
}

void text_sanitize(char *s)
{
    char *r = s;
    char *w = s;
    while (*r) {
        unsigned cp;
        int n = text_utf8_decode(r, &cp);
        if (n < 0) {
            *w++ = '?';
            r++;
            continue;
        }
        if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0)) {
            *w++ = ' ';
            r += n;
            continue;
        }
        if ((cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069) ||
            cp == 0x2028 || cp == 0x2029) {
            r += n; /* drop direction overrides and line separators */
            continue;
        }
        memmove(w, r, (size_t)n);
        w += n;
        r += n;
    }
    *w = '\0';
}

int text_fit(char *dst, size_t cap, const char *src, int width)
{
    if (cap == 0)
        return 0;
    dst[0] = '\0';
    if (!src || width <= 0)
        return 0;

    bool cut = text_width(src) > width;
    int budget = cut ? width - 1 : width; /* leave a column for the ellipsis */
    size_t o = 0;
    int w = 0;
    const char *p = src;

    while (*p) {
        unsigned cp;
        int n = text_utf8_decode(p, &cp);
        bool bad = n < 0;
        if (bad)
            n = 1;
        int cw = bad ? 1 : text_codepoint_width(cp);
        if (w + cw > budget)
            break;
        size_t need = bad ? 1 : (size_t)n;
        if (o + need + 4 > cap) { /* keep room for "…" and the NUL */
            cut = true;
            break;
        }
        if (bad)
            dst[o++] = '?';
        else {
            memcpy(dst + o, p, (size_t)n);
            o += (size_t)n;
        }
        w += cw;
        p += n;
    }
    if (cut && w < width && o + 4 <= cap) {
        memcpy(dst + o, "\xE2\x80\xA6", 3);
        o += 3;
        w += 1;
    }
    dst[o] = '\0';
    return w;
}

size_t text_wrap_next(const char *s, int width, char *out, size_t cap)
{
    if (cap > 0)
        out[0] = '\0';
    if (width < 1)
        width = 1;

    const char *start = s;
    while (*s == ' ')
        s++;
    if (*s == '\0')
        return 0;

    const char *p = s;
    const char *brk = NULL;
    const char *end = NULL;
    const char *next = NULL;
    int w = 0;

    while (*p) {
        unsigned cp;
        int n = text_utf8_decode(p, &cp);
        if (n < 0) {
            n = 1;
            cp = '?';
        }
        int cw = text_codepoint_width(cp);
        if (cp == ' ')
            brk = p;
        if (w + cw > width) {
            if (cp == ' ') {
                end = p;
                next = p + 1;
            } else if (brk) {
                end = brk;
                next = brk + 1;
            } else if (p == s) { /* a single character wider than the line */
                end = p + n;
                next = p + n;
            } else { /* one long word: break it */
                end = p;
                next = p;
            }
            break;
        }
        w += cw;
        p += n;
    }
    if (!end) {
        end = p;
        next = p;
    }

    size_t len = (size_t)(end - s);
    while (len > 0 && s[len - 1] == ' ')
        len--;
    if (cap > 0) {
        if (len >= cap) {
            len = cap - 1;
            while (len > 0 && ((unsigned char)s[len] & 0xC0) == 0x80)
                len--;
        }
        memcpy(out, s, len);
        out[len] = '\0';
    }
    return (size_t)(next - start);
}

static int fold(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

int text_casecmp(const char *a, const char *b)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    while (*x && fold(*x) == fold(*y)) {
        x++;
        y++;
    }
    return fold(*x) - fold(*y);
}

int text_ncasecmp(const char *a, const char *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++) {
        int d = fold(x[i]) - fold(y[i]);
        if (d != 0 || x[i] == '\0')
            return d;
    }
    return 0;
}

bool text_contains_ci(const char *haystack, const char *needle)
{
    size_t m = strlen(needle);
    if (m == 0)
        return true;
    for (const char *p = haystack; *p; p++)
        if (text_ncasecmp(p, needle, m) == 0)
            return true;
    return false;
}

static unsigned upper_cp(unsigned cp)
{
    if (cp >= 'a' && cp <= 'z')
        return cp - 32;
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) /* à..þ except ÷ */
        return cp - 32;
    /* Latin Extended-A: upper/lower pairs, even = upper, in these ranges. */
    if (((cp >= 0x100 && cp <= 0x137) || (cp >= 0x14A && cp <= 0x177)) && (cp & 1) &&
        cp != 0x131)
        return cp - 1;
    if (cp >= 0x13A && cp <= 0x148 && !(cp & 1)) /* ĺ..ň use odd/even the other way */
        return cp - 1;
    if (cp == 0x17A || cp == 0x17C || cp == 0x17E) /* ź ż ž */
        return cp - 1;
    return cp;
}

static int utf8_encode(unsigned cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

void text_upper(char *dst, size_t cap, const char *src)
{
    if (cap == 0)
        return;
    size_t o = 0;
    while (*src) {
        unsigned cp;
        int n = text_utf8_decode(src, &cp);
        char enc[4];
        int len;
        if (n < 0) {
            enc[0] = '?';
            len = 1;
            n = 1;
        } else {
            len = utf8_encode(upper_cp(cp), enc);
        }
        if (o + (size_t)len >= cap)
            break;
        memcpy(dst + o, enc, (size_t)len);
        o += (size_t)len;
        src += n;
    }
    dst[o] = '\0';
}

bool text_parse_int(const char *s, long min, long max, long *out)
{
    while (isspace((unsigned char)*s))
        s++;
    if (*s == '\0')
        return false;
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s)
        return false;
    while (isspace((unsigned char)*end))
        end++;
    if (*end != '\0' || v < min || v > max)
        return false;
    *out = v;
    return true;
}

static bool is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

bool date_valid(const char *s)
{
    if (!s || strlen(s) != 10)
        return false;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) {
            if (s[i] != '-')
                return false;
        } else if (!isdigit((unsigned char)s[i])) {
            return false;
        }
    }
    int y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    int m = (s[5] - '0') * 10 + (s[6] - '0');
    int d = (s[8] - '0') * 10 + (s[9] - '0');
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (y < 1870 || y > 2200 || m < 1 || m > 12)
        return false;
    int max = days[m - 1] + (m == 2 && is_leap(y) ? 1 : 0);
    return d >= 1 && d <= max;
}

void date_today(char out[DATE_LEN])
{
    time_t t = time(NULL);
    struct tm tm;
    if (localtime_r(&t, &tm) == NULL || strftime(out, DATE_LEN, "%Y-%m-%d", &tm) == 0)
        text_copy(out, DATE_LEN, "2000-01-01");
}

int date_year(const char *date)
{
    return date_valid(date) ? atoi(date) : 0;
}

int date_month(const char *date)
{
    return date_valid(date) ? (date[5] - '0') * 10 + (date[6] - '0') : 0;
}

const char *month_abbrev(int month)
{
    static const char *names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return (month >= 1 && month <= 12) ? names[month - 1] : "???";
}

void date_pretty(const char *iso, char *out, size_t cap)
{
    if (!date_valid(iso)) {
        text_copy(out, cap, iso ? iso : "");
        return;
    }
    /* Built by hand rather than with strftime so month names never depend
     * on the user's locale. */
    int day = (iso[8] - '0') * 10 + (iso[9] - '0');
    char tmp[24];
    snprintf(tmp, sizeof tmp, "%d %s %.4s", day, month_abbrev(date_month(iso)), iso);
    text_copy(out, cap, tmp);
}
