
#include "../include/active_expire.h"
#include <time.h>

void *active_expire(void *arg)
{
    ActiveExpireContext *ctx = arg;

    struct timespec interval = {
        .tv_sec = 1,
        .tv_nsec = 0
    };

    while (!atomic_load(&ctx->stop))
    {
        dict_delete_expired(ctx->dict);
        nanosleep(&interval, NULL);
    }

    return NULL;
}
