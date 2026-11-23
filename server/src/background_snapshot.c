#define _POSIX_C_SOURCE 200809L

#include "background_snapshot.h"
#include "snapshot.h"

#include <stdio.h>
#include <time.h>

#define TICK_MS 100
#define SNAPSHOT_INTERVAL_TICKS (60 * 1000 / TICK_MS)

void *snapshot_worker(void *arg)
{
    SnapshotContext *ctx = arg;

    if (ctx == NULL || ctx->dict == NULL || ctx->path == NULL)
        return NULL;

    while (!atomic_load(&ctx->stop))
    {
        for (int i = 0; i < SNAPSHOT_INTERVAL_TICKS; i++)
        {
            if (atomic_load(&ctx->stop))
                return NULL;

            struct timespec tick = {
                .tv_sec = 0,
                .tv_nsec = TICK_MS * 1000000L};

            nanosleep(&tick, NULL);
        }

        if (atomic_load(&ctx->stop))
            break;

        if (!snapshot_save(ctx->dict, ctx->path))
        {
            fprintf(stderr, "Background snapshot saving failed\n");
        }
        else
        {
            fprintf(stderr, "Background snapshot saved\n");
        }
    }

    return NULL;
}