#ifndef BACKGROUND_SNAPSHOT_H
#define BACKGROUND_SNAPSHOT_H

#include <pthread.h>
#include <stdatomic.h>
#include "dictionary.h"

typedef struct
{
    Dictionary *dict;
    const char *path;
    atomic_bool stop;
} SnapshotContext;

void *snapshot_worker(void *arg);

#endif