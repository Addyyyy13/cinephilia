/*
 * audio.c — controlled playback through afplay. See audio.h.
 */
#include "audio.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "text.h"

extern char **environ;

#define MAX_PLAYERS 4
#define DEFAULT_PLAYER "/usr/bin/afplay"
#define MOVE_GAP_MS 70   /* minimum spacing between navigation clicks */
#define REPEAT_GAP_MS 120 /* minimum spacing for repeats of any other sound */

static const char *const FILES[SOUND_COUNT] = {
    [SOUND_STARTUP] = "startup.wav",       [SOUND_MOVE] = "menu_move.wav",
    [SOUND_SECTION] = "section_open.wav",  [SOUND_FILED] = "film_added.wav",
    [SOUND_CONFIRM] = "confirm.wav",       [SOUND_REEL] = "film_reel.wav",
    [SOUND_REVEAL] = "reveal.wav",         [SOUND_DELETE] = "delete.wav",
    [SOUND_ERROR] = "error.wav",           [SOUND_STOP] = "projector_stop.wav",
};

static const char *const LABELS[SOUND_COUNT] = {
    [SOUND_STARTUP] = "Opening",          [SOUND_MOVE] = "Moving through a menu",
    [SOUND_SECTION] = "Opening a section", [SOUND_FILED] = "Filing a new film",
    [SOUND_CONFIRM] = "Confirmation",     [SOUND_REEL] = "The reel turns",
    [SOUND_REVEAL] = "Tonight's film",    [SOUND_DELETE] = "Deleting",
    [SOUND_ERROR] = "Something's wrong",  [SOUND_STOP] = "Closing",
};

static struct {
    AudioStatus status;
    bool enabled;
    int volume;
    char player[PATH_MAX];
    char dir[PATH_MAX];
    char paths[SOUND_COUNT][PATH_MAX];
    bool present[SOUND_COUNT];
    size_t available;
    long long last_start[SOUND_COUNT];
    size_t spawned;
} A = {AUDIO_NO_PLAYER, true, 4, "", "", {{0}}, {0}, 0, {0}, 0};

/* Written from normal code, read by the signal handler. */
static volatile pid_t g_pids[MAX_PLAYERS];
static Sound g_sounds[MAX_PLAYERS];

static long long now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static bool executable(const char *path)
{
    struct stat st;
    return path[0] == '/' && stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
}

static bool readable_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 44 && access(path, R_OK) == 0;
}

static size_t scan_dir(const char *dir, bool record)
{
    size_t found = 0;
    for (int s = 0; s < SOUND_COUNT; s++) {
        char path[PATH_MAX];
        if (snprintf(path, sizeof path, "%s/%s", dir, FILES[s]) >= (int)sizeof path)
            continue;
        bool ok = readable_file(path);
        if (ok)
            found++;
        if (record) {
            A.present[s] = ok;
            text_copy(A.paths[s], sizeof A.paths[s], path);
        }
    }
    return found;
}

AudioStatus audio_init(const char *const *asset_dirs)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
        g_pids[i] = 0;
    A.available = 0;
    A.dir[0] = '\0';
    memset(A.present, 0, sizeof A.present);

    const char *player = getenv("CINEPHILIA_AUDIO_PLAYER");
    if (!player || !*player)
        player = DEFAULT_PLAYER;
    if (executable(player))
        text_copy(A.player, sizeof A.player, player);
    else
        A.player[0] = '\0';

    const char *env_dir = getenv("CINEPHILIA_AUDIO_DIR");
    if (env_dir && *env_dir) {
        A.available = scan_dir(env_dir, true);
        text_copy(A.dir, sizeof A.dir, env_dir);
    } else {
        for (size_t i = 0; asset_dirs && asset_dirs[i]; i++) {
            if (scan_dir(asset_dirs[i], false) > 0) {
                A.available = scan_dir(asset_dirs[i], true);
                text_copy(A.dir, sizeof A.dir, asset_dirs[i]);
                break;
            }
        }
    }

    if (!A.player[0])
        A.status = AUDIO_NO_PLAYER;
    else if (A.available == 0)
        A.status = AUDIO_NO_ASSETS;
    else
        A.status = AUDIO_READY;
    return A.status;
}

AudioStatus audio_status(void) { return A.status; }
const char *audio_player(void) { return A.player; }
const char *audio_asset_dir(void) { return A.dir; }
size_t audio_available(void) { return A.available; }
size_t audio_spawn_count(void) { return A.spawned; }

const char *audio_file_name(Sound s)
{
    return (s >= 0 && s < SOUND_COUNT) ? FILES[s] : "";
}

const char *audio_sound_label(Sound s)
{
    return (s >= 0 && s < SOUND_COUNT) ? LABELS[s] : "";
}

void audio_set_enabled(bool on)
{
    A.enabled = on;
    if (!on)
        audio_stop_all();
}

bool audio_enabled(void) { return A.enabled; }

void audio_set_volume(int level)
{
    if (level < 1)
        level = 1;
    if (level > 10)
        level = 10;
    A.volume = level;
}

int audio_volume(void) { return A.volume; }

/* Collects finished players without blocking. */
static void reap(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        pid_t pid = g_pids[i];
        if (pid > 0) {
            pid_t r = waitpid(pid, NULL, WNOHANG);
            if (r == pid || (r < 0 && errno == ECHILD))
                g_pids[i] = 0;
        }
    }
}

void audio_play(Sound s)
{
    if (!A.enabled || A.status != AUDIO_READY || s < 0 || s >= SOUND_COUNT || !A.present[s])
        return;
    reap();

    long long t = now_ms();
    long long gap = s == SOUND_MOVE ? MOVE_GAP_MS : REPEAT_GAP_MS;
    if (A.last_start[s] && t - A.last_start[s] < gap)
        return; /* rapid repeats would only smear together */

    int free_slot = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g_pids[i] > 0 && g_sounds[i] == s && s == SOUND_MOVE)
            return; /* one navigation click at a time */
        if (g_pids[i] == 0 && free_slot < 0)
            free_slot = i;
    }
    if (free_slot < 0)
        return; /* never more than MAX_PLAYERS at once */

    char volume[16];
    snprintf(volume, sizeof volume, "%.2f", A.volume / 10.0);
    char *argv[] = {A.player, (char *)"-v", volume, A.paths[s], NULL};

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0)
        return;
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    /* Players start with default signal handling, so our Ctrl+C handler
     * (and its terminal restoration) is never inherited. */
    posix_spawnattr_t attr;
    bool have_attr = posix_spawnattr_init(&attr) == 0;
    if (have_attr) {
        sigset_t defaults;
        sigemptyset(&defaults);
        sigaddset(&defaults, SIGINT);
        sigaddset(&defaults, SIGTERM);
        sigaddset(&defaults, SIGHUP);
        sigaddset(&defaults, SIGQUIT);
        sigaddset(&defaults, SIGTSTP);
        sigaddset(&defaults, SIGWINCH);
        posix_spawnattr_setsigdefault(&attr, &defaults);
        /* Each player gets its own process group, so stopping it also stops
         * anything it started, and the terminal's Ctrl+C reaches only us. */
        posix_spawnattr_setpgroup(&attr, 0);
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETPGROUP);
    }

    pid_t pid = 0;
    int rc = posix_spawn(&pid, A.player, &actions, have_attr ? &attr : NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (have_attr)
        posix_spawnattr_destroy(&attr);
    if (rc != 0)
        return; /* stay silent; the interface does not depend on sound */

    g_sounds[free_slot] = s;
    g_pids[free_slot] = pid;
    A.last_start[s] = t;
    A.spawned++;
}

bool audio_busy(void)
{
    reap();
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (g_pids[i] > 0)
            return true;
    return false;
}

void audio_wait(int max_ms)
{
    long long end = now_ms() + max_ms;
    while (audio_busy() && now_ms() < end) {
        struct timespec ts = {0, 20 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
}

void audio_stop_all(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (g_pids[i] > 0)
            kill(-g_pids[i], SIGTERM); /* the player's whole process group */
    long long end = now_ms() + 300;
    while (audio_busy() && now_ms() < end) {
        struct timespec ts = {0, 10 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g_pids[i] > 0) {
            kill(-g_pids[i], SIGKILL);
            waitpid(g_pids[i], NULL, 0);
            g_pids[i] = 0;
        }
    }
}

void audio_signal_cleanup(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        pid_t pid = g_pids[i];
        if (pid > 0)
            kill(-pid, SIGTERM);
    }
}
