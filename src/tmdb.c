/*
 * tmdb.c — TMDB lookups through /usr/bin/curl. See tmdb.h.
 *
 * Each request starts curl with "--config -" and writes the request (URL,
 * timeouts, size limit) to curl's standard input. That keeps the API key
 * out of the command line, where any user could see it with `ps`. curl
 * appends the HTTP status code on its own line after the reply body.
 */
#include "tmdb.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "json.h"
#include "sb.h"
#include "text.h"

extern char **environ;

#define CURL_PATH "/usr/bin/curl"
#define DEFAULT_BASE "https://api.themoviedb.org/3"
#define MAX_REPLY (2 * 1024 * 1024)

/* ------------------------------------------------------------------ */
/* Configuration                                                      */
/* ------------------------------------------------------------------ */

/* TMDB keys are 32 hexadecimal characters; accepting only letters and
 * digits also guarantees the key cannot break out of curl's config. */
static const char *api_key(void)
{
    const char *k = getenv("TMDB_API_KEY");
    if (!k || strlen(k) < 16 || strlen(k) > 64)
        return NULL;
    for (const char *p = k; *p; p++)
        if (!isalnum((unsigned char)*p))
            return NULL;
    return k;
}

/* TMDB_BASE_URL exists so the tests can use a local fake server. */
static const char *base_url(void)
{
    const char *b = getenv("TMDB_BASE_URL");
    if (!b || !*b)
        return DEFAULT_BASE;
    for (const char *p = b; *p; p++)
        if (*p == '"' || *p == '\\' || (unsigned char)*p < 0x20)
            return DEFAULT_BASE;
    return b;
}

static bool curl_present(void)
{
    return access(CURL_PATH, X_OK) == 0;
}

bool tmdb_available(void)
{
    return api_key() != NULL && curl_present();
}

/* ------------------------------------------------------------------ */
/* HTTP through curl                                                  */
/* ------------------------------------------------------------------ */

/* Percent-encodes text for use in a URL query. */
static void url_encode(Sb *sb, const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~')
            sb_putc(sb, (char)*p);
        else
            sb_printf(sb, "%%%02X", *p);
    }
}

static bool write_all(int fd, const char *s, size_t n)
{
    while (n > 0) {
        ssize_t w = write(fd, s, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        s += w;
        n -= (size_t)w;
    }
    return true;
}

/* Fetches `url`. On success returns TMDB_OK with the body in *body (caller
 * frees) and the HTTP status in *status. */
static TmdbResult http_get(const char *url, char **body, int *status)
{
    *body = NULL;
    *status = 0;
    int in[2], out[2];
    if (pipe(in) != 0)
        return TMDB_NETWORK_ERROR;
    if (pipe(out) != 0) {
        close(in[0]);
        close(in[1]);
        return TMDB_NETWORK_ERROR;
    }

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, in[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&fa, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclose(&fa, in[1]);
    posix_spawn_file_actions_addclose(&fa, out[0]);

    /* -q must come first: it stops curl reading the user's ~/.curlrc. */
    char *argv[] = {(char *)CURL_PATH, (char *)"-q", (char *)"--config", (char *)"-", NULL};
    pid_t pid;
    int rc = posix_spawn(&pid, CURL_PATH, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(in[0]);
    close(out[1]);
    if (rc != 0) {
        close(in[1]);
        close(out[0]);
        return TMDB_UNAVAILABLE;
    }

    Sb cfg;
    sb_init(&cfg);
    sb_printf(&cfg,
              "url = \"%s\"\n"
              "silent\n"
              "connect-timeout = 5\n"
              "max-time = 10\n"
              "max-filesize = %d\n"
              "header = \"Accept: application/json\"\n"
              "write-out = \"\\n%%{http_code}\"\n",
              url, MAX_REPLY);
    /* A broken pipe here would kill us, so ignore SIGPIPE while writing. */
    void (*old)(int) = signal(SIGPIPE, SIG_IGN);
    bool sent = !cfg.failed && write_all(in[1], sb_str(&cfg), cfg.len);
    signal(SIGPIPE, old);
    close(in[1]);
    sb_free(&cfg);

    Sb reply;
    sb_init(&reply);
    char buf[8192];
    bool too_big = false;
    for (;;) {
        ssize_t n = read(out[0], buf, sizeof buf);
        if (n == 0)
            break;
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (reply.len + (size_t)n > MAX_REPLY + 16) {
            too_big = true;
            continue; /* keep draining so curl can exit */
        }
        sb_putn(&reply, buf, (size_t)n);
    }
    close(out[0]);
    int wstatus = 0;
    while (waitpid(pid, &wstatus, 0) < 0 && errno == EINTR) {
    }

    if (!sent || !WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0 || reply.failed) {
        sb_free(&reply);
        return TMDB_NETWORK_ERROR; /* no connection, timeout, DNS failure... */
    }
    if (too_big) {
        sb_free(&reply);
        return TMDB_BAD_RESPONSE;
    }

    /* The status code is the last line. */
    char *text = sb_take(&reply);
    if (!text)
        return TMDB_NETWORK_ERROR;
    char *nl = strrchr(text, '\n');
    long code = 0;
    if (!nl || !text_parse_int(nl + 1, 100, 599, &code)) {
        free(text);
        return TMDB_BAD_RESPONSE;
    }
    *nl = '\0';
    *status = (int)code;
    *body = text;
    return TMDB_OK;
}

static TmdbResult status_result(int status)
{
    switch (status) {
    case 200: return TMDB_OK;
    case 401: return TMDB_BAD_KEY;
    case 404: return TMDB_NOT_FOUND;
    case 429: return TMDB_RATE_LIMITED;
    default: return TMDB_BAD_RESPONSE;
    }
}

/* ------------------------------------------------------------------ */
/* Reading TMDB's replies                                             */
/* ------------------------------------------------------------------ */

TmdbResult tmdb_parse_search(const char *json, long *film_id)
{
    JsonNode *root = json_parse(json);
    if (!root)
        return TMDB_BAD_RESPONSE;
    TmdbResult r = TMDB_NOT_FOUND;
    const JsonNode *results = json_get(root, "results");
    if (!results || results->type != JSON_ARRAY) {
        r = TMDB_BAD_RESPONSE;
    } else {
        /* TMDB orders results by relevance: take the first with an id. */
        for (const JsonNode *item = results->child; item; item = item->next) {
            const JsonNode *id = json_get(item, "id");
            if (id && id->type == JSON_NUMBER && id->number > 0) {
                *film_id = (long)id->number;
                r = TMDB_OK;
                break;
            }
        }
    }
    json_free(root);
    return r;
}

/* Stores TMDB text safely: control characters removed, length bounded. */
static void store(char *dst, size_t cap, const char *src)
{
    char tmp[4096];
    text_copy(tmp, sizeof tmp, src ? src : "");
    text_sanitize(tmp);
    text_trim(tmp);
    text_copy(dst, cap, tmp);
}

TmdbResult tmdb_parse_details(const char *json, TmdbFilm *out)
{
    memset(out, 0, sizeof *out);
    JsonNode *root = json_parse(json);
    if (!root)
        return TMDB_BAD_RESPONSE;
    if (root->type != JSON_OBJECT || !json_text(json_get(root, "title"), NULL)) {
        json_free(root);
        return TMDB_BAD_RESPONSE;
    }
    store(out->title, sizeof out->title, json_text(json_get(root, "title"), ""));
    store(out->synopsis, sizeof out->synopsis, json_text(json_get(root, "overview"), ""));

    const char *date = json_text(json_get(root, "release_date"), "");
    if (strlen(date) >= 4 && isdigit((unsigned char)date[0])) {
        int y = atoi(date);
        if (y >= 1878 && y <= 2200)
            out->year = y;
    }

    const JsonNode *first_genre = json_at(json_get(root, "genres"), 0);
    store(out->genre, sizeof out->genre, json_text(json_get(first_genre, "name"), ""));

    /* Present because the request asks for append_to_response=credits. */
    const JsonNode *crew = json_get(json_get(root, "credits"), "crew");
    for (const JsonNode *c = crew ? crew->child : NULL; c; c = c->next) {
        if (strcmp(json_text(json_get(c, "job"), ""), "Director") == 0) {
            store(out->director, sizeof out->director, json_text(json_get(c, "name"), ""));
            break;
        }
    }
    json_free(root);
    return TMDB_OK;
}

/* ------------------------------------------------------------------ */
/* The lookup                                                         */
/* ------------------------------------------------------------------ */

TmdbResult tmdb_lookup(const char *title, int year, TmdbFilm *out)
{
    memset(out, 0, sizeof *out);
    if (!curl_present())
        return TMDB_UNAVAILABLE;
    const char *key = api_key();
    if (!key)
        return TMDB_NO_KEY;

    /* 1. Search by title. */
    Sb url;
    sb_init(&url);
    sb_printf(&url, "%s/search/movie?api_key=%s&include_adult=false&query=", base_url(), key);
    url_encode(&url, title);
    if (year > 0)
        sb_printf(&url, "&primary_release_year=%d", year);
    char *body = NULL;
    int status = 0;
    TmdbResult r = url.failed ? TMDB_NETWORK_ERROR : http_get(sb_str(&url), &body, &status);
    sb_free(&url);
    if (r != TMDB_OK)
        return r;
    r = status_result(status);
    long id = 0;
    if (r == TMDB_OK)
        r = tmdb_parse_search(body, &id);
    free(body);
    if (r != TMDB_OK)
        return r;

    /* 2. Fetch the details of the best match, with its credits. */
    sb_init(&url);
    sb_printf(&url, "%s/movie/%ld?api_key=%s&append_to_response=credits", base_url(), id, key);
    r = url.failed ? TMDB_NETWORK_ERROR : http_get(sb_str(&url), &body, &status);
    sb_free(&url);
    if (r != TMDB_OK)
        return r;
    r = status_result(status);
    if (r == TMDB_OK)
        r = tmdb_parse_details(body, out);
    free(body);
    return r;
}

const char *tmdb_result_text(TmdbResult r)
{
    switch (r) {
    case TMDB_OK: return "Found on TMDB.";
    case TMDB_UNAVAILABLE: return "TMDB lookups need /usr/bin/curl, which was not found.";
    case TMDB_NO_KEY: return "Set TMDB_API_KEY to your TMDB API key to look films up.";
    case TMDB_BAD_KEY: return "TMDB did not accept the API key. Check TMDB_API_KEY.";
    case TMDB_NOT_FOUND: return "TMDB has no film by that title.";
    case TMDB_RATE_LIMITED: return "TMDB asked us to slow down. Try again in a moment.";
    case TMDB_NETWORK_ERROR: return "Could not reach TMDB. Check your internet connection.";
    case TMDB_BAD_RESPONSE: return "TMDB sent a reply that could not be read.";
    }
    return "Unknown TMDB result.";
}
