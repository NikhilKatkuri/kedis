#define _POSIX_C_SOURCE 200809L

#include "../include/active_expire.h"
#include <time.h>

#define EXPIRE_INTERVAL_TICKS 10
#define TICK_MS 100

void *active_expire(void *arg)
{
    ActiveExpireContext *ctx = arg;

    struct timespec tick = {
        .tv_sec = 0,
        .tv_nsec = TICK_MS * 1000000L};

    while (!atomic_load(&ctx->stop))
    {
        dict_delete_expired(ctx->dict);

        for (int i = 0; i < EXPIRE_INTERVAL_TICKS; i++)
        {
            if (atomic_load(&ctx->stop))
                return NULL;

            nanosleep(&tick, NULL);
        }
    }

    return NULL;
}