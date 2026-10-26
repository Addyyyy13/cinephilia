/*
 * CINEPHILIA — A Terminal Film Archive
 * main.c — Application entry point
 *
 * Sets up terminal, loads persisted archive, runs the main menu loop,
 * and ensures graceful cleanup on exit or interrupt.
 */

#include "film.h"
#include "storage.h"
#include "ui.h"
#include "search.h"
#include "journal.h"
#include "discovery.h"
#include "stats.h"
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>

/* ── Global archive ────────────────────────────────────────────────── */

Archive archive;

/* ── Signal handling ───────────────────────────────────────────────── */

static volatile int running = 1;

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* ── Cleanup (registered with atexit) ──────────────────────────────── */

static void cleanup(void) {
    ui_restore_terminal();
    archive_free(&archive);
}

/* ── Main ──────────────────────────────────────────────────────────── */

int main(void) {
    atexit(cleanup);
    signal(SIGINT, signal_handler);

    ui_setup_terminal();
    storage_ensure_dir();
    archive_init(&archive);

    int loaded = storage_load(&archive);
    if (loaded > 0) {
        printf("\033[38;2;218;165;32m  ▸ Loaded %d film%s from archive.\033[0m\n",
               loaded, loaded == 1 ? "" : "s");
    }

    ui_opening_animation();

    while (running) {
        int choice = ui_main_menu();

        switch (choice) {
            case 1: /* Browse archive */
                ui_print_film_list(&archive, -1);
                if (archive.count > 0) {
                    ui_view_film_detail(&archive);
                }
                ui_press_enter();
                break;

            case 2: /* Add film */
                ui_add_film_form(&archive);
                break;

            case 3: /* Watchlist */
                journal_watchlist_menu(&archive);
                break;

            case 4: /* Journal */
                journal_menu(&archive);
                break;

            case 5: /* Discovery */
                discovery_menu(&archive);
                break;

            case 6: /* Search */
                search_menu(&archive);
                break;

            case 7: /* Statistics */
                stats_display(&archive);
                ui_press_enter();
                break;

            case 8: /* Edit film */
                ui_edit_film_form(&archive);
                break;

            case 9: /* Delete film */
                ui_delete_film(&archive);
                break;

            case 0: /* Exit */
                printf("\n\033[38;2;218;165;32m  Until the next screening.\033[0m\n\n");
                running = 0;
                break;

            default:
                printf("\033[31m  ✗ Invalid selection.\033[0m\n");
                break;
        }

        if (!running) break;
    }

    return 0;
}
