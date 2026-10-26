/*
 * film.c — Film archive data model and linked-list operations
 * Part of CINEPHILIA: a personal film archive
 */

#include "film.h"
#include <string.h>
#include <stdlib.h>

void archive_init(Archive *ar) {
    ar->head = NULL;
    ar->next_id = 1;
    ar->count = 0;
    ar->modified = 0;
}

void archive_free(Archive *ar) {
    Film *current = ar->head;
    while (current != NULL) {
        Film *temp = current;
        current = current->next;
        free(temp);
    }
    ar->head = NULL;
    ar->count = 0;
    ar->next_id = 1;
    ar->modified = 0;
}

Film *archive_add(Archive *ar, const Film *film) {
    Film *new_film = malloc(sizeof(Film));
    if (new_film == NULL) return NULL;

    *new_film = *film;  /* copy all fields */
    new_film->id = ar->next_id++;
    new_film->next = NULL;

    if (ar->head == NULL) {
        ar->head = new_film;
    } else {
        Film *current = ar->head;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_film;
    }

    ar->count++;
    ar->modified = 1;
    return new_film;
}

Film *archive_find_by_id(Archive *ar, int id) {
    Film *current = ar->head;
    while (current != NULL) {
        if (current->id == id) return current;
        current = current->next;
    }
    return NULL;
}

int archive_delete(Archive *ar, int id) {
    Film *current = ar->head;
    Film *prev = NULL;

    while (current != NULL) {
        if (current->id == id) {
            if (prev == NULL) {
                ar->head = current->next;
            } else {
                prev->next = current->next;
            }
            free(current);
            ar->count--;
            ar->modified = 1;
            return 1;
        }
        prev = current;
        current = current->next;
    }
    return 0;
}

int archive_count(Archive *ar) {
    return ar->count;
}

int archive_count_watched(Archive *ar) {
    int c = 0;
    Film *f = ar->head;
    while (f) { if (f->watched) c++; f = f->next; }
    return c;
}

int archive_count_watchlist(Archive *ar) {
    int c = 0;
    Film *f = ar->head;
    while (f) { if (!f->watched) c++; f = f->next; }
    return c;
}

Film *archive_get_at(Archive *ar, int index) {
    Film *f = ar->head;
    int i = 0;
    while (f) {
        if (i == index) return f;
        i++;
        f = f->next;
    }
    return NULL;
}
