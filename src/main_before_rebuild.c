
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESET    "\033[0m"
#define BURGUNDY "\033[38;5;131m"
#define GOLD     "\033[38;5;179m"
#define CREAM    "\033[38;5;230m"
#define MUTED    "\033[38;5;245m"

#define FILE_NAME "data/films.txt"

typedef struct Film {
    int id;
    char title[100];
    char director[100];
    int year;
    float rating;
    struct Film *next;
} Film;

Film *head = NULL;
int nextId = 1;

void saveFilms(void) {
    FILE *file = fopen(FILE_NAME, "w");

    if (file == NULL) {
        printf("\nCould not save the archive.\n");
        return;
    }

    Film *current = head;

    while (current != NULL) {
        fprintf(file, "%d|%s|%s|%d|%.1f\n",
                current->id,
                current->title,
                current->director,
                current->year,
                current->rating);

        current = current->next;
    }

    fclose(file);
}

void loadFilms(void) {
    FILE *file = fopen(FILE_NAME, "r");

    if (file == NULL) {
        return;
    }

    char line[300];

    while (fgets(line, sizeof(line), file) != NULL) {
        Film *newFilm = malloc(sizeof(Film));

        if (newFilm == NULL) {
            break;
        }

        if (sscanf(line, "%d|%99[^|]|%99[^|]|%d|%f",
                   &newFilm->id,
                   newFilm->title,
                   newFilm->director,
                   &newFilm->year,
                   &newFilm->rating) != 5) {
            free(newFilm);
            continue;
        }

        newFilm->next = NULL;

        if (head == NULL) {
            head = newFilm;
        } else {
            Film *current = head;

            while (current->next != NULL) {
                current = current->next;
            }

            current->next = newFilm;
        }

        if (newFilm->id >= nextId) {
            nextId = newFilm->id + 1;
        }
    }

    fclose(file);
}

void addFilm(void) {
    Film *newFilm = malloc(sizeof(Film));

    if (newFilm == NULL) {
        printf("\nMemory allocation failed.\n");
        return;
    }

    newFilm->id = nextId++;

    printf("\n%s--- A NEW STORY --- %s\n", GOLD, RESET);

    printf("Film title: ");
    if (scanf(" %99[^\n]", newFilm->title) != 1) {
        free(newFilm);
        return;
    }

    printf("Director: ");
    if (scanf(" %99[^\n]", newFilm->director) != 1) {
        free(newFilm);
        return;
    }

    printf("Release year: ");
    if (scanf("%d", &newFilm->year) != 1 ||
        newFilm->year < 1888 || newFilm->year > 2100) {
        printf("Please enter a valid release year.\n");
        while (getchar() != '\n') {}
        free(newFilm);
        return;
    }

    printf("Your rating (0-10): ");
    if (scanf("%f", &newFilm->rating) != 1 ||
        newFilm->rating < 0 || newFilm->rating > 10) {
        printf("Rating must be between 0 and 10.\n");
        while (getchar() != '\n') {}
        free(newFilm);
        return;
    }

    newFilm->next = NULL;

    if (head == NULL) {
        head = newFilm;
    } else {
        Film *current = head;

        while (current->next != NULL) {
            current = current->next;
        }

        current->next = newFilm;
    }

    saveFilms();

    printf("\n%s'%s' has entered your archive. ♡%s\n",
           CREAM, newFilm->title, RESET);
}

void browseArchive(void) {
    Film *current = head;

    printf("\n%s========== THE ARCHIVE ==========%s\n",
           GOLD, RESET);

    if (current == NULL) {
        printf("The archive is empty. Add your first film!\n");
        return;
    }

    while (current != NULL) {
        printf("\n%s[%d] %s%s\n",
               BURGUNDY, current->id, current->title, RESET);

        printf("    Director : %s\n", current->director);
        printf("    Year     : %d\n", current->year);
        printf("    Rating   : %.1f/10\n", current->rating);

        current = current->next;
    }

    printf("\n%sEnd of the collection.%s\n", GOLD, RESET);
}

void displayMenu(void) {
    printf("\n%s", GOLD);
    printf("  ========================================\n");
    printf("             C I N E P H I L I A .\n");
    printf("  ========================================\n");
    printf("%s", RESET);

    printf("%s  What is this world without art?%s\n\n",
           CREAM, RESET);

    printf("%s  [1]  Browse the archive\n", BURGUNDY);
    printf("  [2]  Add a film\n");
    printf("  [3]  My watchlist\n");
    printf("  [4]  The journal\n");
    printf("  [5]  After dark: random pick\n");
    printf("  [6]  Viewing statistics\n");
    printf("  [0]  Leave quietly\n%s", RESET);

    printf("\n%s  Choose a chapter > %s", GOLD, RESET);
}

void freeArchive(void) {
    Film *current = head;

    while (current != NULL) {
        Film *temp = current;
        current = current->next;
        free(temp);
    }

    head = NULL;
}

int main(void) {
    int choice;

    loadFilms();

    printf("\033[2J\033[H");

    printf("%s  YOUR PERSONAL FILM ARCHIVE\n", MUTED);
    printf("  A little corner of the cinematic universe.%s\n",
           RESET);

    do {
        displayMenu();

        if (scanf("%d", &choice) != 1) {
            printf("\nPlease enter a number.\n");

            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF) {}

            continue;
        }

        switch (choice) {
            case 1:
                browseArchive();
                break;

            case 2:
                addFilm();
                break;

            case 3:
                printf("\nYour watchlist is coming soon.\n");
                break;

            case 4:
                printf("\nYour journal is coming soon.\n");
                break;

            case 5:
                printf("\nMidnight roulette is coming soon.\n");
                break;

            case 6:
                printf("\nViewing statistics are coming soon.\n");
                break;

            case 0:
                printf("\n%sUntil the next screening. *%s\n",
                       GOLD, RESET);
                break;

            default:
                printf("\nPlease choose a valid option.\n");
        }

    } while (choice != 0);

    freeArchive();
    return 0;
}
