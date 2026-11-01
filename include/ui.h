/*
 * ui.h — Cinephilia's visual system: palette, typography helpers, menus,
 * pages, prompts, transitions and the opening sequence.
 *
 * Two modes, chosen automatically:
 *   interactive — stdin and stdout are a real terminal (TERM not "dumb"):
 *                 alternate screen, arrow-key menus, colour, motion. The
 *                 terminal is always restored on exit, Ctrl+C, SIGTERM,
 *                 SIGHUP, and around Ctrl+Z.
 *   line mode   — pipes, files, dumb terminals: numbered menus and plain
 *                 prompts with no escape sequences. The end-to-end tests
 *                 drive the application this way.
 * Independently, when the locale is not UTF-8 (or CINEPHILIA_ASCII is set)
 * every decorative glyph is replaced by a one-column ASCII equivalent on
 * output, so alignment never changes.
 */
#ifndef CINEPHILIA_UI_H
#define CINEPHILIA_UI_H

#include <stdbool.h>
#include <stddef.h>

#include "sb.h"

/* Foreground roles. Backgrounds (midnight #0A0A0A, charcoal #151515,
 * graphite #1D1B19) are applied by the layout code itself. */
typedef enum {
    UI_IVORY, /* primary text          #EAE4D8 */
    UI_STONE, /* secondary text        #969087 */
    UI_GREY,  /* hints, tertiary text  #77736D */
    UI_RED,   /* actions, selection    #C76D61 */
    UI_GOLD,  /* small highlights      #B89A63 */
    UI_ASH,   /* separators, shadows   #35312D */
    UI_BRASS, /* film-strip frames     #5E5037 */
    UI_COLOR_COUNT
} UiColor;

typedef enum { UI_TONE_INFO, UI_TONE_OK, UI_TONE_WARN, UI_TONE_ERROR } UiTone;

typedef enum {
    PROMPT_OK,     /* a value was entered */
    PROMPT_BLANK,  /* Enter on an empty line */
    PROMPT_CLEAR,  /* "-" (number and date prompts only) */
    PROMPT_CANCEL  /* end of input (Ctrl+D, or the input stream ended) */
} PromptResult;

/* Moments the interface reports so the application can attach sound. */
typedef enum { UI_EVENT_MOVE, UI_EVENT_ERROR } UiEvent;

enum { UI_BACK = -1 };

typedef struct {
    bool no_animation;
    bool no_color;
} UiOptions;

void ui_init(const UiOptions *opts);
void ui_shutdown(void); /* idempotent */
bool ui_interactive(void);
bool ui_input_closed(void);
bool ui_ascii(void);
void ui_set_animation(bool on);
bool ui_animation(void);
void ui_set_event_hook(void (*hook)(UiEvent));
/* Called from fatal-signal handlers; must be async-signal-safe. */
void ui_set_signal_cleanup(void (*fn)(void));

int ui_cols(void);
int ui_rows(void);
int ui_content_width(void);
int ui_margin(void);

/* Styling into a builder; these emit nothing when colour is off. */
void ui_sb_fg(Sb *sb, UiColor c);
void ui_sb_bold(Sb *sb);
void ui_sb_italic(Sb *sb);
void ui_sb_plain(Sb *sb); /* regular weight, ivory, midnight background */
void ui_sb_margin(Sb *sb);
/* Letter-spaced upper-case label: "Statistics" -> "S T A T I S T I C S". */
void ui_sb_tracked(Sb *sb, const char *text);
/* Three rows of large numerals (digits, '.', '/', ' '), appended to rows[0..2]. */
int ui_sb_bignum(Sb rows[3], const char *text);

/*
 * Menus. Each item is
 *     "label[\tdetail][\nsecond line[\nthird line ...]]"
 * A label beginning with digits and two spaces ("01  THE COLLECTION")
 * gets a styled index number. Lines after the first are secondary text; a
 * line beginning with '\x0e' is set in italics (personal notes). An empty
 * item is a spacer and an item beginning with '\f' is a heading; neither
 * can be selected. The preamble and postamble are pre-styled lines that
 * scroll with the items.
 */
typedef struct {
    const char *section;         /* designation in the header (upper-cased) */
    const StrList *preamble;
    const char *const *items;
    int count;
    int selected;
    const char *back_label;      /* "Back", "Exit", ... */
    const StrList *postamble;
    const char *highlight;       /* text to mark inside items (search) */
    bool detail_right;           /* right-align details (catalogue rows) */
    bool number_keys;            /* keys 1-9 choose the n-th option */
} UiMenu;

/* Returns the chosen index or UI_BACK. */
int ui_menu(const UiMenu *m);
void ui_page(const char *section, const StrList *lines);

void ui_form_begin(const char *section, const char *heading, const char *instructions);
PromptResult ui_prompt(const char *label, const char *hint, const char *current,
                       char *out, size_t cap);
PromptResult ui_prompt_int(const char *label, const char *hint, const char *current,
                           int min, int max, int *out);
/* YYYY-MM-DD or "today"; dates after max_date (if given) are refused. */
PromptResult ui_prompt_date(const char *label, const char *hint, const char *current,
                            const char *max_date, char out[11]);
bool ui_confirm(const char *question, bool default_yes);

void ui_say(UiTone tone, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* Shown on the next screen (printed at once in line mode). */
void ui_flash(UiTone tone, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* The opening sequence and title card. Returns false if the user asked to
 * leave. `on_start` is called as the sequence begins (to start a sound). */
bool ui_intro(const char *count_line, void (*on_start)(void));
/* The reel-like reveal used by Tonight's Selection. */
void ui_reveal(const char *section, const char *heading, const char *const *names, size_t n,
               const char *final_title);
/* A closing card; waits up to `ms` (any key skips). */
void ui_farewell(int ms);
void ui_print_banner(void);

#endif
