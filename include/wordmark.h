/*
 * wordmark.h — the block-letter font for the CINEPHILIA title.
 *
 * Each letter is drawn by hand on a 5-row grid ('#' = ink). Rendering is
 * kept separate from the terminal so tests can check, by decoding the
 * rendered grid back into letters, that the title really spells
 * C-I-N-E-P-H-I-L-I-A in order.
 */
#ifndef CINEPHILIA_WORDMARK_H
#define CINEPHILIA_WORDMARK_H

#include <stddef.h>

#define WM_ROWS 5
#define WM_MAX_COLS 160
#define WM_WORD "CINEPHILIA"

typedef struct {
    char letter;
    int width;
    const char *rows[WM_ROWS];
} WmGlyph;

const WmGlyph *wordmark_glyph(char letter); /* NULL if not in the font */
int wordmark_width(const char *word, int gap); /* -1 if a letter is missing */

/* Renders word into rows of '#' and ' '. Returns the width, or -1. */
int wordmark_render(const char *word, int gap, char out[WM_ROWS][WM_MAX_COLS + 1]);

/* Reads letters back out of a rendered grid by splitting on blank columns
 * and matching each block against the font. Returns the number of letters
 * decoded, or -1 if a block matches no letter. */
int wordmark_decode(char rows[WM_ROWS][WM_MAX_COLS + 1], int width, char *out, size_t cap);

#endif
