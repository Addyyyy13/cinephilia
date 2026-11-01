/*
 * settings.h — user preferences, kept in a small text file next to the
 * archive ("settings.conf"):
 *
 *   # Cinephilia settings
 *   version=1
 *   animation=on
 *   sound=on
 *   volume=4
 *
 * Unknown keys and malformed lines are ignored, and a missing file means
 * defaults, so the file can never prevent the application from starting.
 */
#ifndef CINEPHILIA_SETTINGS_H
#define CINEPHILIA_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

#define SETTINGS_VOLUME_DEFAULT 4

typedef struct {
    bool animation;
    bool sound;
    int volume; /* 1..10 */
} Settings;

void settings_defaults(Settings *s);
/* Fills s from the file, starting from defaults. False only on a read
 * error (a missing file is not an error). */
bool settings_load(const char *path, Settings *s);
bool settings_save(const char *path, const Settings *s, char *err, size_t errcap);
/* "<folder of archive>/settings.conf" */
void settings_path_for(const char *archive_path, char *out, size_t cap);

#endif
