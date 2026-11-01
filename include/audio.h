/*
 * audio.h — optional, non-blocking sound effects.
 *
 * Playback hands each sound to macOS's built-in `afplay` (/usr/bin/afplay),
 * started with posix_spawn and an argument vector (no shell, so paths with
 * spaces or odd characters are safe). Cinephilia never waits for a sound,
 * never runs more than a few players at once, drops repeated navigation
 * clicks, and terminates every player it started when it exits, including
 * on Ctrl+C.
 *
 * If afplay is missing (any non-macOS system), or a sound file is missing,
 * the corresponding sounds are silently skipped: the application never
 * depends on audio. CINEPHILIA_AUDIO_PLAYER may name another player that
 * accepts afplay's arguments ("-v VOLUME FILE"); the tests use this.
 *
 * The sounds themselves are original and synthesised by tools/gen_sounds.c.
 */
#ifndef CINEPHILIA_AUDIO_H
#define CINEPHILIA_AUDIO_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SOUND_STARTUP,  /* projector spin-up, shutter flutter, warm chord */
    SOUND_MOVE,     /* a soft, muted switch */
    SOUND_SECTION,  /* a page turned in a catalogue drawer */
    SOUND_FILED,    /* a card slid into the drawer */
    SOUND_CONFIRM,  /* a quiet two-note accent */
    SOUND_REEL,     /* a reel turning and slowing */
    SOUND_REVEAL,   /* a gentle chime as tonight's film appears */
    SOUND_DELETE,   /* a neutral, soft close */
    SOUND_ERROR,    /* two low taps */
    SOUND_STOP,     /* the projector winding down */
    SOUND_COUNT
} Sound;

typedef enum {
    AUDIO_READY,     /* player found and at least one sound file present */
    AUDIO_NO_PLAYER, /* afplay (or the configured player) not available */
    AUDIO_NO_ASSETS  /* player found, but no sound files */
} AudioStatus;

/* Finds the player and the sound files. asset_dirs is a NULL-terminated
 * list of folders to try in order; CINEPHILIA_AUDIO_DIR overrides it. */
AudioStatus audio_init(const char *const *asset_dirs);
AudioStatus audio_status(void);
const char *audio_player(void);    /* "" when none */
const char *audio_asset_dir(void); /* "" when none was found */
size_t audio_available(void);      /* sound files found, out of SOUND_COUNT */
const char *audio_file_name(Sound s);
const char *audio_sound_label(Sound s);

void audio_set_enabled(bool on);
bool audio_enabled(void);
void audio_set_volume(int level); /* 1..10; afplay volume = level / 10 */
int audio_volume(void);

/* Starts a sound and returns at once. Does nothing when disabled or
 * unavailable, or when it would only pile up on the same sound. */
void audio_play(Sound s);
bool audio_busy(void);           /* is anything still playing? */
void audio_wait(int max_ms);     /* waits for players to finish, up to max_ms */
void audio_stop_all(void);       /* terminates and reaps every player */
void audio_signal_cleanup(void); /* async-signal-safe: terminates players */
size_t audio_spawn_count(void);  /* players started so far (for tests) */

#endif
