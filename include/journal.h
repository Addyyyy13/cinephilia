#ifndef JOURNAL_H
#define JOURNAL_H

#include "film.h"

/* Mark a film as watched with date and optional journal entry */
void journal_mark_watched(Archive *ar);

/* Add a film directly to the watchlist */
void journal_add_to_watchlist(Archive *ar);

/* Write/edit a journal entry for a film */
void journal_write_entry(Archive *ar);

/* Display the watchlist (unwatched films) */
void journal_show_watchlist(Archive *ar);

/* Display the journal (watched films with notes) */
void journal_show(Archive *ar);

/* Watchlist menu */
void journal_watchlist_menu(Archive *ar);

/* Journal menu */
void journal_menu(Archive *ar);

#endif /* JOURNAL_H */
