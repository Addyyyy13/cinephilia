/*
 * Cinephilia — a personal archive of cinema.
 * "What is this world without art?"
 *
 * main.c: start-up, navigation and every screen. Screens read and change
 * the archive only through film.h / search.h / discover.h / stats.h; every
 * change is saved immediately through storage.h. Rendering primitives live
 * in ui.c and sound in audio.c, so this file is about content and flow.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "discover.h"
#include "dsa/stack.h"
#include "dsa/tally.h"
#include "film.h"
#include "sb.h"
#include "search.h"
#include "settings.h"
#include "stats.h"
#include "storage.h"
#include "text.h"
#include "tmdb.h"
#include "ui.h"

#define APP_VERSION "1.0.0"
#define DEFAULT_DATA_PATH "data/cinephilia.db"
#define PATH_CAP 1024
#define MDASH "\xE2\x80\x94"
#define MIDDOT "\xC2\xB7"
#define LQ "\xE2\x80\x9C"
#define RQ "\xE2\x80\x9D"
#define SEP "  " MIDDOT "  "

typedef struct {
    Archive archive;
    char path[PATH_CAP];
    char settings_path[PATH_CAP];
    Settings settings;
    bool muted;         /* --mute: silent for this session only */
    bool read_only;
    char read_only_reason[512];
    bool dirty;
    Stack undo;         /* DeletedFilm*, most recent on top */
    SortKey sort;
    bool sort_desc;
    FilmFilter filter;
    Rng rng;
    char today[DATE_LEN];
    int this_year;
} App;

/* ================================================================== */
/* Saving                                                             */
/* ================================================================== */

static bool app_can_write(App *app)
{
    if (!app->read_only)
        return true;
    ui_flash(UI_TONE_ERROR, "Read-only: %s", app->read_only_reason);
    return false;
}

static bool app_save(App *app)
{
    char err[512];
    if (storage_save(app->path, &app->archive, err, sizeof err)) {
        app->dirty = false;
        return true;
    }
    app->dirty = true;
    ui_flash(UI_TONE_ERROR,
             "Could not save (%s). The change is kept for now and will be saved with the next one.",
             err);
    return false;
}

static void save_settings(App *app)
{
    char err[512];
    if (!settings_save(app->settings_path, &app->settings, err, sizeof err))
        ui_flash(UI_TONE_ERROR, "Could not save settings (%s).", err);
}

/* ================================================================== */
/* Typography helpers: each returns pre-styled lines for a page       */
/* ================================================================== */

static void short_title(const Film *f, char *out, size_t cap)
{
    text_fit(out, cap, f->title, 48);
}

static void fmt_year(int year, char *out, size_t cap)
{
    if (year)
        snprintf(out, cap, "%d", year);
    else
        snprintf(out, cap, MDASH);
}

static void line_blank(StrList *l)
{
    strlist_push(l, "");
}

static void line_text(StrList *l, UiColor c, const char *text)
{
    Sb sb;
    sb_init(&sb);
    ui_sb_margin(&sb);
    ui_sb_fg(&sb, c);
    sb_fit(&sb, text, ui_content_width(), false);
    strlist_push_sb(l, &sb);
}

static void line_italic(StrList *l, UiColor c, const char *text)
{
    Sb sb;
    sb_init(&sb);
    ui_sb_margin(&sb);
    ui_sb_fg(&sb, c);
    ui_sb_italic(&sb);
    sb_fit(&sb, text, ui_content_width(), false);
    strlist_push_sb(l, &sb);
}

/* An editorial heading: tracked capitals, optionally "/ aside". */
static void line_heading(StrList *l, const char *title, const char *aside)
{
    Sb sb;
    sb_init(&sb);
    ui_sb_margin(&sb);
    ui_sb_fg(&sb, UI_IVORY);
    ui_sb_bold(&sb);
    ui_sb_tracked(&sb, title);
    ui_sb_plain(&sb);
    if (aside && *aside) {
        ui_sb_fg(&sb, UI_GREY);
        sb_puts(&sb, "   /   ");
        ui_sb_fg(&sb, UI_STONE);
        char up[128];
        text_upper(up, sizeof up, aside);
        sb_puts(&sb, up);
    }
    strlist_push_sb(l, &sb);
}

/* A small label above a group of lines. */
static void line_label(StrList *l, const char *text)
{
    Sb sb;
    sb_init(&sb);
    ui_sb_margin(&sb);
    ui_sb_fg(&sb, UI_GOLD);
    char up[128];
    text_upper(up, sizeof up, text);
    sb_puts(&sb, up);
    strlist_push_sb(l, &sb);
}

static void line_rule(StrList *l, int width)
{
    Sb sb;
    sb_init(&sb);
    ui_sb_margin(&sb);
    ui_sb_fg(&sb, UI_ASH);
    sb_repeat(&sb, "\xE2\x94\x80", width);
    strlist_push_sb(l, &sb);
}

/* Wraps text to the content width. Returns true if lines were left out. */
static bool line_wrap(StrList *l, UiColor c, int style, const char *text, int indent, int max_lines)
{
    int width = ui_content_width() - indent;
    char buf[FILM_NOTES_MAX * 2];
    const char *p = text;
    size_t n;
    int count = 0;
    while ((n = text_wrap_next(p, width, buf, sizeof buf)) > 0) {
        if (max_lines > 0 && count == max_lines) {
            Sb more;
            sb_init(&more);
            ui_sb_margin(&more);
            sb_spaces(&more, indent);
            ui_sb_fg(&more, UI_GREY);
            sb_puts(&more, "\xE2\x80\xA6");
            strlist_push_sb(l, &more);
            return true;
        }
        Sb sb;
        sb_init(&sb);
        ui_sb_margin(&sb);
        sb_spaces(&sb, indent);
        ui_sb_fg(&sb, c);
        if (style == 1)
            ui_sb_bold(&sb);
        if (style == 2)
            ui_sb_italic(&sb);
        sb_puts(&sb, buf);
        strlist_push_sb(l, &sb);
        p += n;
        count++;
    }
    return false;
}

/* A film title set large: upper case, bold, wrapped. */
static void line_title(StrList *l, const char *title, int max_lines)
{
    char up[FILM_TITLE_MAX * 2];
    text_upper(up, sizeof up, title);
    line_wrap(l, UI_IVORY, 1, up, 0, max_lines);
}

/* A catalogue-card field: small grey label, value in the given colour. */
static void line_field(StrList *l, const char *label, const char *value, UiColor c)
{
    Sb sb;
    sb_init(&sb);
    ui_sb_margin(&sb);
    ui_sb_fg(&sb, UI_GREY);
    sb_fit(&sb, label, 18, true);
    ui_sb_fg(&sb, c);
    sb_fit(&sb, value, ui_content_width() - 18, false);
    strlist_push_sb(l, &sb);
}

static void film_meta(const Film *f, char *out, size_t cap)
{
    char year[8];
    fmt_year(f->year, year, sizeof year);
    snprintf(out, cap, "%s" SEP "%s%s%s", f->director[0] ? f->director : "Director unknown",
             f->year ? year : "Year unknown", f->genre[0] ? SEP : "", f->genre);
}

static void rating_text(const Film *f, char *out, size_t cap)
{
    if (f->status == FILM_WATCHLIST)
        snprintf(out, cap, "WATCHLIST");
    else if (f->rating)
        snprintf(out, cap, "%02d/10", f->rating);
    else
        snprintf(out, cap, "SEEN");
}

/* A two-line catalogue entry:
 *   01  IN THE MOOD FOR LOVE                              09/10
 *       Wong Kar-wai  ·  2000  ·  Romance                        */
static char *catalogue_item(const Film *f, size_t number, int digits, const char *right)
{
    char up[FILM_TITLE_MAX * 2];
    char meta[FILM_DIRECTOR_MAX + FILM_GENRE_MAX + 40];
    text_upper(up, sizeof up, f->title);
    film_meta(f, meta, sizeof meta);
    Sb sb;
    sb_init(&sb);
    sb_printf(&sb, "%0*zu  %s\t%s\n%s\n", digits, number, up, right, meta);
    return sb_take(&sb);
}

static int index_digits(size_t n)
{
    return n >= 1000 ? 4 : n >= 100 ? 3 : 2;
}

/* --- menus whose items carry an action ----------------------------- */

enum {
    ACT_NONE, ACT_ADD, ACT_SEARCH, ACT_FILTER, ACT_SORT, ACT_CLEAR, ACT_UNDO,
    ACT_FILM, ACT_RANDOM, ACT_WRITE, ACT_ENTRY, ACT_AGAIN
};

typedef struct {
    StrList items;
    int *acts;
    unsigned *ids;
    size_t cap;
} Choices;

static void choices_init(Choices *c)
{
    strlist_init(&c->items);
    c->acts = NULL;
    c->ids = NULL;
    c->cap = 0;
}

static void choices_free(Choices *c)
{
    strlist_free(&c->items);
    free(c->acts);
    free(c->ids);
    choices_init(c);
}

/* Adds an item; takes ownership of `owned` when given, else copies text. */
static void choices_add(Choices *c, const char *text, char *owned, int act, unsigned id)
{
    if (c->items.count == c->cap) {
        size_t cap = c->cap ? c->cap * 2 : 16;
        int *acts = realloc(c->acts, cap * sizeof *acts);
        if (!acts) {
            free(owned);
            return;
        }
        c->acts = acts;
        unsigned *ids = realloc(c->ids, cap * sizeof *ids);
        if (!ids) {
            free(owned);
            return;
        }
        c->ids = ids;
        c->cap = cap;
    }
    bool ok;
    if (owned) {
        Sb tmp;
        sb_init(&tmp);
        sb_puts(&tmp, owned);
        free(owned);
        ok = strlist_push_sb(&c->items, &tmp);
    } else {
        ok = strlist_push(&c->items, text);
    }
    if (ok) {
        c->acts[c->items.count - 1] = act;
        c->ids[c->items.count - 1] = id;
    }
}

/* ================================================================== */
/* Journal helpers                                                    */
/* ================================================================== */

static int entry_newest_first(const void *pa, const void *pb)
{
    const JournalEntry *a = *(const JournalEntry *const *)pa;
    const JournalEntry *b = *(const JournalEntry *const *)pb;
    int c = strcmp(b->date, a->date);
    if (c != 0)
        return c;
    return (b->id > a->id) - (b->id < a->id);
}

/* Entries for one film (film_id != 0) or all of them, newest first. */
static size_t collect_entries(const Archive *a, unsigned film_id, const JournalEntry ***out)
{
    *out = NULL;
    size_t n = 0;
    for (size_t i = 0; i < a->journal_count; i++)
        if (!film_id || a->journal[i].film_id == film_id)
            n++;
    if (n == 0)
        return 0;
    const JournalEntry **arr = malloc(n * sizeof *arr);
    if (!arr)
        return 0;
    size_t k = 0;
    for (size_t i = 0; i < a->journal_count; i++)
        if (!film_id || a->journal[i].film_id == film_id)
            arr[k++] = &a->journal[i];
    qsort(arr, n, sizeof *arr, entry_newest_first);
    *out = arr;
    return n;
}

static void entry_line(StrList *l, const JournalEntry *e)
{
    char date[24];
    date_pretty(e->date, date, sizeof date);
    Sb sb;
    sb_init(&sb);
    ui_sb_margin(&sb);
    ui_sb_fg(&sb, UI_GREY);
    sb_fit(&sb, date, 13, true);
    ui_sb_fg(&sb, e->text[0] ? UI_STONE : UI_GREY);
    ui_sb_italic(&sb);
    sb_fit(&sb, e->text[0] ? e->text : "no note", ui_content_width() - 13, false);
    strlist_push_sb(l, &sb);
}

/* ================================================================== */
/* Flows: add, log, edit, delete                                      */
/* ================================================================== */

static void normalize_genre(const App *app, char *genre)
{
    if (!genre[0])
        return;
    for (const Film *f = app->archive.head; f; f = f->next) {
        if (f->genre[0] && text_casecmp(f->genre, genre) == 0) {
            text_copy(genre, FILM_GENRE_MAX, f->genre); /* reuse the existing spelling */
            return;
        }
    }
    if (genre[0] >= 'a' && genre[0] <= 'z')
        genre[0] = (char)(genre[0] - 32);
}

static void genre_hint(const App *app, char *out, size_t cap)
{
    Tally t;
    tally_init(&t);
    for (const Film *f = app->archive.head; f; f = f->next)
        if (f->genre[0])
            tally_add(&t, f->genre, 0);
    tally_sort_by_count(&t);
    if (t.count == 0) {
        snprintf(out, cap, "optional " MIDDOT " e.g. Drama, Comedy, Documentary");
    } else {
        Sb sb;
        sb_init(&sb);
        sb_puts(&sb, "optional " MIDDOT " in your archive: ");
        for (size_t i = 0; i < t.count && i < 4; i++)
            sb_printf(&sb, "%s%s", i ? ", " : "", t.items[i].key);
        text_copy(out, cap, sb_str(&sb));
        sb_free(&sb);
    }
    tally_free(&t);
}

/* Records a viewing of film `id`: date, rating and a few words. */
static void flow_log_viewing(App *app, unsigned id)
{
    if (!app_can_write(app))
        return;
    Film *f = archive_find(&app->archive, id);
    if (!f)
        return;
    bool first = f->status == FILM_WATCHLIST;
    char title[200];
    short_title(f, title, sizeof title);
    ui_form_begin("Cinema journal", f->title,
                  "A new page in the journal. Press Enter after each answer; "
                  "an empty date means today.");

    char date[DATE_LEN];
    PromptResult r = ui_prompt_date("Date watched", "YYYY-MM-DD " MIDDOT " blank for today", NULL,
                                    app->today, date);
    if (r == PROMPT_CANCEL)
        goto cancelled;
    if (r != PROMPT_OK)
        text_copy(date, sizeof date, app->today);

    char current[16] = "";
    if (f->rating)
        snprintf(current, sizeof current, "%02d/10", f->rating);
    int rating = 0;
    bool clear_rating = false;
    r = ui_prompt_int("Personal rating",
                      f->rating ? "1-10 " MIDDOT " blank keeps it " MIDDOT " '-' removes it"
                                : "1-10 " MIDDOT " blank leaves it unrated",
                      current, RATING_MIN, RATING_MAX, &rating);
    if (r == PROMPT_CANCEL)
        goto cancelled;
    if (r == PROMPT_CLEAR)
        clear_rating = true;
    if (r != PROMPT_OK)
        rating = 0;

    char note[FILM_NOTES_MAX];
    r = ui_prompt("A few words", "optional " MIDDOT " what stayed with you", NULL, note, sizeof note);
    if (r == PROMPT_CANCEL)
        goto cancelled;
    if (r != PROMPT_OK)
        note[0] = '\0';

    if (film_record_viewing(&app->archive, f, date, rating, note) == 0) {
        ui_flash(UI_TONE_ERROR, "Out of memory: the viewing was not logged.");
        return;
    }
    if (clear_rating)
        f->rating = 0;
    char pretty[24];
    date_pretty(date, pretty, sizeof pretty);
    if (app_save(app)) {
        audio_play(SOUND_CONFIRM);
        ui_flash(UI_TONE_OK, first ? "Marked " LQ "%s" RQ " as watched on %s."
                                   : "Logged a viewing of " LQ "%s" RQ " on %s.",
                 title, pretty);
    }
    return;

cancelled:
    ui_flash(UI_TONE_INFO, "Nothing was logged.");
}

/* Offers to fill in director, year and genre from TMDB after the title
 * has been typed. Returns true if the user accepted TMDB's details, which
 * are then copied into `draft`. Only offered when TMDB_API_KEY is set. */
static bool offer_tmdb(Film *draft)
{
    if (!tmdb_available())
        return false;
    if (!ui_confirm("Look this film up on TMDB?", true))
        return false;
    ui_say(UI_TONE_INFO, "Asking TMDB\xE2\x80\xA6");
    TmdbFilm found;
    TmdbResult r = tmdb_lookup(draft->title, 0, &found);
    if (r != TMDB_OK) {
        ui_say(UI_TONE_WARN, "%s You can enter the details yourself.", tmdb_result_text(r));
        return false;
    }
    char year[8];
    fmt_year(found.year, year, sizeof year);
    ui_say(UI_TONE_OK, "%s (%s)%s%s", found.title, year, found.director[0] ? ", directed by " : "",
           found.director);
    if (found.genre[0])
        ui_say(UI_TONE_INFO, "Genre: %s", found.genre);
    if (found.synopsis[0]) {
        char shortened[400];
        text_fit(shortened, sizeof shortened, found.synopsis, 220);
        ui_say(UI_TONE_INFO, "%s", shortened);
    }
    ui_say(UI_TONE_INFO, "%s", TMDB_NOTICE);
    if (!ui_confirm("Use these details?", true))
        return false;
    text_copy(draft->title, sizeof draft->title, found.title);
    text_copy(draft->director, sizeof draft->director, found.director);
    text_copy(draft->genre, sizeof draft->genre, found.genre);
    draft->year = found.year;
    return true;
}

/* preset: -1 to ask, or FILM_WATCHED / FILM_WATCHLIST. prefill: an initial
 * title (from a search), or NULL. Returns the new id, or 0. */
static unsigned flow_add_film(App *app, int preset, const char *prefill)
{
    if (!app_can_write(app))
        return 0;
    ui_form_begin("Add a film", preset == FILM_WATCHLIST ? "Something you mean to see" : "A new record",
                  "Enter each detail and press Enter. Only the title is required; leave it "
                  "empty to cancel. In optional fields, '-' leaves a value out.");
    Film draft;
    memset(&draft, 0, sizeof draft);
    char hint[160];
    PromptResult r;

    if (prefill && *prefill) {
        r = ui_prompt("Title", "blank keeps the title shown", prefill, draft.title, sizeof draft.title);
        if (r == PROMPT_BLANK) {
            text_copy(draft.title, sizeof draft.title, prefill);
            r = PROMPT_OK;
        }
    } else {
        r = ui_prompt("Title", "required", NULL, draft.title, sizeof draft.title);
    }
    if (r != PROMPT_OK)
        goto cancelled;
    bool from_tmdb = offer_tmdb(&draft);
    if (ui_input_closed())
        goto cancelled;
    if (!from_tmdb) {
        r = ui_prompt("Director", "optional", NULL, draft.director, sizeof draft.director);
        if (r == PROMPT_CANCEL)
            goto cancelled;
        if (strcmp(draft.director, "-") == 0)
            draft.director[0] = '\0';

        snprintf(hint, sizeof hint, "%d-%d " MIDDOT " blank if unknown", FILM_YEAR_MIN, app->this_year + 10);
        int year = 0;
        r = ui_prompt_int("Release year", hint, NULL, FILM_YEAR_MIN, app->this_year + 10, &year);
        if (r == PROMPT_CANCEL)
            goto cancelled;
        draft.year = r == PROMPT_OK ? year : 0;
    }

    const Film *dup = archive_find_duplicate(&app->archive, draft.title, draft.year, NULL);
    if (dup) {
        char y[8];
        fmt_year(dup->year, y, sizeof y);
        ui_say(UI_TONE_WARN, LQ "%s" RQ " (%s) is already in the archive.", dup->title, y);
        if (!ui_confirm("Add it anyway?", false))
            goto cancelled;
    }

    if (!from_tmdb) {
        genre_hint(app, hint, sizeof hint);
        r = ui_prompt("Genre", hint, NULL, draft.genre, sizeof draft.genre);
        if (r == PROMPT_CANCEL)
            goto cancelled;
        if (strcmp(draft.genre, "-") == 0)
            draft.genre[0] = '\0';
    }
    normalize_genre(app, draft.genre);

    bool watched;
    if (preset < 0) {
        watched = ui_confirm("Have you already seen it?", true);
        if (ui_input_closed())
            goto cancelled;
    } else {
        watched = preset == FILM_WATCHED;
    }

    char date[DATE_LEN] = "";
    int rating = 0;
    if (watched) {
        r = ui_prompt_date("Date watched",
                           "YYYY-MM-DD " MIDDOT " blank for today " MIDDOT " '-' if you don't remember",
                           NULL, app->today, date);
        if (r == PROMPT_CANCEL)
            goto cancelled;
        if (r == PROMPT_BLANK)
            text_copy(date, sizeof date, app->today);
        if (r == PROMPT_CLEAR)
            date[0] = '\0';
        r = ui_prompt_int("Personal rating", "1-10 " MIDDOT " blank leaves it unrated", NULL, RATING_MIN,
                          RATING_MAX, &rating);
        if (r == PROMPT_CANCEL)
            goto cancelled;
        if (r != PROMPT_OK)
            rating = 0;
        r = ui_prompt("Notes", "optional " MIDDOT " what stayed with you", NULL, draft.notes,
                      sizeof draft.notes);
    } else {
        r = ui_prompt("Why this one?", "optional " MIDDOT " who recommended it, what you've heard", NULL,
                      draft.notes, sizeof draft.notes);
    }
    if (r == PROMPT_CANCEL)
        goto cancelled;
    if (strcmp(draft.notes, "-") == 0)
        draft.notes[0] = '\0';

    Film *f = film_new();
    if (!f) {
        ui_flash(UI_TONE_ERROR, "Out of memory: the film was not added.");
        return 0;
    }
    memcpy(f->title, draft.title, sizeof f->title);
    memcpy(f->director, draft.director, sizeof f->director);
    memcpy(f->genre, draft.genre, sizeof f->genre);
    memcpy(f->notes, draft.notes, sizeof f->notes);
    f->year = draft.year;
    f->status = FILM_WATCHLIST;
    text_copy(f->added_on, sizeof f->added_on, app->today);
    char err[128];
    if (!film_validate(f, err, sizeof err) || !archive_append(&app->archive, f)) {
        free(f);
        ui_flash(UI_TONE_ERROR, "The film could not be added.");
        return 0;
    }
    if (watched) {
        if (date[0]) {
            if (film_record_viewing(&app->archive, f, date, rating, "") == 0)
                ui_flash(UI_TONE_WARN, "Added, but the viewing could not be logged.");
        } else {
            f->status = FILM_WATCHED;
            f->rating = rating;
        }
    }
    unsigned id = f->id;
    char title[200];
    short_title(f, title, sizeof title);
    if (app_save(app)) {
        audio_play(SOUND_FILED);
        ui_flash(UI_TONE_OK, watched ? "Filed " LQ "%s" RQ " in the collection."
                                     : "Added " LQ "%s" RQ " to your watchlist.",
                 title);
    }
    return id;

cancelled:
    ui_flash(UI_TONE_INFO, "Nothing was added.");
    return 0;
}

/* Returns true if the film was deleted. */
static bool flow_delete(App *app, unsigned id)
{
    if (!app_can_write(app))
        return false;
    Film *f = archive_find(&app->archive, id);
    if (!f)
        return true;
    char title[200];
    short_title(f, title, sizeof title);
    size_t n = archive_entry_count(&app->archive, id);
    ui_form_begin("Delete", f->title,
                  "Deleting removes the film and its journal entries. You can undo it from "
                  "The Collection until you close Cinephilia.");
    char question[320];
    if (n)
        snprintf(question, sizeof question, "Delete " LQ "%s" RQ " and its %zu journal entr%s?", title, n,
                 n == 1 ? "y" : "ies");
    else
        snprintf(question, sizeof question, "Delete " LQ "%s" RQ "?", title);
    if (!ui_confirm(question, false)) {
        ui_flash(UI_TONE_INFO, "Kept " LQ "%s" RQ ".", title);
        return false;
    }
    DeletedFilm *d = archive_delete_film(&app->archive, f);
    bool undoable = d && stack_push(&app->undo, d);
    if (d && !undoable)
        deleted_film_free(d);
    if (app_save(app)) {
        audio_play(SOUND_DELETE);
        ui_flash(UI_TONE_OK,
                 undoable ? "Deleted " LQ "%s" RQ ". Undo is in The Collection until you close Cinephilia."
                          : "Deleted " LQ "%s" RQ ".",
                 title);
    }
    return true;
}

static void flow_undo(App *app)
{
    if (!app_can_write(app))
        return;
    DeletedFilm *d = stack_pop(&app->undo);
    if (!d)
        return;
    char title[200];
    short_title(d->film, title, sizeof title);
    bool ok = archive_restore(&app->archive, d);
    if (app_save(app)) {
        audio_play(SOUND_FILED);
        ui_flash(ok ? UI_TONE_OK : UI_TONE_WARN,
                 ok ? "Restored " LQ "%s" RQ "."
                    : "Restored " LQ "%s" RQ ", but some journal entries could not be recovered.",
                 title);
    }
}

enum {
    FIELD_TITLE, FIELD_DIRECTOR, FIELD_YEAR, FIELD_GENRE, FIELD_RATING, FIELD_NOTES,
    FIELD_STATUS, FIELD_LAST_SEEN, FIELD_COUNT
};

static void edit_field(App *app, unsigned id, int field)
{
    if (!app_can_write(app))
        return;
    Film *f = archive_find(&app->archive, id);
    if (!f)
        return;
    if (field == FIELD_RATING && f->status == FILM_WATCHLIST) {
        ui_flash(UI_TONE_INFO, "Rate it once you've seen it " MDASH " choose Mark as watched.");
        return;
    }
    if (field == FIELD_LAST_SEEN && f->status == FILM_WATCHLIST) {
        ui_flash(UI_TONE_INFO, "It's still on your watchlist " MDASH " choose Mark as watched.");
        return;
    }
    if (field == FIELD_STATUS && f->status == FILM_WATCHLIST) {
        flow_log_viewing(app, id);
        return;
    }

    ui_form_begin("Edit", f->title, "Press Enter on an empty line to keep the current value.");
    char buf[FILM_NOTES_MAX];
    char current[16];
    int number = 0;
    bool changed = false;
    PromptResult r;
    const char *optional_hint = "blank keeps it " MIDDOT " '-' clears it";

    switch (field) {
    case FIELD_TITLE:
        r = ui_prompt("Title", "blank keeps the current title", f->title, buf, FILM_TITLE_MAX);
        if (r != PROMPT_OK)
            break;
        if (archive_find_duplicate(&app->archive, buf, f->year, f)) {
            ui_say(UI_TONE_WARN, "Another film with that title and year is already archived.");
            if (!ui_confirm("Use this title anyway?", false))
                break;
        }
        text_copy(f->title, sizeof f->title, buf);
        changed = true;
        break;
    case FIELD_DIRECTOR:
    case FIELD_GENRE:
    case FIELD_NOTES: {
        char *dst = field == FIELD_DIRECTOR ? f->director : field == FIELD_GENRE ? f->genre : f->notes;
        size_t cap = field == FIELD_DIRECTOR ? sizeof f->director
                     : field == FIELD_GENRE  ? sizeof f->genre
                                             : sizeof f->notes;
        const char *label = field == FIELD_DIRECTOR ? "Director" : field == FIELD_GENRE ? "Genre" : "Notes";
        r = ui_prompt(label, optional_hint, dst, buf, cap);
        if (r != PROMPT_OK)
            break;
        if (strcmp(buf, "-") == 0)
            buf[0] = '\0';
        if (field == FIELD_GENRE)
            normalize_genre(app, buf);
        text_copy(dst, cap, buf);
        changed = true;
        break;
    }
    case FIELD_YEAR: {
        char hint[96];
        snprintf(hint, sizeof hint, "%d-%d " MIDDOT " %s", FILM_YEAR_MIN, app->this_year + 10, optional_hint);
        if (f->year)
            snprintf(current, sizeof current, "%d", f->year);
        else
            current[0] = '\0';
        r = ui_prompt_int("Release year", hint, current, FILM_YEAR_MIN, app->this_year + 10, &number);
        if (r == PROMPT_OK || r == PROMPT_CLEAR) {
            f->year = r == PROMPT_OK ? number : 0;
            changed = true;
        }
        break;
    }
    case FIELD_RATING:
        if (f->rating)
            snprintf(current, sizeof current, "%02d/10", f->rating);
        else
            current[0] = '\0';
        r = ui_prompt_int("Personal rating", "1-10 " MIDDOT " blank keeps it " MIDDOT " '-' removes it",
                          current, RATING_MIN, RATING_MAX, &number);
        if (r == PROMPT_OK || r == PROMPT_CLEAR) {
            f->rating = r == PROMPT_OK ? number : 0;
            changed = true;
        }
        break;
    case FIELD_STATUS:
        if (ui_confirm("Return it to your watchlist? Its journal entries are kept.", false)) {
            f->status = FILM_WATCHLIST;
            changed = true;
        }
        break;
    case FIELD_LAST_SEEN: {
        char date[DATE_LEN];
        r = ui_prompt_date("Last seen", "YYYY-MM-DD " MIDDOT " '-' clears it", f->watched_on, app->today, date);
        if (r == PROMPT_OK || r == PROMPT_CLEAR) {
            text_copy(f->watched_on, sizeof f->watched_on, r == PROMPT_OK ? date : "");
            changed = true;
        }
        break;
    }
    default:
        break;
    }

    if (changed && app_save(app)) {
        audio_play(SOUND_CONFIRM);
        ui_flash(UI_TONE_OK, "Saved.");
    } else if (!changed) {
        ui_flash(UI_TONE_INFO, "Nothing was changed.");
    }
}

static void screen_edit(App *app, unsigned id)
{
    int sel = 0;
    while (!ui_input_closed()) {
        Film *f = archive_find(&app->archive, id);
        if (!f)
            return;
        char rows[FIELD_COUNT][FILM_NOTES_MAX + 32];
        char rating[16];
        rating_text(f, rating, sizeof rating);
        snprintf(rows[FIELD_TITLE], sizeof rows[0], "TITLE\t%s", f->title);
        snprintf(rows[FIELD_DIRECTOR], sizeof rows[0], "DIRECTOR\t%s", f->director[0] ? f->director : MDASH);
        if (f->year)
            snprintf(rows[FIELD_YEAR], sizeof rows[0], "RELEASE YEAR\t%d", f->year);
        else
            snprintf(rows[FIELD_YEAR], sizeof rows[0], "RELEASE YEAR\tunknown");
        snprintf(rows[FIELD_GENRE], sizeof rows[0], "GENRE\t%s", f->genre[0] ? f->genre : MDASH);
        snprintf(rows[FIELD_RATING], sizeof rows[0], "PERSONAL RATING\t%s",
                 f->status == FILM_WATCHLIST ? "not seen yet" : f->rating ? rating : "unrated");
        snprintf(rows[FIELD_NOTES], sizeof rows[0], "NOTES\t%s", f->notes[0] ? f->notes : MDASH);
        snprintf(rows[FIELD_STATUS], sizeof rows[0], "STATUS\t%s",
                 f->status == FILM_WATCHED ? "watched" : "on the watchlist");
        snprintf(rows[FIELD_LAST_SEEN], sizeof rows[0], "LAST SEEN\t%s", f->watched_on[0] ? f->watched_on : MDASH);
        const char *items[FIELD_COUNT];
        for (int i = 0; i < FIELD_COUNT; i++)
            items[i] = rows[i];

        StrList pre;
        strlist_init(&pre);
        line_title(&pre, f->title, 2);
        line_italic(&pre, UI_GREY, "Choose a detail to change. Each change is saved at once.");
        line_blank(&pre);
        UiMenu m = {.section = "Edit", .preamble = &pre, .items = items, .count = FIELD_COUNT,
                    .selected = sel, .back_label = "Done"};
        int c = ui_menu(&m);
        strlist_free(&pre);
        if (c == UI_BACK)
            return;
        sel = c;
        edit_field(app, id, c);
    }
}

/* ================================================================== */
/* Film detail                                                        */
/* ================================================================== */

static void screen_entry(App *app, unsigned entry_id);

static void screen_notes(App *app, unsigned id)
{
    Film *f = archive_find(&app->archive, id);
    if (!f)
        return;
    StrList lines;
    strlist_init(&lines);
    line_title(&lines, f->title, 3);
    line_rule(&lines, 12);
    line_blank(&lines);
    line_wrap(&lines, UI_IVORY, 2, f->notes, 0, 0);
    ui_page("Notes", &lines);
    strlist_free(&lines);
}

static void screen_film_journal(App *app, unsigned id)
{
    int sel = 0;
    while (!ui_input_closed()) {
        Film *f = archive_find(&app->archive, id);
        if (!f)
            return;
        const JournalEntry **es;
        size_t n = collect_entries(&app->archive, id, &es);
        if (n == 0)
            return;
        Choices c;
        choices_init(&c);
        for (size_t i = 0; i < n; i++) {
            char date[24];
            date_pretty(es[i]->date, date, sizeof date);
            char up[24];
            text_upper(up, sizeof up, date);
            Sb sb;
            sb_init(&sb);
            sb_printf(&sb, "%s\n\x0e%s\n", up, es[i]->text[0] ? es[i]->text : "no note");
            choices_add(&c, NULL, sb_take(&sb), ACT_ENTRY, es[i]->id);
        }
        free(es);
        StrList pre;
        strlist_init(&pre);
        line_title(&pre, f->title, 2);
        char summary[64];
        snprintf(summary, sizeof summary, "%zu viewing%s in the journal", n, n == 1 ? "" : "s");
        line_text(&pre, UI_STONE, summary);
        line_blank(&pre);
        UiMenu m = {.section = "Cinema journal", .preamble = &pre,
                    .items = (const char *const *)c.items.items, .count = (int)c.items.count,
                    .selected = sel, .back_label = "Back"};
        int k = ui_menu(&m);
        unsigned eid = k >= 0 ? c.ids[k] : 0;
        strlist_free(&pre);
        choices_free(&c);
        if (k == UI_BACK)
            return;
        sel = k;
        screen_entry(app, eid);
    }
}

static void flow_return_to_watchlist(App *app, unsigned id)
{
    if (!app_can_write(app))
        return;
    Film *f = archive_find(&app->archive, id);
    if (!f)
        return;
    f->status = FILM_WATCHLIST;
    char title[200];
    short_title(f, title, sizeof title);
    if (app_save(app)) {
        audio_play(SOUND_CONFIRM);
        ui_flash(UI_TONE_OK, LQ "%s" RQ " is back on your watchlist. Its journal is kept.", title);
    }
}

/* The film as a catalogue card. Returns true if the notes were cut short. */
static bool film_card(StrList *l, const App *app, const Film *f, int note_lines)
{
    char buf[256];
    char date[24];
    line_title(l, f->title, 3);
    line_rule(l, 12);
    line_blank(l);
    line_field(l, "DIRECTOR", f->director[0] ? f->director : "Unknown", UI_IVORY);
    fmt_year(f->year, buf, sizeof buf);
    line_field(l, "RELEASE YEAR", f->year ? buf : "Unknown", UI_IVORY);
    line_field(l, "GENRE", f->genre[0] ? f->genre : MDASH, UI_IVORY);
    if (f->status == FILM_WATCHED) {
        if (f->rating)
            snprintf(buf, sizeof buf, "%02d/10", f->rating);
        line_field(l, "PERSONAL RATING", f->rating ? buf : "Not rated", f->rating ? UI_GOLD : UI_STONE);
        size_t views = archive_entry_count(&app->archive, f->id);
        Sb sb;
        sb_init(&sb);
        sb_puts(&sb, "Watched");
        if (f->watched_on[0]) {
            date_pretty(f->watched_on, date, sizeof date);
            sb_printf(&sb, SEP "last seen %s", date);
        }
        if (views > 1)
            sb_printf(&sb, SEP "%zu viewings", views);
        line_field(l, "STATUS", sb_str(&sb), UI_IVORY);
        sb_free(&sb);
    } else {
        line_field(l, "PERSONAL RATING", "Not seen yet", UI_STONE);
        if (f->added_on[0]) {
            date_pretty(f->added_on, date, sizeof date);
            snprintf(buf, sizeof buf, "On the watchlist" SEP "since %s", date);
        } else {
            snprintf(buf, sizeof buf, "On the watchlist");
        }
        line_field(l, "STATUS", buf, UI_RED);
    }
    bool cut = false;
    if (f->notes[0]) {
        line_blank(l);
        line_label(l, "Notes");
        cut = line_wrap(l, UI_IVORY, 2, f->notes, 0, note_lines);
    }
    return cut;
}

static void screen_film(App *app, unsigned id)
{
    enum { A_LOG, A_EDIT, A_RETURN, A_NOTES, A_JOURNAL, A_DELETE };
    int sel = 0;
    while (!ui_input_closed()) {
        Film *f = archive_find(&app->archive, id);
        if (!f)
            return;
        StrList pre;
        strlist_init(&pre);
        bool cut = film_card(&pre, app, f, 6);

        const JournalEntry **es;
        size_t views = collect_entries(&app->archive, id, &es);
        if (views) {
            line_blank(&pre);
            line_label(&pre, "From the journal");
            for (size_t i = 0; i < views && i < 3; i++)
                entry_line(&pre, es[i]);
            free(es);
        }
        line_blank(&pre);

        const char *items[6];
        int acts[6];
        int n = 0;
        char journal_item[64];
        items[n] = f->status == FILM_WATCHED ? "LOG A VIEWING\tdate, rating, a few words"
                                             : "MARK AS WATCHED\tlog the date and your rating";
        acts[n++] = A_LOG;
        items[n] = "EDIT DETAILS";
        acts[n++] = A_EDIT;
        if (f->status == FILM_WATCHED) {
            items[n] = "RETURN TO THE WATCHLIST\tto see it again";
            acts[n++] = A_RETURN;
        }
        if (cut) {
            items[n] = "READ THE FULL NOTES";
            acts[n++] = A_NOTES;
        }
        if (views) {
            snprintf(journal_item, sizeof journal_item, "JOURNAL ENTRIES\t%zu", views);
            items[n] = journal_item;
            acts[n++] = A_JOURNAL;
        }
        items[n] = "DELETE THIS FILM";
        acts[n++] = A_DELETE;

        UiMenu m = {.section = "Catalogue card", .preamble = &pre, .items = items, .count = n,
                    .selected = sel, .back_label = "Back"};
        int c = ui_menu(&m);
        strlist_free(&pre);
        if (c == UI_BACK)
            return;
        sel = c;
        switch (acts[c]) {
        case A_LOG: flow_log_viewing(app, id); break;
        case A_EDIT: screen_edit(app, id); break;
        case A_RETURN: flow_return_to_watchlist(app, id); sel = 0; break;
        case A_NOTES: screen_notes(app, id); break;
        case A_JOURNAL: screen_film_journal(app, id); break;
        case A_DELETE:
            if (flow_delete(app, id))
                return;
            break;
        default: break;
        }
    }
}

/* ================================================================== */
/* The collection: browse, filter, sort                               */
/* ================================================================== */

static void sort_label(const App *app, char *out, size_t cap)
{
    const char *dir;
    switch (app->sort) {
    case SORT_TITLE:
    case SORT_DIRECTOR: dir = app->sort_desc ? "Z to A" : "A to Z"; break;
    case SORT_YEAR: dir = app->sort_desc ? "newest first" : "oldest first"; break;
    case SORT_RATING: dir = app->sort_desc ? "highest first" : "lowest first"; break;
    default: dir = app->sort_desc ? "newest first" : "oldest first"; break;
    }
    snprintf(out, cap, "%s, %s", sort_key_name(app->sort), dir);
}

static void flow_refine_search(App *app)
{
    ui_form_begin("The collection", "Search", "Any part of a title or a director's name.");
    char q[FILM_TITLE_MAX];
    PromptResult r = ui_prompt("Look for", "blank clears the search", app->filter.query, q, sizeof q);
    if (r == PROMPT_OK)
        text_copy(app->filter.query, sizeof app->filter.query, q);
    else if (r == PROMPT_BLANK)
        app->filter.query[0] = '\0';
}

static void screen_sort(App *app)
{
    const char *items[] = {"TITLE\tA to Z, setting aside The / A / An", "DIRECTOR\tA to Z",
                           "RELEASE YEAR\toldest first", "PERSONAL RATING\thighest first",
                           "DATE ADDED\tnewest first", "", "REVERSE THE ORDER"};
    static const SortKey keys[] = {SORT_TITLE, SORT_DIRECTOR, SORT_YEAR, SORT_RATING, SORT_ADDED};
    StrList pre;
    strlist_init(&pre);
    char label[64];
    char line[96];
    sort_label(app, label, sizeof label);
    line_heading(&pre, "Order", NULL);
    snprintf(line, sizeof line, "Currently by %s.", label);
    line_italic(&pre, UI_STONE, line);
    line_blank(&pre);
    UiMenu m = {.section = "The collection", .preamble = &pre, .items = items, .count = 7,
                .selected = (int)app->sort, .back_label = "Back"};
    int c = ui_menu(&m);
    strlist_free(&pre);
    if (c >= 0 && c < 5) {
        app->sort = keys[c];
        app->sort_desc = sort_default_descending(app->sort);
    } else if (c == 6) {
        app->sort_desc = !app->sort_desc;
    }
}

static void pick_genre(App *app)
{
    Tally t;
    tally_init(&t);
    for (const Film *f = app->archive.head; f; f = f->next)
        if (f->genre[0])
            tally_add(&t, f->genre, 0);
    tally_sort_by_count(&t);
    Choices c;
    choices_init(&c);
    choices_add(&c, "ANY GENRE", NULL, ACT_NONE, 0);
    for (size_t i = 0; i < t.count; i++) {
        Sb sb;
        sb_init(&sb);
        sb_printf(&sb, "%s\t%d film%s", t.items[i].key, t.items[i].count, t.items[i].count == 1 ? "" : "s");
        choices_add(&c, NULL, sb_take(&sb), ACT_FILM, (unsigned)i);
    }
    UiMenu m = {.section = "Filter", .items = (const char *const *)c.items.items,
                .count = (int)c.items.count, .back_label = "Back"};
    int k = ui_menu(&m);
    if (k == 0)
        app->filter.genre[0] = '\0';
    else if (k > 0)
        text_copy(app->filter.genre, sizeof app->filter.genre, t.items[c.ids[k]].key);
    choices_free(&c);
    tally_free(&t);
}

static void screen_filter(App *app)
{
    int sel = 0;
    while (!ui_input_closed()) {
        FilmFilter *fl = &app->filter;
        char rows[4][96];
        snprintf(rows[0], sizeof rows[0], "GENRE\t%s", fl->genre[0] ? fl->genre : "any");
        if (fl->min_rating)
            snprintf(rows[1], sizeof rows[1], "MINIMUM RATING\t%d/10 or higher", fl->min_rating);
        else
            snprintf(rows[1], sizeof rows[1], "MINIMUM RATING\tany");
        if (fl->year_from && fl->year_to)
            snprintf(rows[2], sizeof rows[2], "RELEASE YEARS\t%d-%d", fl->year_from, fl->year_to);
        else if (fl->year_from)
            snprintf(rows[2], sizeof rows[2], "RELEASE YEARS\tfrom %d", fl->year_from);
        else if (fl->year_to)
            snprintf(rows[2], sizeof rows[2], "RELEASE YEARS\tup to %d", fl->year_to);
        else
            snprintf(rows[2], sizeof rows[2], "RELEASE YEARS\tany");
        snprintf(rows[3], sizeof rows[3], "STATUS\t%s",
                 fl->status < 0 ? "all films" : fl->status == FILM_WATCHED ? "watched" : "watchlist");
        const char *items[] = {rows[0], rows[1], rows[2], rows[3], "", "CLEAR ALL FILTERS"};
        StrList pre;
        strlist_init(&pre);
        line_heading(&pre, "Filter", NULL);
        line_italic(&pre, UI_STONE, "Narrow the collection. Filters stay until you clear them.");
        line_blank(&pre);
        UiMenu m = {.section = "The collection", .preamble = &pre, .items = items, .count = 6,
                    .selected = sel, .back_label = "Done"};
        int c = ui_menu(&m);
        strlist_free(&pre);
        if (c == UI_BACK)
            return;
        sel = c;
        int v = 0;
        PromptResult r;
        switch (c) {
        case 0:
            pick_genre(app);
            break;
        case 1:
            ui_form_begin("Filter", "Minimum rating", NULL);
            r = ui_prompt_int("Show films rated at least", "1-10 " MIDDOT " blank for any", NULL,
                              RATING_MIN, RATING_MAX, &v);
            if (r != PROMPT_CANCEL)
                fl->min_rating = r == PROMPT_OK ? v : 0;
            break;
        case 2: {
            ui_form_begin("Filter", "Release years", NULL);
            int from = 0, to = 0;
            r = ui_prompt_int("From", "blank for no lower limit", NULL, FILM_YEAR_MIN, FILM_YEAR_MAX, &from);
            if (r == PROMPT_CANCEL)
                break;
            if (r != PROMPT_OK)
                from = 0;
            r = ui_prompt_int("To", "blank for no upper limit", NULL, FILM_YEAR_MIN, FILM_YEAR_MAX, &to);
            if (r == PROMPT_CANCEL)
                break;
            if (r != PROMPT_OK)
                to = 0;
            if (from && to && from > to) { /* accept a reversed range */
                int t = from;
                from = to;
                to = t;
            }
            fl->year_from = from;
            fl->year_to = to;
            break;
        }
        case 3: {
            const char *opts[] = {"ALL FILMS", "WATCHED", "ON THE WATCHLIST"};
            UiMenu sm = {.section = "Filter", .items = opts, .count = 3, .selected = fl->status + 1,
                         .back_label = "Back"};
            int s = ui_menu(&sm);
            if (s >= 0)
                fl->status = s - 1;
            break;
        }
        case 5: {
            char q[FILM_TITLE_MAX];
            text_copy(q, sizeof q, fl->query);
            film_filter_init(fl);
            text_copy(fl->query, sizeof fl->query, q); /* the search is cleared on its own */
            ui_flash(UI_TONE_OK, "Filters cleared.");
            return;
        }
        default:
            break;
        }
    }
}

static void screen_collection(App *app)
{
    int sel = -1;
    while (!ui_input_closed()) {
        archive_sort(&app->archive, app->sort, app->sort_desc);
        const Film **films = NULL;
        size_t n = 0;
        if (!archive_collect(&app->archive, &app->filter, &films, &n)) {
            ui_flash(UI_TONE_ERROR, "Out of memory.");
            return;
        }

        Choices c;
        choices_init(&c);
        Sb sb;
        sb_init(&sb);
        char desc[256];
        char label[64];
        size_t total = app->archive.count;

        choices_add(&c, "ADD A FILM\ta new record", NULL, ACT_ADD, 0);
        if (total > 0) {
            if (app->filter.query[0])
                sb_printf(&sb, "SEARCH\t" LQ "%s" RQ, app->filter.query);
            else
                sb_puts(&sb, "SEARCH\ttitles and directors");
            choices_add(&c, NULL, sb_take(&sb), ACT_SEARCH, 0);
            FilmFilter only = app->filter;
            only.query[0] = '\0';
            film_filter_describe(&only, desc, sizeof desc);
            sb_printf(&sb, "FILTER\t%s", desc[0] ? desc : "genre, rating, years, status");
            choices_add(&c, NULL, sb_take(&sb), ACT_FILTER, 0);
            sort_label(app, label, sizeof label);
            sb_printf(&sb, "ORDER\t%s", label);
            choices_add(&c, NULL, sb_take(&sb), ACT_SORT, 0);
            if (film_filter_active(&app->filter))
                choices_add(&c, "SHOW EVERY FILM\tclear search and filters", NULL, ACT_CLEAR, 0);
        }
        const DeletedFilm *last = stack_peek(&app->undo);
        if (last) {
            sb_printf(&sb, "UNDO DELETE\t%s", last->film->title);
            choices_add(&c, NULL, sb_take(&sb), ACT_UNDO, 0);
        }
        int first_film = -1;
        if (n > 0) {
            choices_add(&c, "", NULL, ACT_NONE, 0);
            first_film = (int)c.items.count;
            int digits = index_digits(n);
            for (size_t i = 0; i < n; i++) {
                char right[16];
                rating_text(films[i], right, sizeof right);
                choices_add(&c, NULL, catalogue_item(films[i], i + 1, digits, right), ACT_FILM, films[i]->id);
            }
        }
        free(films);

        StrList pre;
        strlist_init(&pre);
        char aside[48];
        snprintf(aside, sizeof aside, "%03zu film%s", total, total == 1 ? "" : "s");
        line_heading(&pre, "The collection", aside);
        if (total == 0) {
            line_blank(&pre);
            line_text(&pre, UI_IVORY, "The shelves are empty.");
            line_italic(&pre, UI_STONE, "Every collection begins with a single film.");
        } else if (film_filter_active(&app->filter)) {
            film_filter_describe(&app->filter, desc, sizeof desc);
            char summary[320];
            snprintf(summary, sizeof summary, "Showing %zu of %zu" SEP "%s", n, total, desc);
            line_italic(&pre, UI_STONE, summary);
            if (n == 0) {
                line_blank(&pre);
                line_text(&pre, UI_IVORY, "No film answers to that.");
                line_italic(&pre, UI_STONE, "Loosen a filter, or choose Show every film.");
            }
        } else {
            char summary[160];
            snprintf(summary, sizeof summary, "%zu watched" SEP "%zu on the watchlist",
                     archive_count_status(&app->archive, FILM_WATCHED),
                     archive_count_status(&app->archive, FILM_WATCHLIST));
            line_italic(&pre, UI_STONE, summary);
        }
        line_blank(&pre);

        if (sel < 0)
            sel = first_film >= 0 ? first_film : 0;
        UiMenu m = {.section = "The collection", .preamble = &pre,
                    .items = (const char *const *)c.items.items, .count = (int)c.items.count,
                    .selected = sel, .back_label = "Back", .highlight = app->filter.query,
                    .detail_right = false};
        /* Catalogue rows right-align the rating; option rows read better
         * left-aligned, so only switch when the list is all films. */
        m.detail_right = first_film >= 0;
        int k = ui_menu(&m);
        int act = k >= 0 ? c.acts[k] : ACT_NONE;
        unsigned id = k >= 0 ? c.ids[k] : 0;
        strlist_free(&pre);
        choices_free(&c);
        if (k == UI_BACK)
            return;
        sel = k;

        switch (act) {
        case ACT_ADD:
            if (flow_add_film(app, -1, NULL))
                sel = -1;
            break;
        case ACT_SEARCH: flow_refine_search(app); sel = -1; break;
        case ACT_FILTER: screen_filter(app); sel = -1; break;
        case ACT_SORT: screen_sort(app); break;
        case ACT_CLEAR:
            film_filter_init(&app->filter);
            ui_flash(UI_TONE_OK, "Showing every film.");
            sel = -1;
            break;
        case ACT_UNDO: flow_undo(app); break;
        case ACT_FILM: screen_film(app, id); break;
        default: break;
        }
    }
}

/* ================================================================== */
/* Search                                                             */
/* ================================================================== */

static bool ask_query(char *q, size_t cap)
{
    ui_form_begin("Search the archive", "Search the archive",
                  "Any part of a title or a director's name. Leave it empty to go back.");
    return ui_prompt("Look for", NULL, NULL, q, cap) == PROMPT_OK;
}

static void screen_search(App *app)
{
    char q[FILM_TITLE_MAX];
    if (!ask_query(q, sizeof q))
        return;
    int sel = -1;
    bool announced = false;
    while (!ui_input_closed()) {
        FilmFilter fl;
        film_filter_init(&fl);
        text_copy(fl.query, sizeof fl.query, q);
        archive_sort(&app->archive, SORT_TITLE, false);
        const Film **films = NULL;
        size_t n = 0;
        if (!archive_collect(&app->archive, &fl, &films, &n))
            return;

        Choices c;
        choices_init(&c);
        choices_add(&c, "SEARCH AGAIN", NULL, ACT_AGAIN, 0);
        if (n == 0) {
            Sb sb;
            sb_init(&sb);
            sb_printf(&sb, "ADD " LQ "%s" RQ " TO THE ARCHIVE", q);
            choices_add(&c, NULL, sb_take(&sb), ACT_ADD, 0);
        }
        int first = -1;
        if (n > 0) {
            choices_add(&c, "", NULL, ACT_NONE, 0);
            first = (int)c.items.count;
            int digits = index_digits(n);
            for (size_t i = 0; i < n; i++) {
                char right[16];
                rating_text(films[i], right, sizeof right);
                choices_add(&c, NULL, catalogue_item(films[i], i + 1, digits, right), ACT_FILM, films[i]->id);
            }
        }
        free(films);

        StrList pre;
        strlist_init(&pre);
        char aside[48];
        snprintf(aside, sizeof aside, "%zu found", n);
        line_heading(&pre, "Results", aside);
        char line[FILM_TITLE_MAX + 64];
        if (n > 0) {
            snprintf(line, sizeof line, "for " LQ "%s" RQ " in titles and directors", q);
            line_italic(&pre, UI_STONE, line);
            if (!announced) {
                audio_play(SOUND_CONFIRM);
                announced = true;
            }
        } else {
            line_blank(&pre);
            snprintf(line, sizeof line, "Nothing in the archive answers to " LQ "%s" RQ ".", q);
            line_text(&pre, UI_IVORY, line);
            line_italic(&pre, UI_STONE, "Search looks through titles and directors. Try fewer letters,");
            line_italic(&pre, UI_STONE, "or add the film to the archive now.");
        }
        line_blank(&pre);

        if (sel < 0)
            sel = first >= 0 ? first : 0;
        UiMenu m = {.section = "Search the archive", .preamble = &pre,
                    .items = (const char *const *)c.items.items, .count = (int)c.items.count,
                    .selected = sel, .back_label = "Back", .highlight = q, .detail_right = n > 0};
        int k = ui_menu(&m);
        int act = k >= 0 ? c.acts[k] : ACT_NONE;
        unsigned id = k >= 0 ? c.ids[k] : 0;
        strlist_free(&pre);
        choices_free(&c);
        if (k == UI_BACK)
            return;
        sel = k;
        if (act == ACT_AGAIN) {
            if (!ask_query(q, sizeof q))
                return;
            sel = -1;
            announced = false;
        } else if (act == ACT_ADD) {
            unsigned nid = flow_add_film(app, -1, q);
            if (nid) {
                screen_film(app, nid);
                return;
            }
        } else if (act == ACT_FILM) {
            screen_film(app, id);
        }
    }
}

/* ================================================================== */
/* Watchlist                                                          */
/* ================================================================== */

static int added_oldest_first(const void *pa, const void *pb)
{
    const Film *a = *(const Film *const *)pa;
    const Film *b = *(const Film *const *)pb;
    bool ea = a->added_on[0] == '\0';
    bool eb = b->added_on[0] == '\0';
    if (ea != eb)
        return ea ? 1 : -1;
    int c = strcmp(a->added_on, b->added_on);
    if (c != 0)
        return c;
    return (a->id > b->id) - (a->id < b->id);
}

static void screen_pick(App *app, int status);

static void screen_watchlist(App *app)
{
    int sel = -1;
    while (!ui_input_closed()) {
        FilmFilter only;
        film_filter_init(&only);
        only.status = FILM_WATCHLIST;
        const Film **films = NULL;
        size_t n = 0;
        if (!archive_collect(&app->archive, &only, &films, &n)) {
            ui_flash(UI_TONE_ERROR, "Out of memory.");
            return;
        }
        if (n > 1)
            qsort(films, n, sizeof *films, added_oldest_first);

        Choices c;
        choices_init(&c);
        if (n > 0)
            choices_add(&c, "CHOOSE ONE FOR TONIGHT\tleave it to chance", NULL, ACT_RANDOM, 0);
        choices_add(&c, "ADD A FILM TO SEE", NULL, ACT_ADD, 0);
        int first = -1;
        if (n > 0) {
            choices_add(&c, "", NULL, ACT_NONE, 0);
            first = (int)c.items.count;
            int digits = index_digits(n);
            for (size_t i = 0; i < n; i++) {
                char right[32] = "";
                if (films[i]->added_on[0]) {
                    char d[24];
                    date_pretty(films[i]->added_on, d, sizeof d);
                    snprintf(right, sizeof right, "SINCE %s", d);
                    text_upper(right, sizeof right, right);
                }
                choices_add(&c, NULL, catalogue_item(films[i], i + 1, digits, right), ACT_FILM, films[i]->id);
            }
        }
        free(films);

        StrList pre;
        strlist_init(&pre);
        char aside[48];
        snprintf(aside, sizeof aside, "%03zu to see", n);
        line_heading(&pre, "Watchlist", aside);
        if (n == 0) {
            line_blank(&pre);
            line_text(&pre, UI_IVORY, "Nothing is waiting.");
            line_italic(&pre, UI_STONE, "The films you mean to see will gather here, in the order they arrived.");
        } else {
            line_italic(&pre, UI_STONE, "A queue of films not yet seen, the longest-waiting first.");
        }
        line_blank(&pre);

        if (sel < 0)
            sel = first >= 0 ? first : 0;
        UiMenu m = {.section = "Watchlist", .preamble = &pre,
                    .items = (const char *const *)c.items.items, .count = (int)c.items.count,
                    .selected = sel, .back_label = "Back", .detail_right = n > 0};
        int k = ui_menu(&m);
        int act = k >= 0 ? c.acts[k] : ACT_NONE;
        unsigned id = k >= 0 ? c.ids[k] : 0;
        strlist_free(&pre);
        choices_free(&c);
        if (k == UI_BACK)
            return;
        sel = k;
        switch (act) {
        case ACT_RANDOM: screen_pick(app, FILM_WATCHLIST); break;
        case ACT_ADD:
            if (flow_add_film(app, FILM_WATCHLIST, NULL))
                sel = -1;
            break;
        case ACT_FILM: screen_film(app, id); break;
        default: break;
        }
    }
}

/* ================================================================== */
/* Cinema journal                                                     */
/* ================================================================== */

static void screen_entry(App *app, unsigned entry_id)
{
    enum { E_OPEN, E_NOTE, E_DATE, E_DELETE };
    int sel = 0;
    while (!ui_input_closed()) {
        JournalEntry *e = archive_entry_find(&app->archive, entry_id);
        if (!e)
            return;
        Film *f = archive_find(&app->archive, e->film_id);
        if (!f)
            return;
        unsigned film_id = f->id;
        char date[24];
        date_pretty(e->date, date, sizeof date);
        char meta[FILM_DIRECTOR_MAX + FILM_GENRE_MAX + 40];
        film_meta(f, meta, sizeof meta);

        StrList pre;
        strlist_init(&pre);
        char up[24];
        text_upper(up, sizeof up, date);
        line_label(&pre, up);
        line_title(&pre, f->title, 2);
        line_text(&pre, UI_STONE, meta);
        line_rule(&pre, 12);
        line_blank(&pre);
        if (e->text[0])
            line_wrap(&pre, UI_IVORY, 2, e->text, 0, 0);
        else
            line_italic(&pre, UI_GREY, "No words for this viewing.");
        line_blank(&pre);

        const char *items[] = {"OPEN THE CATALOGUE CARD", "EDIT THE NOTE", "CHANGE THE DATE",
                               "REMOVE THIS ENTRY"};
        UiMenu m = {.section = "Cinema journal", .preamble = &pre, .items = items, .count = 4,
                    .selected = sel, .back_label = "Back"};
        int c = ui_menu(&m);
        strlist_free(&pre);
        if (c == UI_BACK)
            return;
        sel = c;

        if (c == E_OPEN) {
            screen_film(app, film_id);
            continue;
        }
        if (!app_can_write(app))
            continue;
        e = archive_entry_find(&app->archive, entry_id);
        f = archive_find(&app->archive, film_id);
        if (!e || !f)
            return;

        if (c == E_NOTE) {
            ui_form_begin("Cinema journal", f->title, NULL);
            char note[FILM_NOTES_MAX];
            PromptResult r = ui_prompt("Note", "blank keeps it " MIDDOT " '-' clears it", e->text, note, sizeof note);
            if (r == PROMPT_OK) {
                text_copy(e->text, sizeof e->text, strcmp(note, "-") == 0 ? "" : note);
                if (app_save(app)) {
                    audio_play(SOUND_CONFIRM);
                    ui_flash(UI_TONE_OK, "Saved.");
                }
            }
        } else if (c == E_DATE) {
            ui_form_begin("Cinema journal", f->title, NULL);
            char nd[DATE_LEN];
            PromptResult r = ui_prompt_date("Date watched", "YYYY-MM-DD", e->date, app->today, nd);
            if (r == PROMPT_OK) {
                text_copy(e->date, sizeof e->date, nd);
                film_recompute_last_seen(&app->archive, f);
                if (app_save(app)) {
                    audio_play(SOUND_CONFIRM);
                    ui_flash(UI_TONE_OK, "Saved.");
                }
            }
        } else if (c == E_DELETE) {
            ui_form_begin("Cinema journal", f->title, NULL);
            char q[96];
            snprintf(q, sizeof q, "Remove the entry for %s?", date);
            if (ui_confirm(q, false)) {
                bool was_latest = strcmp(e->date, f->watched_on) == 0;
                archive_remove_entry(&app->archive, entry_id);
                if (was_latest)
                    film_recompute_last_seen(&app->archive, f);
                if (app_save(app)) {
                    audio_play(SOUND_DELETE);
                    ui_flash(UI_TONE_OK, "Entry removed.");
                }
                return;
            }
        }
    }
}

static void flow_write_entry(App *app)
{
    if (!app_can_write(app))
        return;
    if (app->archive.count == 0) {
        ui_flash(UI_TONE_INFO, "Add a film first, then keep its viewings here.");
        return;
    }
    ui_form_begin("Cinema journal", "A new page", "Which film did you watch? Part of its title is enough.");
    char q[FILM_TITLE_MAX];
    PromptResult r = ui_prompt("Film", "blank to cancel", NULL, q, sizeof q);
    if (r != PROMPT_OK)
        return;
    FilmFilter fl;
    film_filter_init(&fl);
    text_copy(fl.query, sizeof fl.query, q);
    const Film **films = NULL;
    size_t n = 0;
    if (!archive_collect(&app->archive, &fl, &films, &n))
        return;
    if (n == 0) {
        ui_flash(UI_TONE_INFO, "No film in the archive answers to " LQ "%s" RQ ". Add it first.", q);
        return;
    }
    unsigned id = films[0]->id;
    if (n > 1) {
        Choices c;
        choices_init(&c);
        for (size_t i = 0; i < n; i++) {
            char year[8];
            fmt_year(films[i]->year, year, sizeof year);
            Sb sb;
            sb_init(&sb);
            sb_printf(&sb, "%s (%s)\t%s", films[i]->title, year, films[i]->director[0] ? films[i]->director : "");
            choices_add(&c, NULL, sb_take(&sb), ACT_FILM, films[i]->id);
        }
        StrList pre;
        strlist_init(&pre);
        line_heading(&pre, "Which one?", NULL);
        line_blank(&pre);
        UiMenu m = {.section = "Cinema journal", .preamble = &pre,
                    .items = (const char *const *)c.items.items, .count = (int)c.items.count,
                    .back_label = "Cancel", .highlight = q};
        int k = ui_menu(&m);
        id = k >= 0 ? c.ids[k] : 0;
        strlist_free(&pre);
        choices_free(&c);
    }
    free(films);
    if (id)
        flow_log_viewing(app, id);
}

static void screen_journal(App *app)
{
    int sel = -1;
    while (!ui_input_closed()) {
        const JournalEntry **es;
        size_t n = collect_entries(&app->archive, 0, &es);
        Choices c;
        choices_init(&c);
        choices_add(&c, "WRITE AN ENTRY\tany film in the archive", NULL, ACT_WRITE, 0);
        int first = -1;
        size_t films_seen = 0;
        if (n > 0) {
            choices_add(&c, "", NULL, ACT_NONE, 0);
            first = (int)c.items.count;
            for (size_t i = 0; i < n; i++) {
                const Film *f = archive_find(&app->archive, es[i]->film_id);
                char date[24];
                char up_date[24];
                char up_title[FILM_TITLE_MAX * 2];
                date_pretty(es[i]->date, date, sizeof date);
                text_upper(up_date, sizeof up_date, date);
                text_upper(up_title, sizeof up_title, f ? f->title : "?");
                Sb sb;
                sb_init(&sb);
                sb_printf(&sb, "%s\t%s\n\x0e%s\n", up_title, up_date,
                          es[i]->text[0] ? es[i]->text : "no words for this viewing");
                choices_add(&c, NULL, sb_take(&sb), ACT_ENTRY, es[i]->id);
            }
            for (const Film *f = app->archive.head; f; f = f->next)
                if (archive_entry_count(&app->archive, f->id))
                    films_seen++;
        }

        StrList pre;
        strlist_init(&pre);
        char aside[48];
        snprintf(aside, sizeof aside, "%03zu entr%s", n, n == 1 ? "y" : "ies");
        line_heading(&pre, "Cinema journal", aside);
        if (n == 0) {
            line_blank(&pre);
            line_text(&pre, UI_IVORY, "The notebook is still blank.");
            line_italic(&pre, UI_STONE, "Each time you watch a film, note the date, a rating, a few words.");
        } else {
            char since[24];
            date_pretty(es[n - 1]->date, since, sizeof since);
            char summary[160];
            snprintf(summary, sizeof summary, "%zu film%s" SEP "kept since %s", films_seen,
                     films_seen == 1 ? "" : "s", since);
            line_italic(&pre, UI_STONE, summary);
        }
        free(es);
        line_blank(&pre);

        if (sel < 0)
            sel = first >= 0 ? first : 0;
        UiMenu m = {.section = "Cinema journal", .preamble = &pre,
                    .items = (const char *const *)c.items.items, .count = (int)c.items.count,
                    .selected = sel, .back_label = "Back", .detail_right = n > 0};
        int k = ui_menu(&m);
        int act = k >= 0 ? c.acts[k] : ACT_NONE;
        unsigned id = k >= 0 ? c.ids[k] : 0;
        strlist_free(&pre);
        choices_free(&c);
        if (k == UI_BACK)
            return;
        sel = k;
        if (act == ACT_WRITE) {
            flow_write_entry(app);
            sel = -1;
        } else if (act == ACT_ENTRY) {
            screen_entry(app, id);
        }
    }
}

/* ================================================================== */
/* Tonight's selection                                                */
/* ================================================================== */

#define TONIGHT_HEADING "Tonight, we watch..."

static void screen_pick(App *app, int status)
{
    while (!ui_input_closed()) {
        size_t pool = 0;
        const Film *f = discover_random(&app->archive, status, &app->rng, &pool);
        if (!f) {
            ui_flash(UI_TONE_INFO, status == FILM_WATCHLIST
                                       ? "The watchlist is empty " MDASH " nothing to choose from yet."
                                       : "The archive is empty " MDASH " nothing to choose from yet.");
            return;
        }
        unsigned id = f->id;

        /* The reel: other titles flicker past before the choice settles. */
        FilmFilter fl;
        film_filter_init(&fl);
        fl.status = status;
        const Film **all = NULL;
        size_t n = 0;
        if (archive_collect(&app->archive, &fl, &all, &n) && n > 0) {
            const char **names = malloc(n * sizeof *names);
            if (names) {
                for (size_t i = 0; i < n; i++)
                    names[i] = all[(i * 5 + rng_below(&app->rng, (uint32_t)n)) % n]->title;
                if (ui_animation())
                    audio_play(SOUND_REEL);
                ui_reveal("Tonight's selection", TONIGHT_HEADING, names, n, f->title);
                free(names);
            }
        }
        free(all);
        audio_play(SOUND_REVEAL);

        /* The same rows as the reveal: heading, blank, title. */
        StrList pre;
        strlist_init(&pre);
        Sb sb;
        sb_init(&sb);
        ui_sb_margin(&sb);
        ui_sb_fg(&sb, UI_GOLD);
        ui_sb_tracked(&sb, TONIGHT_HEADING);
        strlist_push_sb(&pre, &sb);
        line_blank(&pre);
        line_title(&pre, f->title, 2);
        char meta[FILM_DIRECTOR_MAX + FILM_GENRE_MAX + 40];
        film_meta(f, meta, sizeof meta);
        line_text(&pre, UI_STONE, meta);
        line_rule(&pre, 12);
        line_blank(&pre);
        char buf[64];
        if (f->status == FILM_WATCHED && f->rating) {
            snprintf(buf, sizeof buf, "%02d/10", f->rating);
            line_field(&pre, "PERSONAL RATING", buf, UI_GOLD);
        } else {
            line_field(&pre, "PERSONAL RATING", f->status == FILM_WATCHED ? "Not rated" : "Not seen yet", UI_STONE);
        }
        line_field(&pre, "FROM", status == FILM_WATCHLIST ? "your watchlist" : "the whole collection", UI_STONE);
        if (f->notes[0]) {
            line_blank(&pre);
            line_wrap(&pre, UI_IVORY, 2, f->notes, 0, 2);
        }
        line_blank(&pre);
        if (pool == 1) {
            line_italic(&pre, UI_GREY, status == FILM_WATCHLIST ? "It is the only film on your watchlist."
                                                                 : "It is the only film in the archive.");
            line_blank(&pre);
        }
        const char *items[] = {"OPEN THE CATALOGUE CARD", "SPIN THE REEL AGAIN"};
        UiMenu m = {.section = "Tonight's selection", .preamble = &pre, .items = items,
                    .count = pool > 1 ? 2 : 1, .back_label = "Back"};
        int c = ui_menu(&m);
        strlist_free(&pre);
        if (c == 0) {
            screen_film(app, id);
            return;
        }
        if (c != 1)
            return;
    }
}

static void screen_recommend(App *app)
{
    enum { MAX_RECS = 12 };
    Recommendation recs[MAX_RECS];
    bool rewatch = false;
    size_t n = discover_recommend(&app->archive, recs, MAX_RECS, &rewatch);
    if (n == 0) {
        ui_flash(UI_TONE_INFO, "Nothing to recommend yet " MDASH " add films to your watchlist and rate what you've seen.");
        return;
    }
    unsigned ids[MAX_RECS];
    char reasons[MAX_RECS][200];
    for (size_t i = 0; i < n; i++) {
        ids[i] = recs[i].film->id;
        text_copy(reasons[i], sizeof reasons[i], recs[i].reason);
    }

    int sel = 0;
    while (!ui_input_closed()) {
        Choices c;
        choices_init(&c);
        int digits = index_digits(n);
        for (size_t i = 0; i < n; i++) {
            const Film *f = archive_find(&app->archive, ids[i]);
            if (!f)
                continue; /* deleted from its card meanwhile */
            char up[FILM_TITLE_MAX * 2];
            text_upper(up, sizeof up, f->title);
            char year[8];
            fmt_year(f->year, year, sizeof year);
            Sb sb;
            sb_init(&sb);
            sb_printf(&sb, "%0*zu  %s\t%s\n%s\n", digits, i + 1, up, year, reasons[i]);
            choices_add(&c, NULL, sb_take(&sb), ACT_FILM, f->id);
        }
        if (c.items.count == 0) {
            choices_free(&c);
            return;
        }
        StrList pre;
        strlist_init(&pre);
        line_heading(&pre, rewatch ? "Worth revisiting" : "Recommended", NULL);
        if (rewatch)
            line_wrap(&pre, UI_STONE, 2,
                      "The watchlist is empty, so here are favourites to return to: films you rated "
                      "8 or more, the longest unseen first.", 0, 0);
        else
            line_wrap(&pre, UI_STONE, 2,
                      "Your watchlist, ranked by how you have rated other films of the same genre and "
                      "by the same director. Worked out from your own archive " MDASH " no network, no AI.",
                      0, 0);
        line_blank(&pre);
        UiMenu m = {.section = "Tonight's selection", .preamble = &pre,
                    .items = (const char *const *)c.items.items, .count = (int)c.items.count,
                    .selected = sel, .back_label = "Back", .detail_right = true};
        int k = ui_menu(&m);
        unsigned id = k >= 0 ? c.ids[k] : 0;
        strlist_free(&pre);
        choices_free(&c);
        if (k == UI_BACK)
            return;
        sel = k;
        screen_film(app, id);
    }
}

static void screen_double_feature(App *app)
{
    while (!ui_input_closed()) {
        DoubleFeature df;
        if (!discover_double_feature(&app->archive, &app->rng, &df)) {
            ui_flash(UI_TONE_INFO, "A double feature needs at least two films in the archive.");
            return;
        }
        audio_play(SOUND_REVEAL);
        unsigned a = df.first->id;
        unsigned b = df.second->id;
        char meta[FILM_DIRECTOR_MAX + FILM_GENRE_MAX + 40];
        StrList pre;
        strlist_init(&pre);
        line_heading(&pre, "A double feature", df.from_watchlist ? "from your watchlist" : "from the collection");
        line_blank(&pre);
        line_label(&pre, "First");
        line_title(&pre, df.first->title, 2);
        film_meta(df.first, meta, sizeof meta);
        line_text(&pre, UI_STONE, meta);
        line_blank(&pre);
        line_label(&pre, "Then");
        line_title(&pre, df.second->title, 2);
        film_meta(df.second, meta, sizeof meta);
        line_text(&pre, UI_STONE, meta);
        line_blank(&pre);
        line_rule(&pre, 12);
        line_wrap(&pre, UI_IVORY, 2, df.reason, 0, 2);
        line_blank(&pre);

        char open_a[FILM_TITLE_MAX + 32];
        char open_b[FILM_TITLE_MAX + 32];
        snprintf(open_a, sizeof open_a, "OPEN THE FIRST\t%s", df.first->title);
        snprintf(open_b, sizeof open_b, "OPEN THE SECOND\t%s", df.second->title);
        const char *items[] = {"ANOTHER PAIRING", open_a, open_b};
        UiMenu m = {.section = "Tonight's selection", .preamble = &pre, .items = items, .count = 3,
                    .back_label = "Back"};
        int c = ui_menu(&m);
        strlist_free(&pre);
        if (c == UI_BACK)
            return;
        if (c == 1)
            screen_film(app, a);
        else if (c == 2)
            screen_film(app, b);
    }
}

static void screen_tonight(App *app)
{
    int sel = 0;
    while (!ui_input_closed()) {
        const char *items[] = {"SURPRISE ME\tany film in the collection",
                               "FROM THE WATCHLIST\tsomething not yet seen",
                               "RECOMMENDED\tranked by your own ratings",
                               "A DOUBLE FEATURE\ttwo films that belong together"};
        StrList pre;
        strlist_init(&pre);
        line_heading(&pre, "Tonight's selection", NULL);
        line_italic(&pre, UI_STONE, "Chance, taste, or a pairing. Every suggestion comes from your own archive.");
        line_blank(&pre);
        line_rule(&pre, 12);
        line_blank(&pre);
        UiMenu m = {.section = "Tonight's selection", .preamble = &pre, .items = items, .count = 4,
                    .selected = sel, .back_label = "Back", .number_keys = true};
        int c = ui_menu(&m);
        strlist_free(&pre);
        if (c == UI_BACK)
            return;
        sel = c;
        switch (c) {
        case 0: screen_pick(app, -1); break;
        case 1: screen_pick(app, FILM_WATCHLIST); break;
        case 2: screen_recommend(app); break;
        case 3: screen_double_feature(app); break;
        default: break;
        }
    }
}

/* ================================================================== */
/* Statistics                                                         */
/* ================================================================== */

static void sb_bar(Sb *sb, int value, int max, int width)
{
    static const char *partial[] = {"\xE2\x96\x8F", "\xE2\x96\x8E", "\xE2\x96\x8D", "\xE2\x96\x8C",
                                    "\xE2\x96\x8B", "\xE2\x96\x8A", "\xE2\x96\x89"};
    long units = max > 0 ? (long)value * width * 8 / max : 0;
    if (value > 0 && units == 0)
        units = 1;
    for (long i = 0; i < units / 8; i++)
        sb_puts(sb, "\xE2\x96\x88");
    if (units % 8)
        sb_puts(sb, partial[units % 8 - 1]);
}

static void line_bar(StrList *l, const char *label, int value, int max, const char *suffix)
{
    int cw = ui_content_width();
    int label_w = 20;
    int bar_w = cw - label_w - 18;
    if (bar_w > 32)
        bar_w = 32;
    if (bar_w < 6)
        bar_w = 6;
    Sb sb;
    sb_init(&sb);
    ui_sb_margin(&sb);
    ui_sb_fg(&sb, UI_STONE);
    sb_fit(&sb, label, label_w, true);
    ui_sb_fg(&sb, UI_GOLD);
    sb_bar(&sb, value, max, bar_w);
    ui_sb_fg(&sb, UI_GREY);
    sb_printf(&sb, "  %d", value);
    if (suffix)
        sb_puts(&sb, suffix);
    strlist_push_sb(l, &sb);
}

/* Two large figures side by side, with labels underneath. */
static void line_figures(StrList *l, const char *a, const char *label_a, const char *b, const char *label_b)
{
    Sb rows[3];
    for (int r = 0; r < 3; r++) {
        sb_init(&rows[r]);
        ui_sb_margin(&rows[r]);
        ui_sb_fg(&rows[r], UI_IVORY);
    }
    int wa = ui_sb_bignum(rows, a);
    int col_b = wa > 22 ? wa + 6 : 28;
    for (int r = 0; r < 3; r++) {
        sb_spaces(&rows[r], col_b - wa);
        ui_sb_fg(&rows[r], UI_GOLD);
    }
    ui_sb_bignum(rows, b);
    for (int r = 0; r < 3; r++)
        strlist_push_sb(l, &rows[r]);
    Sb lab;
    sb_init(&lab);
    ui_sb_margin(&lab);
    ui_sb_fg(&lab, UI_GREY);
    sb_fit(&lab, label_a, col_b, true);
    sb_puts(&lab, label_b);
    strlist_push_sb(l, &lab);
}

static void screen_stats(App *app)
{
    Stats s;
    if (!stats_compute(&app->archive, app->today, &s)) {
        ui_flash(UI_TONE_ERROR, "Out of memory while counting.");
        return;
    }
    StrList L;
    strlist_init(&L);
    char buf[256];
    char d1[24];
    char d2[24];

    line_heading(&L, "Statistics", "the archive in numbers");
    line_blank(&L);
    if (s.total == 0) {
        line_figures(&L, "0", "FILMS ARCHIVED", "-", "AVERAGE RATING");
        line_blank(&L);
        line_text(&L, UI_IVORY, "Nothing to measure yet.");
        line_italic(&L, UI_STONE, "Add a film and the archive will begin to describe your taste.");
        ui_page("Statistics", &L);
        strlist_free(&L);
        stats_free(&s);
        return;
    }

    char total[16];
    char avg[16];
    snprintf(total, sizeof total, "%zu", s.total);
    if (s.rated)
        snprintf(avg, sizeof avg, "%.1f", s.avg_rating);
    else
        snprintf(avg, sizeof avg, "-");
    line_figures(&L, total, "FILMS ARCHIVED", avg, s.rated ? "AVERAGE RATING / 10" : "NO RATINGS YET");
    line_blank(&L);
    snprintf(buf, sizeof buf, "%zu watched" SEP "%zu on the watchlist" SEP "%zu viewing%s logged", s.watched,
             s.watchlist, s.viewings, s.viewings == 1 ? "" : "s");
    line_text(&L, UI_STONE, buf);
    if (s.first_viewing[0]) {
        date_pretty(s.first_viewing, d1, sizeof d1);
        date_pretty(s.last_viewing, d2, sizeof d2);
        snprintf(buf, sizeof buf, "The journal runs from %s to %s.", d1, d2);
        line_italic(&L, UI_GREY, buf);
    }

    line_blank(&L);
    line_rule(&L, 12);
    line_label(&L, "Highest rated");
    if (s.top_count == 0)
        line_italic(&L, UI_GREY, "Rate the films you have watched and your favourites will gather here.");
    for (size_t i = 0; i < s.top_count; i++) {
        const Film *f = s.top[i];
        Sb sb;
        sb_init(&sb);
        ui_sb_margin(&sb);
        ui_sb_fg(&sb, UI_GOLD);
        sb_printf(&sb, "%02d/10   ", f->rating);
        ui_sb_fg(&sb, UI_IVORY);
        char up[FILM_TITLE_MAX * 2];
        text_upper(up, sizeof up, f->title);
        /* rating 8 + title + 3 + year 4 + separator 5 + director <= width */
        int director_w = ui_content_width() >= 64 ? 18 : 0;
        int title_w = ui_content_width() - 8 - 3 - 4 - (director_w ? 5 + director_w : 0);
        sb_fit(&sb, up, title_w, true);
        ui_sb_fg(&sb, UI_GREY);
        char year[8];
        fmt_year(f->year, year, sizeof year);
        sb_printf(&sb, "   %s", year);
        if (f->director[0] && director_w) {
            sb_puts(&sb, SEP);
            sb_fit(&sb, f->director, director_w, false);
        }
        strlist_push_sb(&L, &sb);
    }

    line_blank(&L);
    line_label(&L, "Favourite genres");
    if (s.genre_count == 0)
        line_italic(&L, UI_GREY, "No genres recorded yet.");
    int gmax = s.genre_count ? s.genres[0].count : 0;
    for (size_t i = 0; i < s.genre_count && i < 8; i++) {
        if (s.genres[i].rated)
            snprintf(buf, sizeof buf, "   avg %.1f", s.genres[i].avg);
        else
            buf[0] = '\0';
        line_bar(&L, s.genres[i].name, s.genres[i].count, gmax, buf);
    }
    if (s.genre_count > 8) {
        snprintf(buf, sizeof buf, "and %zu more", s.genre_count - 8);
        line_italic(&L, UI_GREY, buf);
    }

    line_blank(&L);
    line_label(&L, "Watching history, the last twelve months");
    int mmax = 0;
    int year_total = 0;
    int busiest = -1;
    for (int i = 0; i < STATS_MONTHS; i++) {
        year_total += s.months[i];
        if (s.months[i] > mmax) {
            mmax = s.months[i];
            busiest = i;
        }
    }
    if (year_total == 0) {
        line_italic(&L, UI_GREY, "No dated viewings in the last twelve months.");
    } else {
        static const char *levels[] = {"\xE2\x96\x81", "\xE2\x96\x82", "\xE2\x96\x83", "\xE2\x96\x84",
                                       "\xE2\x96\x85", "\xE2\x96\x86", "\xE2\x96\x87", "\xE2\x96\x88"};
        Sb counts, bars, labels;
        sb_init(&counts);
        sb_init(&bars);
        sb_init(&labels);
        ui_sb_margin(&counts);
        ui_sb_margin(&bars);
        ui_sb_margin(&labels);
        ui_sb_fg(&counts, UI_GREY);
        ui_sb_fg(&labels, UI_GREY);
        for (int i = 0; i < STATS_MONTHS; i++) {
            int v = s.months[i];
            if (v)
                sb_printf(&counts, "%-4d", v);
            else
                sb_puts(&counts, "    ");
            if (v) {
                ui_sb_fg(&bars, UI_GOLD);
                const char *g = levels[(v * 7 + mmax - 1) / mmax];
                sb_puts(&bars, g);
                sb_puts(&bars, g);
            } else {
                ui_sb_fg(&bars, UI_ASH);
                sb_puts(&bars, "\xE2\x96\x81\xE2\x96\x81");
            }
            sb_puts(&bars, "  ");
            char mon[8];
            text_upper(mon, sizeof mon, month_abbrev(s.month_number[i]));
            sb_printf(&labels, "%-4s", mon);
        }
        strlist_push_sb(&L, &counts);
        strlist_push_sb(&L, &bars);
        strlist_push_sb(&L, &labels);
        snprintf(buf, sizeof buf, "%d viewing%s" SEP "busiest: %s %d, with %d", year_total,
                 year_total == 1 ? "" : "s", month_abbrev(s.month_number[busiest]), s.month_year[busiest], mmax);
        line_italic(&L, UI_GREY, buf);
    }

    line_blank(&L);
    line_label(&L, "By decade");
    int dmax = 0;
    for (int i = 0; i < STATS_DECADES; i++)
        if (s.decades[i] > dmax)
            dmax = s.decades[i];
    for (int i = 0; i < STATS_DECADES; i++) {
        if (!s.decades[i])
            continue;
        snprintf(d1, sizeof d1, "%ds", STATS_DECADE_BASE + i * 10);
        line_bar(&L, d1, s.decades[i], dmax, NULL);
    }
    if (s.unknown_year) {
        snprintf(buf, sizeof buf, "%zu film%s without a year", s.unknown_year, s.unknown_year == 1 ? "" : "s");
        line_italic(&L, UI_GREY, buf);
    }

    line_blank(&L);
    line_label(&L, "Directors");
    if (s.director_count == 0)
        line_italic(&L, UI_GREY, "No directors recorded yet.");
    for (size_t i = 0; i < s.director_count && i < 6; i++) {
        const NamedStat *d = &s.directors[i];
        char right[64];
        if (d->rated)
            snprintf(right, sizeof right, "%d film%s" SEP "avg %.1f", d->count, d->count == 1 ? "" : "s", d->avg);
        else
            snprintf(right, sizeof right, "%d film%s", d->count, d->count == 1 ? "" : "s");
        Sb sb;
        sb_init(&sb);
        ui_sb_margin(&sb);
        ui_sb_fg(&sb, UI_IVORY);
        sb_fit(&sb, d->name, 24, true);
        ui_sb_fg(&sb, UI_STONE);
        sb_puts(&sb, right);
        strlist_push_sb(&L, &sb);
    }

    if (s.rated) {
        line_blank(&L);
        line_label(&L, "Your ratings");
        int rmax = 0;
        for (int r = RATING_MIN; r <= RATING_MAX; r++)
            if (s.rating_counts[r] > rmax)
                rmax = s.rating_counts[r];
        for (int r = RATING_MAX; r >= RATING_MIN; r--) {
            if (!s.rating_counts[r])
                continue;
            snprintf(d1, sizeof d1, "%02d/10", r);
            line_bar(&L, d1, s.rating_counts[r], rmax, NULL);
        }
    }

    ui_page("Statistics", &L);
    strlist_free(&L);
    stats_free(&s);
}

/* ================================================================== */
/* Settings                                                           */
/* ================================================================== */

static void sound_status(const App *app, char *out, size_t cap)
{
    if (!ui_interactive()) {
        snprintf(out, cap, "Sound plays only in an interactive terminal.");
        return;
    }
    switch (audio_status()) {
    case AUDIO_NO_PLAYER:
        snprintf(out, cap, "Silent: afplay was not found. Sound needs macOS.");
        return;
    case AUDIO_NO_ASSETS:
        snprintf(out, cap, "Silent: no sound files were found. Run 'make sounds' to create them.");
        return;
    case AUDIO_READY:
        break;
    }
    if (app->muted)
        snprintf(out, cap, "Muted for this session (--mute). %zu of %d sounds ready.", audio_available(), SOUND_COUNT);
    else
        snprintf(out, cap, "Ready: %zu of %d sounds, played with %s.", audio_available(), SOUND_COUNT, audio_player());
}

static void apply_settings(App *app)
{
    ui_set_animation(app->settings.animation);
    audio_set_enabled(app->settings.sound && !app->muted);
    audio_set_volume(app->settings.volume);
}

static void screen_preview(void)
{
    const Sound order[] = {SOUND_STARTUP, SOUND_MOVE,   SOUND_SECTION, SOUND_FILED, SOUND_CONFIRM,
                           SOUND_REEL,    SOUND_REVEAL, SOUND_DELETE,  SOUND_ERROR, SOUND_STOP};
    char rows[SOUND_COUNT][96];
    const char *items[SOUND_COUNT];
    for (int i = 0; i < SOUND_COUNT; i++) {
        char up[64];
        text_upper(up, sizeof up, audio_sound_label(order[i]));
        snprintf(rows[i], sizeof rows[i], "%s\t%s", up, audio_file_name(order[i]));
        items[i] = rows[i];
    }
    int sel = 0;
    while (!ui_input_closed()) {
        StrList pre;
        strlist_init(&pre);
        line_heading(&pre, "Listen", NULL);
        line_italic(&pre, UI_STONE, "Choose a sound to hear it. Every sound is original, synthesised for Cinephilia.");
        line_blank(&pre);
        UiMenu m = {.section = "Settings", .preamble = &pre, .items = items, .count = SOUND_COUNT,
                    .selected = sel, .back_label = "Back"};
        int c = ui_menu(&m);
        strlist_free(&pre);
        if (c == UI_BACK)
            return;
        sel = c;
        if (!audio_enabled() || audio_status() != AUDIO_READY) {
            ui_flash(UI_TONE_INFO, "Sound is off or unavailable here, so nothing will play.");
            continue;
        }
        audio_stop_all();
        audio_play(order[c]);
        ui_flash(UI_TONE_INFO, "Playing: %s.", audio_sound_label(order[c]));
    }
}

static void screen_settings(App *app)
{
    int sel = 0;
    while (!ui_input_closed()) {
        char rows[3][96];
        snprintf(rows[0], sizeof rows[0], "MOTION\t%s",
                 app->settings.animation ? "on " MDASH " opening sequence and transitions" : "off " MDASH " still");
        snprintf(rows[1], sizeof rows[1], "SOUND EFFECTS\t%s", app->settings.sound ? "on" : "off");
        snprintf(rows[2], sizeof rows[2], "VOLUME\t%d / 10", app->settings.volume);
        const char *items[] = {rows[0], rows[1], rows[2], "LISTEN TO THE SOUNDS", "", "RESET PREFERENCES"};

        StrList pre;
        strlist_init(&pre);
        line_heading(&pre, "Settings", NULL);
        line_italic(&pre, UI_STONE, "Saved as you change them, next to your archive.");
        line_blank(&pre);
        char status[256];
        sound_status(app, status, sizeof status);
        line_field(&pre, "SOUND", status, UI_STONE);
        line_blank(&pre);

        StrList post;
        strlist_init(&post);
        line_blank(&post);
        line_rule(&post, 12);
        line_field(&post, "ARCHIVE FILE", app->path, UI_GREY);
        line_field(&post, "SETTINGS FILE", app->settings_path, UI_GREY);
        if (audio_asset_dir()[0])
            line_field(&post, "SOUNDS", audio_asset_dir(), UI_GREY);

        UiMenu m = {.section = "Settings", .preamble = &pre, .items = items, .count = 6,
                    .selected = sel, .back_label = "Back", .postamble = &post};
        int c = ui_menu(&m);
        strlist_free(&pre);
        strlist_free(&post);
        if (c == UI_BACK)
            return;
        sel = c;
        switch (c) {
        case 0:
            app->settings.animation = !app->settings.animation;
            apply_settings(app);
            save_settings(app);
            ui_flash(UI_TONE_OK, app->settings.animation ? "Motion on." : "Motion off: every screen appears at once.");
            break;
        case 1:
            app->settings.sound = !app->settings.sound;
            apply_settings(app);
            save_settings(app);
            audio_play(SOUND_CONFIRM);
            ui_flash(UI_TONE_OK, app->settings.sound ? "Sound effects on." : "Sound effects off. Silence it is.");
            break;
        case 2: {
            ui_form_begin("Settings", "Volume",
                          "1 is barely there; 10 is afplay's normal level. The default is 4. "
                          "Your Mac's own volume is never changed.");
            char cur[8];
            snprintf(cur, sizeof cur, "%d", app->settings.volume);
            int v = 0;
            if (ui_prompt_int("Volume", "1-10", cur, 1, 10, &v) == PROMPT_OK) {
                app->settings.volume = v;
                apply_settings(app);
                save_settings(app);
                audio_play(SOUND_CONFIRM);
                ui_flash(UI_TONE_OK, "Volume %d of 10.", v);
            }
            break;
        }
        case 3:
            screen_preview();
            break;
        case 5:
            ui_form_begin("Settings", "Reset preferences", NULL);
            if (ui_confirm("Return motion, sound and volume to their defaults?", false)) {
                settings_defaults(&app->settings);
                apply_settings(app);
                save_settings(app);
                ui_flash(UI_TONE_OK, "Preferences reset.");
            }
            break;
        default:
            break;
        }
    }
}

/* ================================================================== */
/* The archival index (home)                                          */
/* ================================================================== */

static void screen_home(App *app)
{
    int sel = 0;
    while (!ui_input_closed()) {
        size_t total = app->archive.count;
        size_t to_see = archive_count_status(&app->archive, FILM_WATCHLIST);
        size_t entries = app->archive.journal_count;

        char rows[9][96];
        snprintf(rows[0], sizeof rows[0], "01  THE COLLECTION\t%03zu film%s", total, total == 1 ? "" : "s");
        snprintf(rows[1], sizeof rows[1], "02  ADD A FILM\ta new record");
        snprintf(rows[2], sizeof rows[2], "03  SEARCH THE ARCHIVE\ttitles and directors");
        snprintf(rows[3], sizeof rows[3], "04  WATCHLIST\t%03zu to see", to_see);
        snprintf(rows[4], sizeof rows[4], "05  TONIGHT'S SELECTION\tchance, taste, pairings");
        snprintf(rows[5], sizeof rows[5], "06  CINEMA JOURNAL\t%03zu entr%s", entries, entries == 1 ? "y" : "ies");
        snprintf(rows[6], sizeof rows[6], "07  STATISTICS\tthe archive in numbers");
        snprintf(rows[7], sizeof rows[7], "08  SETTINGS\tmotion, sound");
        snprintf(rows[8], sizeof rows[8], "09  EXIT");
        const char *items[9];
        for (int i = 0; i < 9; i++)
            items[i] = rows[i];

        StrList pre;
        strlist_init(&pre);
        line_heading(&pre, "The archive", NULL);
        line_italic(&pre, UI_STONE, "A collection of stories, frames, and feelings.");
        line_blank(&pre);
        line_rule(&pre, 12);
        line_blank(&pre);

        StrList post;
        strlist_init(&post);
        line_blank(&post);
        char foot[160];
        if (total == 0)
            snprintf(foot, sizeof foot, "THE SHELVES ARE EMPTY" SEP "EVERY COLLECTION BEGINS WITH A SINGLE FILM");
        else
            snprintf(foot, sizeof foot, "%03zu FILMS ARCHIVED" SEP "%03zu WATCHED" SEP "%03zu TO SEE", total,
                     archive_count_status(&app->archive, FILM_WATCHED), to_see);
        line_text(&post, UI_GREY, foot);
        line_italic(&post, UI_ASH, "\xE2\x80\x9CWhat is this world without art?\xE2\x80\x9D");

        UiMenu m = {.section = "Personal cinema archive", .preamble = &pre, .items = items, .count = 9,
                    .selected = sel, .back_label = "Exit", .postamble = &post, .number_keys = true};
        int c = ui_menu(&m);
        strlist_free(&pre);
        strlist_free(&post);
        if (c == UI_BACK || c == 8)
            return;
        sel = c;
        if (c != 1)
            audio_play(SOUND_SECTION);
        switch (c) {
        case 0: screen_collection(app); break;
        case 1: flow_add_film(app, -1, NULL); break;
        case 2: screen_search(app); break;
        case 3: screen_watchlist(app); break;
        case 4: screen_tonight(app); break;
        case 5: screen_journal(app); break;
        case 6: screen_stats(app); break;
        case 7: screen_settings(app); break;
        default: break;
        }
    }
}

/* ================================================================== */
/* Start-up and shutdown                                              */
/* ================================================================== */

static void usage(FILE *fp)
{
    fprintf(fp,
            "Cinephilia %s - a personal archive of cinema\n"
            "\n"
            "Usage: cinephilia [options]\n"
            "\n"
            "  -d, --data PATH     archive file (default: $CINEPHILIA_DATA or %s)\n"
            "      --no-animation  no motion (also: CINEPHILIA_NO_ANIMATION=1)\n"
            "      --no-color      monochrome output (also honours NO_COLOR)\n"
            "      --mute          no sound for this session\n"
            "      --banner        print the title and exit\n"
            "  -h, --help          show this help\n"
            "  -V, --version       show the version\n",
            APP_VERSION, DEFAULT_DATA_PATH);
}

static uint64_t initial_seed(void)
{
    const char *env = getenv("CINEPHILIA_SEED");
    long v;
    if (env && text_parse_int(env, 0, 2147483647L, &v))
        return (uint64_t)v + 1;
    return ((uint64_t)time(NULL) << 20) ^ ((uint64_t)getpid() << 4) ^ (uint64_t)clock();
}

static void load_archive(App *app)
{
    StorageReport rep;
    StorageStatus st = storage_load(app->path, &app->archive, &rep);
    switch (st) {
    case STORAGE_OK:
    case STORAGE_NEW:
        break;
    case STORAGE_RECOVERED: {
        /* Keep the damaged original before the next save rewrites it. */
        char copy[PATH_CAP + 64];
        char stamp[32];
        time_t t = time(NULL);
        struct tm tm;
        if (localtime_r(&t, &tm))
            strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
        else
            text_copy(stamp, sizeof stamp, "copy");
        snprintf(copy, sizeof copy, "%s.damaged-%s", app->path, stamp);
        char err[256];
        if (storage_copy_file(app->path, copy, err, sizeof err))
            ui_flash(UI_TONE_WARN, "%s The original was kept as %s.", rep.message, copy);
        else
            ui_flash(UI_TONE_WARN, "%s (Could not keep a copy: %s.)", rep.message, err);
        break;
    }
    case STORAGE_UNSUPPORTED:
    case STORAGE_IO_ERROR:
        app->read_only = true;
        text_copy(app->read_only_reason, sizeof app->read_only_reason, rep.message);
        ui_flash(UI_TONE_ERROR, "%s", rep.message);
        break;
    }
}

static void on_ui_event(UiEvent e)
{
    audio_play(e == UI_EVENT_MOVE ? SOUND_MOVE : SOUND_ERROR);
}

static void on_intro_start(void)
{
    audio_play(SOUND_STARTUP);
}

/* Sound files are looked for in CINEPHILIA_AUDIO_DIR, next to the
 * executable, then in the current folder. */
static void init_audio(const char *argv0)
{
    static char beside_exe[PATH_CAP];
    const char *dirs[3];
    int n = 0;
    const char *slash = strrchr(argv0, '/');
    if (slash) {
        snprintf(beside_exe, sizeof beside_exe, "%.*s/assets/audio", (int)(slash - argv0), argv0);
        dirs[n++] = beside_exe;
    }
    dirs[n++] = "assets/audio";
    dirs[n] = NULL;
    audio_init(dirs);
}

int main(int argc, char **argv)
{
    const char *data = getenv("CINEPHILIA_DATA");
    if (!data || !*data)
        data = DEFAULT_DATA_PATH;
    UiOptions opts = {false, false};
    bool banner = false;
    bool mute = false;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-d") == 0 || strcmp(a, "--data") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "cinephilia: %s needs a file path\n", a);
                return 2;
            }
            data = argv[++i];
        } else if (strncmp(a, "--data=", 7) == 0) {
            data = a + 7;
        } else if (strcmp(a, "--no-animation") == 0 || strcmp(a, "--no-anim") == 0) {
            opts.no_animation = true;
        } else if (strcmp(a, "--no-color") == 0 || strcmp(a, "--no-colour") == 0) {
            opts.no_color = true;
        } else if (strcmp(a, "--mute") == 0) {
            mute = true;
        } else if (strcmp(a, "--banner") == 0) {
            banner = true;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(a, "-V") == 0 || strcmp(a, "--version") == 0) {
            printf("cinephilia %s (archive format %d)\n", APP_VERSION, STORAGE_FORMAT_VERSION);
            return 0;
        } else {
            fprintf(stderr, "cinephilia: unknown option '%s'\n\n", a);
            usage(stderr);
            return 2;
        }
    }
    if (banner) {
        ui_print_banner();
        return 0;
    }

    static App app; /* large; keep it off the stack */
    archive_init(&app.archive);
    stack_init(&app.undo);
    film_filter_init(&app.filter);
    app.sort = SORT_TITLE;
    app.muted = mute || (getenv("CINEPHILIA_MUTE") && *getenv("CINEPHILIA_MUTE"));
    if (data[0] == '\0' || text_copy(app.path, sizeof app.path, data) != strlen(data)) {
        fprintf(stderr, "cinephilia: the data path is empty or too long\n");
        return 2;
    }
    settings_path_for(app.path, app.settings_path, sizeof app.settings_path);
    if (!settings_load(app.settings_path, &app.settings))
        settings_defaults(&app.settings);
    if (opts.no_animation)
        app.settings.animation = false; /* for this session; not saved unless changed */

    const char *today = getenv("CINEPHILIA_TODAY"); /* reproducible tests */
    if (today && date_valid(today))
        text_copy(app.today, sizeof app.today, today);
    else
        date_today(app.today);
    app.this_year = date_year(app.today);
    rng_seed(&app.rng, initial_seed());

    ui_init(&opts);
    if (ui_interactive()) { /* sound belongs to the full-screen experience */
        init_audio(argv[0]);
        ui_set_signal_cleanup(audio_signal_cleanup);
        ui_set_event_hook(on_ui_event);
    } else {
        audio_set_enabled(false);
    }
    apply_settings(&app);
    if (!ui_interactive())
        audio_set_enabled(false);
    load_archive(&app);

    char count_line[96] = "";
    if (app.archive.count)
        snprintf(count_line, sizeof count_line, "%03zu FILMS IN THE COLLECTION", app.archive.count);
    bool enter = ui_intro(count_line[0] ? count_line : NULL, on_intro_start);
    if (enter)
        screen_home(&app);

    int status = 0;
    if (app.dirty && !app.read_only) {
        char err[512];
        if (storage_save(app.path, &app.archive, err, sizeof err)) {
            app.dirty = false;
        } else {
            status = 1;
            ui_shutdown();
            fprintf(stderr, "cinephilia: could not save to %s: %s\n", app.path, err);
        }
    }
    if (ui_interactive() && status == 0) {
        /* The projector winds down; the lights come up. */
        audio_stop_all();
        audio_play(SOUND_STOP);
        ui_farewell(audio_busy() ? 1400 : (ui_animation() ? 700 : 0));
    }
    audio_stop_all();
    ui_shutdown();
    if (!ui_interactive() && status == 0)
        printf("\n  The lights come up. Until the next screening.\n");
    stack_free(&app.undo, deleted_film_free);
    archive_free(&app.archive);
    return status;
}
