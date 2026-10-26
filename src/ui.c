/*
 * ui.c — Terminal UI, rendering, and animation module for CINEPHILIA
 *
 * Handles all visual output: ANSI color rendering, ASCII art title,
 * cinematic opening animation, menu display, film cards, and safe
 * user input functions.
 *
 * Part of the CINEPHILIA project — a personal film archive for the terminal.
 */

#include "ui.h"
#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <errno.h>

/* ─── Color Palette ──────────────────────────────────────────────── */

#define RESET       "\033[0m"
#define BOLD        "\033[1m"
#define DIM         "\033[2m"
#define ITALIC      "\033[3m"
#define IVORY       "\033[38;2;234;228;216m"
#define CORAL       "\033[38;2;199;109;97m"
#define GOLD        "\033[38;2;184;154;99m"
#define MUTED       "\033[38;2;119;115;109m"
#define BG_DARK     "\033[48;2;10;10;10m"

/* ─── ASCII Art Title ────────────────────────────────────────────── */

static const char *TITLE_ART[] = {
    " ██████ ██ ███    ██ ███████ ██████  ██   ██ ██ ██      ██  █████  ",
    "██      ██ ████   ██ ██      ██   ██ ██   ██ ██ ██      ██ ██   ██ ",
    "██      ██ ██ ██  ██ █████   ██████  ███████ ██ ██      ██ ███████ ",
    "██      ██ ██  ██ ██ ██      ██      ██   ██ ██ ██      ██ ██   ██ ",
    " ██████ ██ ██   ████ ███████ ██      ██   ██ ██ ███████ ██ ██   ██ ",
};

#define TITLE_ART_LINES 5

static const char *TAGLINE = "\"What is this world without art?\"";

/* ─── Saved terminal state ───────────────────────────────────────── */

static struct termios saved_termios;
static int            termios_saved = 0;

/* ─── Film-strip motif ───────────────────────────────────────────── */

static const char *FILM_STRIP = "│▓░▓░▓│";

/* ═══════════════════════════════════════════════════════════════════
 *  1. Terminal Utilities
 * ═══════════════════════════════════════════════════════════════════ */

void ui_get_terminal_size(int *width, int *height)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
        ws.ws_col > 0 && ws.ws_row > 0) {
        *width  = ws.ws_col;
        *height = ws.ws_row;
    } else {
        /* Sensible fallback */
        *width  = 80;
        *height = 24;
    }
}

void ui_clear_screen(void)
{
    printf("\033[2J\033[H");
    fflush(stdout);
}

void ui_move_cursor(int row, int col)
{
    printf("\033[%d;%dH", row, col);
}

void ui_hide_cursor(void)
{
    printf("\033[?25l");
    fflush(stdout);
}

void ui_show_cursor(void)
{
    printf("\033[?25h");
    fflush(stdout);
}

void ui_flush(void)
{
    fflush(stdout);
}

int ui_is_interactive(void)
{
    return isatty(STDOUT_FILENO);
}

/* ═══════════════════════════════════════════════════════════════════
 *  2. Safe Input Functions
 * ═══════════════════════════════════════════════════════════════════ */

void ui_read_line(const char *prompt, char *buf, int maxlen)
{
    printf("%s%s%s ", GOLD, prompt, RESET);
    fflush(stdout);

    if (fgets(buf, maxlen, stdin) != NULL) {
        /* Strip trailing newline */
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
        }
    } else {
        buf[0] = '\0';
    }
}

int ui_read_int(const char *prompt, int *out)
{
    char buf[64];
    ui_read_line(prompt, buf, sizeof(buf));

    if (buf[0] == '\0') {
        return 0;
    }

    char *endptr = NULL;
    errno = 0;
    long val = strtol(buf, &endptr, 10);

    if (errno != 0 || endptr == buf || *endptr != '\0') {
        return 0;
    }

    *out = (int)val;
    return 1;
}

int ui_read_float(const char *prompt, float *out)
{
    char buf[64];
    ui_read_line(prompt, buf, sizeof(buf));

    if (buf[0] == '\0') {
        return 0;
    }

    char *endptr = NULL;
    errno = 0;
    float val = strtof(buf, &endptr);

    if (errno != 0 || endptr == buf || *endptr != '\0') {
        return 0;
    }

    *out = val;
    return 1;
}

int ui_confirm(const char *prompt)
{
    char buf[16];
    printf("%s%s (y/n): %s", GOLD, prompt, RESET);
    fflush(stdout);

    if (fgets(buf, sizeof(buf), stdin) == NULL) {
        return 0;
    }

    return (buf[0] == 'y' || buf[0] == 'Y');
}

void ui_press_enter(void)
{
    printf("\n%s  Press Enter to continue...%s", MUTED, RESET);
    fflush(stdout);

    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {
        /* consume any leftover characters */
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  3. Display Components
 * ═══════════════════════════════════════════════════════════════════ */

/*
 * Helper: print a string centered on the terminal.
 * visual_len is the number of visible characters (excluding ANSI codes).
 */
static void print_centered(const char *text, int visual_len)
{
    int tw, th;
    ui_get_terminal_size(&tw, &th);
    (void)th;

    if (tw < 70 || visual_len >= tw) {
        /* Compact mode — no centering */
        printf("%s", text);
        return;
    }

    int pad = (tw - visual_len) / 2;
    if (pad > 0) {
        printf("%*s", pad, "");
    }
    printf("%s", text);
}

void ui_print_header(const char *title)
{
    int tw, th;
    ui_get_terminal_size(&tw, &th);
    (void)th;

    int title_len = (int)strlen(title);
    int line_width = (tw < 70) ? tw : 60;

    printf("\n");

    /* Top decorative line */
    printf("  %s", GOLD);
    for (int i = 0; i < line_width; i++) {
        printf("─");
    }
    printf("%s\n", RESET);

    /* Centered title */
    int pad = (line_width - title_len) / 2;
    if (pad < 2) pad = 2;
    printf("  %s%s%*s%s%s\n", GOLD, BOLD, pad, "", title, RESET);

    /* Bottom decorative line */
    printf("  %s", GOLD);
    for (int i = 0; i < line_width; i++) {
        printf("─");
    }
    printf("%s\n\n", RESET);
}

void ui_print_divider(void)
{
    int tw, th;
    ui_get_terminal_size(&tw, &th);
    (void)th;

    int line_width = (tw < 70) ? tw - 2 : 60;

    printf("  %s", MUTED);
    for (int i = 0; i < line_width; i++) {
        printf("─");
    }
    printf("%s\n", RESET);
}

void ui_print_film_card(const Film *film, int show_details)
{
    if (film == NULL) return;

    /* Title in CORAL + BOLD */
    printf("\n  %s%s[%d] %s%s\n", CORAL, BOLD, film->id, film->title, RESET);

    /* Director and year */
    printf("  %s    Dir. %s  ·  %d%s\n", IVORY, film->director, film->year, RESET);

    /* Rating as dots: scale 0-10 to 0-5 filled dots */
    int filled = (int)(film->rating / 2.0f + 0.5f);
    if (filled < 0) filled = 0;
    if (filled > 5) filled = 5;

    printf("  %s    ", GOLD);
    for (int i = 0; i < 5; i++) {
        if (i < filled) {
            printf("● ");
        } else {
            printf("○ ");
        }
    }
    printf(" %.1f/10%s\n", film->rating, RESET);

    /* Genre in MUTED */
    if (film->genre[0] != '\0') {
        printf("  %s    %s%s\n", MUTED, film->genre, RESET);
    }

    if (show_details) {
        /* Watched status */
        if (film->watched) {
            printf("  %s    ✓ Watched%s", IVORY, RESET);
            if (film->date_watched[0] != '\0') {
                printf("  %s(%s)%s", MUTED, film->date_watched, RESET);
            }
            printf("\n");
        } else {
            printf("  %s    ○ On the watchlist%s\n", MUTED, RESET);
        }

        /* Notes */
        if (film->notes[0] != '\0') {
            printf("  %s    Notes: %s%s\n", IVORY, film->notes, RESET);
        }

        /* Journal entry */
        if (film->journal[0] != '\0') {
            printf("  %s%s    \"%s\"%s\n", IVORY, ITALIC, film->journal, RESET);
        }
    }
}

void ui_print_film_list(const Archive *ar, int watched_filter)
{
    if (ar == NULL || ar->count == 0) {
        ui_print_empty_archive();
        return;
    }

    int found = 0;

    for (int i = 0; i < ar->count; i++) {
        const Film *f = &ar->films[i];

        if (watched_filter == -1 ||
            (watched_filter == 1 && f->watched) ||
            (watched_filter == 0 && !f->watched)) {
            ui_print_film_card(f, 0);
            found++;
        }
    }

    if (found == 0) {
        if (watched_filter == 0) {
            printf("\n  %sYour watchlist is empty — every great film is waiting.%s\n",
                   MUTED, RESET);
        } else if (watched_filter == 1) {
            printf("\n  %sNo films marked as watched yet.%s\n",
                   MUTED, RESET);
        } else {
            ui_print_empty_archive();
        }
    } else {
        printf("\n  %s%d film%s%s\n", MUTED, found,
               found == 1 ? "" : "s", RESET);
    }
}

void ui_print_empty_archive(void)
{
    printf("\n");
    printf("  %s%s", MUTED, ITALIC);
    printf("  The archive is silent.\n");
    printf("    No reels, no frames — only possibility.\n");
    printf("    Add your first film and the projector begins.%s\n", RESET);
    printf("\n");
}

/* ═══════════════════════════════════════════════════════════════════
 *  4. ASCII Art Title
 * ═══════════════════════════════════════════════════════════════════ */

void ui_print_title(void)
{
    int tw, th;
    ui_get_terminal_size(&tw, &th);
    (void)th;

    printf("\n");

    /* Each title art line is about 67 visible characters */
    int art_width = 67;

    for (int i = 0; i < TITLE_ART_LINES; i++) {
        printf("%s", CORAL);
        print_centered(TITLE_ART[i], art_width);
        printf("%s\n", RESET);
    }

    printf("\n");

    /* Tagline — centered, muted and italic */
    int tagline_len = (int)strlen(TAGLINE);
    char tagline_buf[256];
    snprintf(tagline_buf, sizeof(tagline_buf),
             "%s%s%s%s", MUTED, ITALIC, TAGLINE, RESET);
    print_centered(tagline_buf, tagline_len);
    printf("\n\n");
}

/* ═══════════════════════════════════════════════════════════════════
 *  5. Opening Animation
 * ═══════════════════════════════════════════════════════════════════ */

void ui_opening_animation(void)
{
    /* Skip animation if not interactive */
    if (!ui_is_interactive()) {
        ui_print_title();
        return;
    }

    int tw, th;
    ui_get_terminal_size(&tw, &th);

    ui_clear_screen();
    ui_hide_cursor();

    /*
     * Phase 1: Film-strip motif from upper-left corner
     * Draw the strip at progressive diagonal positions.
     */
    int frames = 6;
    for (int f = 0; f < frames; f++) {
        int row = 1 + f;
        int col = 1 + f * 3;

        if (row > th || col > tw - 8) break;

        ui_move_cursor(row, col);
        printf("%s%s%s", MUTED, FILM_STRIP, RESET);
        ui_flush();
        usleep(80000); /* 80ms */

        /* Erase previous frame (except last) */
        if (f < frames - 1) {
            ui_move_cursor(row, col);
            printf("       ");
            ui_flush();
        }
    }

    /*
     * Phase 2: Film-strip motif from lower-right corner
     */
    for (int f = 0; f < frames; f++) {
        int row = th - f;
        int col = tw - 7 - f * 3;

        if (row < 1 || col < 1) break;

        ui_move_cursor(row, col);
        printf("%s%s%s", MUTED, FILM_STRIP, RESET);
        ui_flush();
        usleep(80000); /* 80ms */

        /* Erase previous frame (except last) */
        if (f < frames - 1) {
            ui_move_cursor(row, col);
            printf("       ");
            ui_flush();
        }
    }

    /* Clear screen before title reveal */
    ui_clear_screen();

    /*
     * Phase 3: Title reveal — line by line with delay
     */
    int art_width = 67;
    int start_row = (th - TITLE_ART_LINES) / 2 - 1;
    if (start_row < 2) start_row = 2;

    for (int i = 0; i < TITLE_ART_LINES; i++) {
        ui_move_cursor(start_row + i, 1);

        /* Center on terminal */
        int pad = (tw - art_width) / 2;
        if (pad < 0) pad = 0;

        printf("%s%*s%s%s\n", CORAL, pad, "", TITLE_ART[i], RESET);
        ui_flush();
        usleep(100000); /* 100ms per line */
    }

    /*
     * Phase 4: Tagline fade-in — character by character
     */
    int tagline_len = (int)strlen(TAGLINE);
    int tagline_row = start_row + TITLE_ART_LINES + 1;
    int tagline_pad = (tw - tagline_len) / 2;
    if (tagline_pad < 0) tagline_pad = 0;

    ui_move_cursor(tagline_row, tagline_pad + 1);
    printf("%s%s", MUTED, ITALIC);

    for (int i = 0; TAGLINE[i] != '\0'; i++) {
        /* Handle multi-byte UTF-8 sequences in tagline (there shouldn't be any,
         * but let's be safe — the tagline uses ASCII + curly quotes) */
        putchar(TAGLINE[i]);
        ui_flush();
        usleep(30000); /* 30ms per character */
    }
    printf("%s", RESET);
    ui_flush();

    /*
     * Phase 5: Brief pause, then show cursor
     */
    usleep(800000); /* 800ms */

    ui_show_cursor();
}

/* ═══════════════════════════════════════════════════════════════════
 *  6. Main Menu
 * ═══════════════════════════════════════════════════════════════════ */

int ui_main_menu(void)
{
    printf("\n");
    ui_print_divider();
    printf("\n");

    /* Menu items: number, label, description */
    static const struct {
        int         num;
        const char *label;
        const char *desc;
    } items[] = {
        { 1, "The Archive",     "Browse your film collection"      },
        { 2, "Add a Film",      "Record a new cinematic memory"    },
        { 3, "Watchlist",       "Films waiting to be discovered"   },
        { 4, "The Journal",     "Your personal cinema diary"       },
        { 5, "Discovery",       "Random picks & recommendations"   },
        { 6, "Search",          "Find films in your archive"       },
        { 7, "Statistics",      "Your cinema in numbers"           },
        { 0, "Leave Quietly",   "Until the next screening"         },
    };

    int n = (int)(sizeof(items) / sizeof(items[0]));

    for (int i = 0; i < n; i++) {
        printf("  %s%d%s %s───%s %s%-20s%s %s%s%s\n",
               CORAL, items[i].num, RESET,
               MUTED, RESET,
               IVORY, items[i].label, RESET,
               MUTED, items[i].desc, RESET);
    }

    printf("\n");

    int choice = -1;
    ui_read_int("  Choose a chapter ›", &choice);

    return choice;
}

/* ═══════════════════════════════════════════════════════════════════
 *  7. Film Forms
 * ═══════════════════════════════════════════════════════════════════ */

static void print_genre_suggestions(void)
{
    printf("\n  %sGenre suggestions: Drama, Comedy, Thriller, Horror, Sci-Fi, Romance,%s\n",
           MUTED, RESET);
    printf("  %s                   Documentary, Animation, Action, Mystery, Fantasy,%s\n",
           MUTED, RESET);
    printf("  %s                   War, Musical, Western, Noir, Experimental%s\n",
           MUTED, RESET);
}

void ui_add_film_form(Archive *ar)
{
    ui_print_header("A NEW FILM");

    Film film;
    memset(&film, 0, sizeof(Film));

    /* Title */
    ui_read_line("  Title:", film.title, sizeof(film.title));
    if (film.title[0] == '\0') {
        printf("  %sTitle cannot be empty.%s\n", CORAL, RESET);
        return;
    }

    /* Director */
    ui_read_line("  Director:", film.director, sizeof(film.director));
    if (film.director[0] == '\0') {
        printf("  %sDirector cannot be empty.%s\n", CORAL, RESET);
        return;
    }

    /* Year */
    int year = 0;
    if (!ui_read_int("  Year (1888–2100):", &year) ||
        year < 1888 || year > 2100) {
        printf("  %sPlease enter a valid year between 1888 and 2100.%s\n",
               CORAL, RESET);
        return;
    }
    film.year = year;

    /* Rating */
    float rating = 0.0f;
    if (!ui_read_float("  Rating (0.0–10.0):", &rating) ||
        rating < 0.0f || rating > 10.0f) {
        printf("  %sRating must be between 0.0 and 10.0.%s\n",
               CORAL, RESET);
        return;
    }
    film.rating = rating;

    /* Genre */
    print_genre_suggestions();
    ui_read_line("  Genre:", film.genre, sizeof(film.genre));

    /* Notes (optional) */
    ui_read_line("  Notes (optional):", film.notes, sizeof(film.notes));

    /* Default: not yet watched */
    film.watched = 0;

    /* Add to archive and save */
    archive_add(ar, &film);
    storage_save(ar);

    printf("\n  %s%s'%s' has entered your archive. ♡%s\n",
           IVORY, BOLD, film.title, RESET);
}

void ui_edit_film_form(Archive *ar)
{
    ui_print_header("EDIT A FILM");

    if (ar->count == 0) {
        ui_print_empty_archive();
        return;
    }

    ui_print_film_list(ar, -1);
    printf("\n");

    int id = 0;
    if (!ui_read_int("  Film ID to edit:", &id)) {
        printf("  %sInvalid input.%s\n", CORAL, RESET);
        return;
    }

    Film *film = archive_find_by_id(ar, id);
    if (film == NULL) {
        printf("  %sNo film found with ID %d.%s\n", CORAL, id, RESET);
        return;
    }

    ui_print_film_card(film, 1);
    printf("\n  %sPress Enter to keep the current value.%s\n\n", MUTED, RESET);

    char buf[256];

    /* Title */
    printf("  %sCurrent title: %s%s\n", MUTED, film->title, RESET);
    ui_read_line("  New title:", buf, sizeof(buf));
    if (buf[0] != '\0') {
        strncpy(film->title, buf, sizeof(film->title) - 1);
        film->title[sizeof(film->title) - 1] = '\0';
    }

    /* Director */
    printf("  %sCurrent director: %s%s\n", MUTED, film->director, RESET);
    ui_read_line("  New director:", buf, sizeof(buf));
    if (buf[0] != '\0') {
        strncpy(film->director, buf, sizeof(film->director) - 1);
        film->director[sizeof(film->director) - 1] = '\0';
    }

    /* Year */
    printf("  %sCurrent year: %d%s\n", MUTED, film->year, RESET);
    int new_year = 0;
    ui_read_line("  New year:", buf, sizeof(buf));
    if (buf[0] != '\0') {
        char *endptr = NULL;
        errno = 0;
        long val = strtol(buf, &endptr, 10);
        if (errno == 0 && endptr != buf && *endptr == '\0' &&
            val >= 1888 && val <= 2100) {
            new_year = (int)val;
            film->year = new_year;
        } else {
            printf("  %sInvalid year — keeping current value.%s\n", CORAL, RESET);
        }
    }

    /* Rating */
    printf("  %sCurrent rating: %.1f%s\n", MUTED, film->rating, RESET);
    ui_read_line("  New rating:", buf, sizeof(buf));
    if (buf[0] != '\0') {
        char *endptr = NULL;
        errno = 0;
        float val = strtof(buf, &endptr);
        if (errno == 0 && endptr != buf && *endptr == '\0' &&
            val >= 0.0f && val <= 10.0f) {
            film->rating = val;
        } else {
            printf("  %sInvalid rating — keeping current value.%s\n", CORAL, RESET);
        }
    }

    /* Genre */
    printf("  %sCurrent genre: %s%s\n", MUTED,
           film->genre[0] ? film->genre : "(none)", RESET);
    print_genre_suggestions();
    ui_read_line("  New genre:", buf, sizeof(buf));
    if (buf[0] != '\0') {
        strncpy(film->genre, buf, sizeof(film->genre) - 1);
        film->genre[sizeof(film->genre) - 1] = '\0';
    }

    /* Notes */
    printf("  %sCurrent notes: %s%s\n", MUTED,
           film->notes[0] ? film->notes : "(none)", RESET);
    ui_read_line("  New notes:", buf, sizeof(buf));
    if (buf[0] != '\0') {
        strncpy(film->notes, buf, sizeof(film->notes) - 1);
        film->notes[sizeof(film->notes) - 1] = '\0';
    }

    /* Watched status */
    printf("  %sCurrent status: %s%s\n", MUTED,
           film->watched ? "Watched" : "Watchlist", RESET);
    ui_read_line("  Watched? (y/n, Enter to keep):", buf, sizeof(buf));
    if (buf[0] == 'y' || buf[0] == 'Y') {
        film->watched = 1;
        if (film->date_watched[0] == '\0') {
            /* Auto-fill today's date */
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            if (tm_info != NULL) {
                strftime(film->date_watched, sizeof(film->date_watched),
                         "%Y-%m-%d", tm_info);
            }
        }
    } else if (buf[0] == 'n' || buf[0] == 'N') {
        film->watched = 0;
        film->date_watched[0] = '\0';
    }

    /* Journal */
    printf("  %sCurrent journal: %s%s\n", MUTED,
           film->journal[0] ? film->journal : "(none)", RESET);
    ui_read_line("  Journal entry:", buf, sizeof(buf));
    if (buf[0] != '\0') {
        strncpy(film->journal, buf, sizeof(film->journal) - 1);
        film->journal[sizeof(film->journal) - 1] = '\0';
    }

    /* Save */
    storage_save(ar);

    printf("\n  %s%s'%s' has been updated.%s\n",
           IVORY, BOLD, film->title, RESET);
}

void ui_delete_film(Archive *ar)
{
    ui_print_header("DELETE A FILM");

    if (ar->count == 0) {
        ui_print_empty_archive();
        return;
    }

    ui_print_film_list(ar, -1);
    printf("\n");

    int id = 0;
    if (!ui_read_int("  Film ID to delete:", &id)) {
        printf("  %sInvalid input.%s\n", CORAL, RESET);
        return;
    }

    Film *film = archive_find_by_id(ar, id);
    if (film == NULL) {
        printf("  %sNo film found with ID %d.%s\n", CORAL, id, RESET);
        return;
    }

    ui_print_film_card(film, 1);
    printf("\n");

    if (!ui_confirm("  Are you sure you want to delete this film?")) {
        printf("  %sDeletion cancelled.%s\n", MUTED, RESET);
        return;
    }

    char title_copy[128];
    strncpy(title_copy, film->title, sizeof(title_copy) - 1);
    title_copy[sizeof(title_copy) - 1] = '\0';

    archive_remove(ar, id);
    storage_save(ar);

    printf("\n  %s'%s' has been removed from the archive.%s\n",
           MUTED, title_copy, RESET);
}

void ui_view_film_detail(Archive *ar)
{
    ui_print_header("FILM DETAIL");

    if (ar->count == 0) {
        ui_print_empty_archive();
        return;
    }

    ui_print_film_list(ar, -1);
    printf("\n");

    int id = 0;
    if (!ui_read_int("  Film ID to view:", &id)) {
        printf("  %sInvalid input.%s\n", CORAL, RESET);
        return;
    }

    Film *film = archive_find_by_id(ar, id);
    if (film == NULL) {
        printf("  %sNo film found with ID %d.%s\n", CORAL, id, RESET);
        return;
    }

    printf("\n");
    ui_print_divider();
    ui_print_film_card(film, 1);
    ui_print_divider();
}

/* ═══════════════════════════════════════════════════════════════════
 *  8. Terminal State
 * ═══════════════════════════════════════════════════════════════════ */

void ui_setup_terminal(void)
{
    if (ui_is_interactive()) {
        if (tcgetattr(STDIN_FILENO, &saved_termios) == 0) {
            termios_saved = 1;
        }
    }
}

void ui_restore_terminal(void)
{
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
        termios_saved = 0;
    }

    ui_show_cursor();
    printf("%s", RESET);
    ui_flush();
}
