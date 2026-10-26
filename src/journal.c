/* ─────────────────────────────────────────────────────────────────────
 *  journal.c — Watchlist & Journal features for CINEPHILIA
 *
 *  Provides watchlist management (add, view, mark-as-watched) and
 *  journal entries (write, view) for the personal film archive.
 * ───────────────────────────────────────────────────────────────────── */

#include "journal.h"
#include "ui.h"
#include "storage.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ── helpers ─────────────────────────────────────────────────────────── */

/* Fill buf with today's date in YYYY-MM-DD format. */
static void get_today(char *buf, int maxlen)
{
    time_t t  = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, (size_t)maxlen, "%Y-%m-%d", tm);
}

/* Flush stdin up to and including the next newline (or EOF). */
static void flush_input(void)
{
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {}
}

/* Safe line read: reads up to maxlen-1 chars into buf, strips newline.
 * Returns 1 on success, 0 on EOF / error.                              */
static int read_line(char *buf, int maxlen)
{
    if (fgets(buf, maxlen, stdin) == NULL) return 0;

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';
    return 1;
}

/* Find a film by its ID.  Returns NULL if not found. */
static Film *find_film_by_id(const Archive *ar, int id)
{
    for (int i = 0; i < ar->count; i++) {
        if (ar->films[i].id == id)
            return &ar->films[i];
    }
    return NULL;
}

/* ── watchlist ───────────────────────────────────────────────────────── */

void journal_show_watchlist(Archive *ar)
{
    printf("\n");
    printf(GOLD "  Watchlist" RESET "\n");
    printf(MUTED "  ──────────────────────────────────────" RESET "\n\n");

    int found = 0;
    for (int i = 0; i < ar->count; i++) {
        if (!ar->films[i].watched) {
            ui_print_film_card(&ar->films[i]);
            found++;
        }
    }

    if (!found) {
        printf(MUTED "  Your watchlist is empty. "
               "Time to discover new films." RESET "\n\n");
    }
}

void journal_add_to_watchlist(Archive *ar)
{
    if (ar->count >= MAX_FILMS) {
        printf(CORAL "\n  Archive is full — cannot add more films.\n" RESET);
        return;
    }

    Film *f = &ar->films[ar->count];
    memset(f, 0, sizeof(Film));

    printf("\n" GOLD "  ─── Add to Watchlist ───" RESET "\n\n");

    /* Title */
    printf(IVORY "  Film title: " RESET);
    flush_input();
    if (!read_line(f->title, (int)sizeof(f->title)) || f->title[0] == '\0') {
        printf(MUTED "  Cancelled.\n" RESET);
        return;
    }

    /* Director */
    printf(IVORY "  Director: " RESET);
    if (!read_line(f->director, (int)sizeof(f->director)) ||
        f->director[0] == '\0') {
        printf(MUTED "  Cancelled.\n" RESET);
        return;
    }

    /* Year */
    printf(IVORY "  Release year: " RESET);
    {
        char ybuf[16];
        if (!read_line(ybuf, (int)sizeof(ybuf)) || ybuf[0] == '\0') {
            printf(MUTED "  Cancelled.\n" RESET);
            return;
        }
        if (sscanf(ybuf, "%d", &f->year) != 1 ||
            f->year < 1888 || f->year > 2100) {
            printf(CORAL "  Please enter a valid year (1888–2100).\n" RESET);
            return;
        }
    }

    /* Genre */
    printf(IVORY "  Genre: " RESET);
    if (!read_line(f->genre, (int)sizeof(f->genre)) || f->genre[0] == '\0') {
        strncpy(f->genre, "Unspecified", sizeof(f->genre) - 1);
    }

    /* Defaults for watchlist entry */
    f->id      = ar->next_id++;
    f->rating  = 0.0f;
    f->watched = 0;
    f->watch_date[0]    = '\0';
    f->journal_entry[0] = '\0';

    ar->count++;
    storage_save(ar);

    printf("\n" IVORY "  '%s' added to your watchlist. ♡" RESET "\n\n",
           f->title);
}

void journal_mark_watched(Archive *ar)
{
    printf("\n" GOLD "  ─── Mark as Watched ───" RESET "\n\n");

    /* Show unwatched films */
    int found = 0;
    for (int i = 0; i < ar->count; i++) {
        if (!ar->films[i].watched) {
            printf(MUTED "  [%d] " IVORY "%s" MUTED " — %s (%d)" RESET "\n",
                   ar->films[i].id,
                   ar->films[i].title,
                   ar->films[i].director,
                   ar->films[i].year);
            found++;
        }
    }

    if (!found) {
        printf(MUTED "  No unwatched films. "
               "Add something to your watchlist first." RESET "\n\n");
        return;
    }

    /* Ask for film ID */
    printf("\n" IVORY "  Film ID to mark: " RESET);
    int id;
    if (scanf("%d", &id) != 1) {
        flush_input();
        printf(CORAL "  Invalid input.\n" RESET);
        return;
    }
    flush_input();

    Film *f = find_film_by_id(ar, id);
    if (!f) {
        printf(CORAL "  Film not found.\n" RESET);
        return;
    }
    if (f->watched) {
        printf(MUTED "  '%s' is already marked as watched.\n" RESET,
               f->title);
        return;
    }

    f->watched = 1;

    /* Date — suggest today, allow custom */
    char today[DATE_LEN];
    get_today(today, DATE_LEN);
    printf(IVORY "  Watch date [%s]: " RESET, today);
    {
        char dbuf[DATE_LEN];
        if (read_line(dbuf, DATE_LEN) && dbuf[0] != '\0') {
            strncpy(f->watch_date, dbuf, sizeof(f->watch_date) - 1);
            f->watch_date[sizeof(f->watch_date) - 1] = '\0';
        } else {
            strncpy(f->watch_date, today, sizeof(f->watch_date) - 1);
            f->watch_date[sizeof(f->watch_date) - 1] = '\0';
        }
    }

    /* Optional rating */
    printf(IVORY "  Your rating (0-10, Enter to skip): " RESET);
    {
        char rbuf[16];
        if (read_line(rbuf, (int)sizeof(rbuf)) && rbuf[0] != '\0') {
            float r;
            if (sscanf(rbuf, "%f", &r) == 1 && r >= 0.0f && r <= 10.0f)
                f->rating = r;
        }
    }

    /* Optional journal entry */
    printf(IVORY "  Journal entry (Enter to skip):\n  > " RESET);
    {
        char jbuf[JOURNAL_LEN];
        if (read_line(jbuf, (int)sizeof(jbuf)) && jbuf[0] != '\0') {
            strncpy(f->journal_entry, jbuf, sizeof(f->journal_entry) - 1);
            f->journal_entry[sizeof(f->journal_entry) - 1] = '\0';
        }
    }

    storage_save(ar);

    printf("\n" IVORY "  '%s' — watched. ✓" RESET "\n\n", f->title);
}

/* ── journal ─────────────────────────────────────────────────────────── */

void journal_show(Archive *ar)
{
    printf("\n");
    printf(GOLD "  The Journal" RESET "\n");
    printf(MUTED "  ──────────────────────────────────────" RESET "\n\n");

    int found = 0;
    for (int i = 0; i < ar->count; i++) {
        Film *f = &ar->films[i];
        if (!f->watched) continue;

        ui_print_film_card(f);

        /* Watch date */
        if (f->watch_date[0] != '\0') {
            printf(MUTED "    Watched on  " IVORY "%s" RESET "\n",
                   f->watch_date);
        }

        /* Journal entry */
        if (f->journal_entry[0] != '\0') {
            printf(MUTED "    ── journal ──────────────────" RESET "\n");
            printf(IVORY "    %s" RESET "\n", f->journal_entry);
            printf(MUTED "    ─────────────────────────────" RESET "\n");
        }

        printf("\n");
        found++;
    }

    if (!found) {
        printf(MUTED "  No journal entries yet. "
               "Watch a film and write your thoughts." RESET "\n\n");
    }
}

void journal_write_entry(Archive *ar)
{
    printf("\n" GOLD "  ─── Write a Journal Entry ───" RESET "\n\n");

    /* Show watched films */
    int found = 0;
    for (int i = 0; i < ar->count; i++) {
        if (ar->films[i].watched) {
            printf(MUTED "  [%d] " IVORY "%s" RESET, ar->films[i].id,
                   ar->films[i].title);
            if (ar->films[i].journal_entry[0] != '\0')
                printf(MUTED " (has entry)" RESET);
            printf("\n");
            found++;
        }
    }

    if (!found) {
        printf(MUTED "  No watched films yet. "
               "Mark a film as watched first." RESET "\n\n");
        return;
    }

    /* Ask for film ID */
    printf("\n" IVORY "  Film ID: " RESET);
    int id;
    if (scanf("%d", &id) != 1) {
        flush_input();
        printf(CORAL "  Invalid input.\n" RESET);
        return;
    }
    flush_input();

    Film *f = find_film_by_id(ar, id);
    if (!f) {
        printf(CORAL "  Film not found.\n" RESET);
        return;
    }
    if (!f->watched) {
        printf(MUTED "  '%s' hasn't been watched yet.\n" RESET, f->title);
        return;
    }

    /* Show current entry if any */
    if (f->journal_entry[0] != '\0') {
        printf(MUTED "\n  Current entry:\n" RESET);
        printf(IVORY "  \"%s\"" RESET "\n", f->journal_entry);
        printf(MUTED "  (Writing a new entry will replace this.)\n" RESET);
    }

    /* Write new entry */
    printf(IVORY "\n  Your thoughts:\n  > " RESET);
    {
        char jbuf[JOURNAL_LEN];
        if (read_line(jbuf, (int)sizeof(jbuf)) && jbuf[0] != '\0') {
            strncpy(f->journal_entry, jbuf, sizeof(f->journal_entry) - 1);
            f->journal_entry[sizeof(f->journal_entry) - 1] = '\0';
            storage_save(ar);
            printf(IVORY "\n  Entry saved for '%s'. ✓" RESET "\n\n",
                   f->title);
        } else {
            printf(MUTED "  No changes made.\n" RESET "\n");
        }
    }
}

/* ── submenus ────────────────────────────────────────────────────────── */

void journal_watchlist_menu(Archive *ar)
{
    int choice;

    do {
        printf("\n");
        printf(GOLD "  Watchlist" RESET "\n");
        printf(MUTED "  ──────────────────" RESET "\n");
        printf(CORAL "  1 ─── View watchlist\n");
        printf("  2 ─── Add to watchlist\n");
        printf("  3 ─── Mark as watched\n");
        printf("  0 ─── Back\n" RESET);
        printf(GOLD "\n  > " RESET);

        if (scanf("%d", &choice) != 1) {
            flush_input();
            printf(CORAL "  Please enter a number.\n" RESET);
            continue;
        }

        switch (choice) {
            case 1: journal_show_watchlist(ar); break;
            case 2: journal_add_to_watchlist(ar); break;
            case 3: journal_mark_watched(ar);    break;
            case 0: break;
            default:
                printf(MUTED "  Please choose a valid option.\n" RESET);
        }
    } while (choice != 0);
}

void journal_menu(Archive *ar)
{
    int choice;

    do {
        printf("\n");
        printf(GOLD "  The Journal" RESET "\n");
        printf(MUTED "  ──────────────────" RESET "\n");
        printf(CORAL "  1 ─── View journal entries\n");
        printf("  2 ─── Write a journal entry\n");
        printf("  0 ─── Back\n" RESET);
        printf(GOLD "\n  > " RESET);

        if (scanf("%d", &choice) != 1) {
            flush_input();
            printf(CORAL "  Please enter a number.\n" RESET);
            continue;
        }

        switch (choice) {
            case 1: journal_show(ar);        break;
            case 2: journal_write_entry(ar); break;
            case 0: break;
            default:
                printf(MUTED "  Please choose a valid option.\n" RESET);
        }
    } while (choice != 0);
}
