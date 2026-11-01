/*
 * wordmark.c — hand-drawn block letters for the title. See wordmark.h.
 */
#include "wordmark.h"

#include <ctype.h>
#include <string.h>

/* Strokes are two columns wide: terminal cells are about twice as tall as
 * they are wide, so a two-column stroke reads as square. */
static const WmGlyph FONT[] = {
    {'A', 6, {" #### ", "##  ##", "######", "##  ##", "##  ##"}},
    {'C', 6, {" #####", "##    ", "##    ", "##    ", " #####"}},
    {'E', 5, {"#####", "##   ", "#### ", "##   ", "#####"}},
    {'H', 6, {"##  ##", "##  ##", "######", "##  ##", "##  ##"}},
    {'I', 4, {"####", " ## ", " ## ", " ## ", "####"}},
    {'L', 5, {"##   ", "##   ", "##   ", "##   ", "#####"}},
    {'N', 7, {"##   ##", "###  ##", "## # ##", "##  ###", "##   ##"}},
    {'P', 6, {"##### ", "##  ##", "##### ", "##    ", "##    "}},
};

const WmGlyph *wordmark_glyph(char letter)
{
    char up = (char)toupper((unsigned char)letter);
    for (size_t i = 0; i < sizeof FONT / sizeof FONT[0]; i++)
        if (FONT[i].letter == up)
            return &FONT[i];
    return NULL;
}

int wordmark_width(const char *word, int gap)
{
    int w = 0;
    for (const char *p = word; *p; p++) {
        const WmGlyph *g = wordmark_glyph(*p);
        if (!g)
            return -1;
        w += g->width + (p[1] ? gap : 0);
    }
    return w;
}

int wordmark_render(const char *word, int gap, char out[WM_ROWS][WM_MAX_COLS + 1])
{
    int w = wordmark_width(word, gap);
    if (w < 0 || w > WM_MAX_COLS)
        return -1;
    for (int r = 0; r < WM_ROWS; r++) {
        memset(out[r], ' ', (size_t)w);
        out[r][w] = '\0';
    }
    int x = 0;
    for (const char *p = word; *p; p++) {
        const WmGlyph *g = wordmark_glyph(*p);
        for (int r = 0; r < WM_ROWS; r++)
            memcpy(out[r] + x, g->rows[r], (size_t)g->width);
        x += g->width + (p[1] ? gap : 0);
    }
    return w;
}

static int column_blank(char rows[WM_ROWS][WM_MAX_COLS + 1], int c)
{
    for (int r = 0; r < WM_ROWS; r++)
        if (rows[r][c] != ' ')
            return 0;
    return 1;
}

int wordmark_decode(char rows[WM_ROWS][WM_MAX_COLS + 1], int width, char *out, size_t cap)
{
    size_t n = 0;
    int c = 0;
    while (c < width) {
        while (c < width && column_blank(rows, c))
            c++;
        if (c >= width)
            break;
        int start = c;
        while (c < width && !column_blank(rows, c))
            c++;
        int seg = c - start;
        const WmGlyph *match = NULL;
        for (size_t i = 0; i < sizeof FONT / sizeof FONT[0] && !match; i++) {
            if (FONT[i].width != seg)
                continue;
            int same = 1;
            for (int r = 0; r < WM_ROWS && same; r++)
                same = memcmp(rows[r] + start, FONT[i].rows[r], (size_t)seg) == 0;
            if (same)
                match = &FONT[i];
        }
        if (!match)
            return -1;
        if (n + 1 < cap)
            out[n] = match->letter;
        n++;
    }
    if (cap > 0)
        out[n < cap ? n : cap - 1] = '\0';
    return (int)n;
}
