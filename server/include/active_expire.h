
#ifndef ACTIVE_EXPIRE_H
#define ACTIVE_EXPIRE_H

#include <pthread.h>
#include <stdatomic.h>
#include "dictionary.h"

typedef struct
{
    Dictionary *dict;
    atomic_bool stop;
} ActiveExpireContext;

void *active_expire(void *arg);

#endif
