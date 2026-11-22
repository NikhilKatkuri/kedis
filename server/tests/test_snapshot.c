#define _POSIX_C_SOURCE 200809L

#include "../include/dictionary.h"
#include "../include/snapshot.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define S(x) (x), strlen(x)

static int64_t test_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void cleanup_files(const char *path)
{
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    unlink(path);
    unlink(tmp);
}

static void test_missing_file_is_ok(void)
{
    const char *path = "/tmp/kedis_snap_missing.bin";
    cleanup_files(path);

    Dictionary *d = dict_create(8);
    assert(d != NULL);

    /* Loading a nonexistent snapshot should succeed cleanly and leave d empty */
    assert(snapshot_load(d, path));
    assert(d->count == 0);

    dict_free(d);
}

static void test_save_and_load_ttl_and_binary(void)
{
    const char *path = "/tmp/kedis_snap_roundtrip.bin";
    cleanup_files(path);

    int64_t now = test_now_ms();
    Dictionary *src = dict_create(8);
    assert(src != NULL);

    /* Persistent entry */
    assert(dict_set(src, S("perm"), S("val_perm"), -1));

    /* Future TTL entry (expires in 60s) */
    assert(dict_set(src, S("live_ttl"), S("val_live"), now + 60000));

    /* Expired entry (expired 5s ago) */
    assert(dict_set(src, S("expired_key"), S("val_exp"), now - 5000));

    /* Binary key with embedded NUL and empty value */
    assert(dict_set(src, "bin\0key", 7, "", 0, -1));

    assert(snapshot_save(src, path));

    /* Ensure temporary file was renamed away */
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    assert(access(tmp, F_OK) != 0);

    Dictionary *dst = dict_create(8);
    assert(dst != NULL);
    assert(snapshot_load(dst, path));

    Entry *e_perm = dict_get(dst, S("perm"));
    assert(e_perm != NULL);
    assert(e_perm->value_len == 8 && memcmp(e_perm->value, "val_perm", 8) == 0);
    assert(e_perm->expiration_time == -1);

    Entry *e_live = dict_get(dst, S("live_ttl"));
    assert(e_live != NULL);
    assert(e_live->value_len == 8 && memcmp(e_live->value, "val_live", 8) == 0);
    assert(e_live->expiration_time > now);

    /* Expired key should not be restored */
    assert(dict_get(dst, S("expired_key")) == NULL);

    Entry *e_bin = dict_get(dst, "bin\0key", 7);
    assert(e_bin != NULL);
    assert(e_bin->value_len == 0);

    assert(dst->count == 3);

    dict_free(dst);
    dict_free(src);
    cleanup_files(path);
}

static void test_corrupt_magic_rejected(void)
{
    const char *path = "/tmp/kedis_snap_corrupt.bin";
    cleanup_files(path);

    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    fwrite("BADMAGIC", 1, 8, f);
    fclose(f);

    Dictionary *d = dict_create(8);
    assert(d != NULL);
    assert(!snapshot_load(d, path));

    dict_free(d);
    cleanup_files(path);
}

int main(void)
{
    test_missing_file_is_ok();
    test_save_and_load_ttl_and_binary();
    test_corrupt_magic_rejected();
    printf("all snapshot tests passed\n");
    return 0;
}