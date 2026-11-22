#define _POSIX_C_SOURCE 200809L

#include "../include/snapshot.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SNAP_MAGIC "KEDIS001"
#define SNAP_MAGIC_LEN 8
#define SNAP_MAX_LEN (512u * 1024u * 1024u)

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static bool write_all(FILE *f, const void *buf, size_t n)
{
    return n == 0 || fwrite(buf, 1, n, f) == n;
}

static bool read_all(FILE *f, void *buf, size_t n)
{
    return n == 0 || fread(buf, 1, n, f) == n;
}

bool snapshot_save(const Dictionary *d, const char *path)
{
    if (d == NULL || path == NULL)
        return false;

    char tmp[4096];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp)
        return false;

    FILE *f = fopen(tmp, "wb");
    if (f == NULL)
        return false;

    int64_t now = now_ms();

    uint64_t live = 0;
    for (size_t i = 0; i < d->capacity; i++)
        for (const Entry *e = d->buckets[i]; e != NULL; e = e->next)
            if (e->expires_at_ms == -1 || e->expires_at_ms > now)
                live++;

    if (!write_all(f, SNAP_MAGIC, SNAP_MAGIC_LEN) ||
        !write_all(f, &live, sizeof live))
        goto fail;

    for (size_t i = 0; i < d->capacity; i++)
    {
        for (const Entry *e = d->buckets[i]; e != NULL; e = e->next)
        {
            if (e->expires_at_ms != -1 && e->expires_at_ms <= now)
                continue;

            uint32_t kl = (uint32_t)e->key_len;
            uint32_t vl = (uint32_t)e->value_len;

            if (!write_all(f, &kl, sizeof kl) ||
                !write_all(f, e->key, e->key_len) ||
                !write_all(f, &vl, sizeof vl) ||
                !write_all(f, e->value, e->value_len) ||
                !write_all(f, &e->expires_at_ms, sizeof e->expires_at_ms))
                goto fail;
        }
    }

    if (fflush(f) != 0 || fsync(fileno(f)) != 0)
        goto fail;
    if (fclose(f) != 0)
    {
        remove(tmp);
        return false;
    }
    if (rename(tmp, path) != 0)
    {
        remove(tmp);
        return false;
    }
    return true;

fail:
    fclose(f);
    remove(tmp);
    return false;
}

bool snapshot_load(Dictionary *d, const char *path)
{
    if (d == NULL || path == NULL)
        return false;

    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return errno == ENOENT;

    char magic[SNAP_MAGIC_LEN];
    uint64_t total = 0;
    if (!read_all(f, magic, SNAP_MAGIC_LEN) ||
        memcmp(magic, SNAP_MAGIC, SNAP_MAGIC_LEN) != 0 ||
        !read_all(f, &total, sizeof total))
    {
        fclose(f);
        return false;
    }

    int64_t now = now_ms();

    for (uint64_t n = 0; n < total; n++)
    {
        uint32_t kl, vl;
        int64_t exp;
        char *key = NULL, *val = NULL;

        if (!read_all(f, &kl, sizeof kl) || kl > SNAP_MAX_LEN)
            goto fail;
        key = malloc((size_t)kl + 1);
        if (key == NULL || !read_all(f, key, kl))
            goto fail;

        if (!read_all(f, &vl, sizeof vl) || vl > SNAP_MAX_LEN)
            goto fail;
        val = malloc((size_t)vl + 1);
        if (val == NULL || !read_all(f, val, vl))
            goto fail;

        if (!read_all(f, &exp, sizeof exp))
            goto fail;

        if (exp == -1 || exp > now)
        {
            if (!dict_set(d, key, kl, val, vl, exp))
                goto fail;
        }

        free(key);
        free(val);
        continue;

    fail:
        free(key);
        free(val);
        fclose(f);
        return false;
    }

    fclose(f);
    return true;
}