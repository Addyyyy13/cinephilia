#ifndef STORAGE_H
#define STORAGE_H

#include "film.h"

#define DATA_DIR      "data"
#define DATA_FILE     "data/films.dat"
#define DATA_FILE_V1  "data/films.txt"
#define DATA_BACKUP   "data/films.txt.bak"
#define STORAGE_HEADER "#CINEPHILIA_V2"

/* Load films from disk into the archive. Returns number of films loaded, or -1 on error. */
int storage_load(Archive *ar);

/* Save all films to disk. Returns 0 on success, -1 on error. */
int storage_save(const Archive *ar);

/* Migrate v1 format (films.txt) to v2 (films.dat). Returns number migrated or -1 on error. */
int storage_migrate_v1(Archive *ar);

/* Ensure data directory exists */
int storage_ensure_dir(void);

#endif /* STORAGE_H */
