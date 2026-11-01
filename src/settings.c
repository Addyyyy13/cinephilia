/*
 * settings.c — preferences file. See settings.h.
 */
#include "settings.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "text.h"

void settings_defaults(Settings *s)
{
    s->animation = true;
    s->sound = true;
    s->volume = SETTINGS_VOLUME_DEFAULT;
}

static bool parse_bool(const char *v, bool *out)
{
    if (text_casecmp(v, "on") == 0 || text_casecmp(v, "true") == 0 || strcmp(v, "1") == 0) {
        *out = true;
        return true;
    }
    if (text_casecmp(v, "off") == 0 || text_casecmp(v, "false") == 0 || strcmp(v, "0") == 0) {
        *out = false;
        return true;
    }
    return false;
}

bool settings_load(const char *path, Settings *s)
{
    settings_defaults(s);
    FILE *fp = fopen(path, "r");
    if (!fp)
        return errno == ENOENT;
    char line[256];
    while (fgets(line, sizeof line, fp)) {
        text_trim(line);
        if (line[0] == '#' || line[0] == '\0')
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        text_trim(key);
        text_trim(val);
        bool b;
        long n;
        if (strcmp(key, "animation") == 0 && parse_bool(val, &b))
            s->animation = b;
        else if (strcmp(key, "sound") == 0 && parse_bool(val, &b))
            s->sound = b;
        else if (strcmp(key, "volume") == 0 && text_parse_int(val, 1, 10, &n))
            s->volume = (int)n;
    }
    bool ok = !ferror(fp);
    fclose(fp);
    return ok;
}

bool settings_save(const char *path, const Settings *s, char *err, size_t errcap)
{
    char tmp[1100];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) {
        snprintf(err, errcap, "path is too long");
        return false;
    }
    /* The folder may not exist yet if nothing has been archived. */
    const char *slash = strrchr(path, '/');
    if (slash && slash != path) {
        char dir[1024];
        snprintf(dir, sizeof dir, "%.*s", (int)(slash - path), path);
        (void)mkdir(dir, 0700);
    }
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        snprintf(err, errcap, "cannot write %s: %s", tmp, strerror(errno));
        return false;
    }
    FILE *fp = fdopen(fd, "w");
    if (!fp) {
        close(fd);
        unlink(tmp);
        snprintf(err, errcap, "cannot write %s", tmp);
        return false;
    }
    int rc = fprintf(fp, "# Cinephilia settings\nversion=1\nanimation=%s\nsound=%s\nvolume=%d\n",
                     s->animation ? "on" : "off", s->sound ? "on" : "off", s->volume);
    bool ok = rc > 0 && fflush(fp) == 0 && fsync(fileno(fp)) == 0;
    if (fclose(fp) != 0)
        ok = false;
    if (!ok || rename(tmp, path) != 0) {
        snprintf(err, errcap, "cannot save %s: %s", path, strerror(errno));
        unlink(tmp);
        return false;
    }
    return true;
}

void settings_path_for(const char *archive_path, char *out, size_t cap)
{
    const char *slash = strrchr(archive_path, '/');
    if (!slash) {
        snprintf(out, cap, "settings.conf");
        return;
    }
    int dir_len = (int)(slash - archive_path);
    snprintf(out, cap, "%.*s/settings.conf", dir_len, archive_path);
}
