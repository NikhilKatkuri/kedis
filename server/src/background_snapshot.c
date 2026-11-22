#define _POSIX_C_SOURCE 200809L

#include "background_snapshot.h"
#include "snapshot.h"

#include <stdio.h>
#include <time.h>

void *snapshot_worker(void *arg)
{
    SnapshotContext *ctx = arg;

    if (ctx == NULL || ctx->dict == NULL || ctx->path == NULL)
        return NULL;

    while (!atomic_load(&ctx->stop))
    {
        for (int i = 0; i < 60; i++)
        {
            if (atomic_load(&ctx->stop))
                return NULL;

            struct timespec interval = {
                .tv_sec = 1,
                .tv_nsec = 0};

            nanosleep(&interval, NULL);
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