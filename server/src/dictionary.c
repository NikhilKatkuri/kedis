#define _POSIX_C_SOURCE 199309L

#include "../include/dictionary.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define LOAD_NUM 3
#define LOAD_DEN 4

#define SHRINK_NUM 1
#define SHRINK_DEN 10

#define DICT_MIN_CAPACITY 4

#include <time.h>
#include <stdint.h>
#include <inttypes.h>

static inline bool dict_has_valid_key(const Dictionary *d, const char *key, size_t key_len)
{
    return d != NULL &&
           key != NULL &&
           key_len > 0 &&
           d->buckets != NULL &&
           d->capacity > 0 &&
           d->count > 0;
}

static int64_t current_time_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return -1;

    return (int64_t)ts.tv_sec * 1000 +
           ts.tv_nsec / 1000000;
}

static size_t next_pow2(size_t n)
{
    size_t p = DICT_MIN_CAPACITY;

    while (p < n)
    {
        if (p > SIZE_MAX / 2)
            return 0;
        p <<= 1;
    }
    return p;
}

static uint64_t hash_function(const char *key, size_t key_len)
{
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < key_len; i++)
    {
        h ^= (unsigned char)key[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static void entry_free(Entry *e)
{
    if (e == NULL)
        return;
    free(e->key);
    free(e->value);
    free(e);
}

static bool dict_needs_grow(size_t count, size_t capacity)
{
    if (count >= SIZE_MAX / LOAD_DEN)
        return true;

    return (count + 1) * LOAD_DEN > capacity * LOAD_NUM;
}

static bool dict_needs_shrink(size_t count, size_t capacity)
{
    if (capacity <= DICT_MIN_CAPACITY)
        return false;

    if (count >= SIZE_MAX / SHRINK_DEN)
        return false;

    return count * SHRINK_DEN < capacity * SHRINK_NUM;
}

Dictionary *dict_create(size_t capacity)
{
    Dictionary *d = calloc(1, sizeof(Dictionary));
    if (d == NULL)
        return NULL;

    d->capacity = next_pow2(capacity);
    if (d->capacity == 0)
    {
        free(d);
        return NULL;
    }

    d->buckets = calloc(d->capacity, sizeof(Entry *));
    if (d->buckets == NULL)
    {
        free(d);
        return NULL;
    }

    if (pthread_mutex_init(&d->mutex, NULL) != 0)
    {
        free(d->buckets);
        free(d);
        return NULL;
    }

    return d;
}

void dict_free(Dictionary *d)
{
    if (d == NULL)
        return;

    pthread_mutex_lock(&d->mutex);

    for (size_t i = 0; i < d->capacity; i++)
    {
        Entry *e = d->buckets[i];
        while (e != NULL)
        {
            Entry *next = e->next;
            entry_free(e);
            e = next;
        }
    }

    pthread_mutex_unlock(&d->mutex);
    pthread_mutex_destroy(&d->mutex);
    free(d->buckets);
    free(d);
}

static Entry *entry_create(const char *key, size_t key_len, const char *value, size_t value_len, int64_t TTL, uint64_t hash)
{
    Entry *e = calloc(1, sizeof(Entry));
    if (e == NULL)
        return NULL;

    e->key = malloc(key_len + 1);
    e->value = malloc(value_len + 1);
    if (e->key == NULL || e->value == NULL)
    {
        entry_free(e);
        return NULL;
    }

    memcpy(e->key, key, key_len);
    e->key[key_len] = '\0';
    memcpy(e->value, value, value_len);
    e->value[value_len] = '\0';

    e->key_len = key_len;
    e->value_len = value_len;

    e->expiration_time = TTL;
    e->hash = hash;

    return e;
}

static bool dict_resize(Dictionary *d, size_t capacity)
{
    if (d == NULL || capacity < DICT_MIN_CAPACITY ||
        (capacity & (capacity - 1)) != 0 ||
        capacity > SIZE_MAX / sizeof(Entry *))
        return false;

    Entry **new_buckets = calloc(capacity, sizeof(Entry *));
    if (new_buckets == NULL)
        return false;

    for (size_t i = 0; i < d->capacity; i++)
    {
        Entry *old_entity = d->buckets[i];
        while (old_entity != NULL)
        {
            Entry *next = old_entity->next;
            size_t idx = old_entity->hash & (capacity - 1);
            old_entity->next = new_buckets[idx];
            new_buckets[idx] = old_entity;
            old_entity = next;
        }
    }

    free(d->buckets);
    d->buckets = new_buckets;
    d->capacity = capacity;
    return true;
}

bool dict_set(Dictionary *d, const char *key, size_t key_len, const char *value, size_t value_len, int64_t TTL)
{
    if (d == NULL || key == NULL || value == NULL || d->capacity == 0)
        return false;

    pthread_mutex_lock(&d->mutex);

    int64_t expiration_time = TTL;

    if (TTL != -1 && TTL <= 0)
    {
        pthread_mutex_unlock(&d->mutex);
        return false;
    }

    uint64_t h = hash_function(key, key_len);
    size_t index = h & (d->capacity - 1);

    for (Entry *e = d->buckets[index]; e != NULL; e = e->next)
    {
        if (e->hash == h && e->key_len == key_len &&
            memcmp(e->key, key, key_len) == 0)
        {
            char *copy = malloc(value_len + 1);

            if (copy == NULL)
            {
                pthread_mutex_unlock(&d->mutex);
                return false;
            }

            memcpy(copy, value, value_len);
            copy[value_len] = '\0';

            free(e->value);
            e->value = copy;
            e->value_len = value_len;
            e->expiration_time = expiration_time;

            pthread_mutex_unlock(&d->mutex);
            return true;
        }
    }

    if (dict_needs_grow(d->count, d->capacity))
    {
        if (d->capacity > SIZE_MAX / 2)
        {
            pthread_mutex_unlock(&d->mutex);
            return false;
        }

        if (!dict_resize(d, d->capacity * 2))
        {
            pthread_mutex_unlock(&d->mutex);
            return false;
        }
    }

    index = h & (d->capacity - 1);

    Entry *e = entry_create(
        key, key_len, value, value_len, expiration_time, h);

    if (e == NULL)
    {
        pthread_mutex_unlock(&d->mutex);
        return false;
    }

    e->next = d->buckets[index];
    d->buckets[index] = e;
    d->count++;

    pthread_mutex_unlock(&d->mutex);
    return true;
}

Entry *dict_get(Dictionary *d, const char *key, size_t key_len)
{
    if (!dict_has_valid_key(d, key, key_len))
        return false;

    pthread_mutex_lock(&d->mutex);

    uint64_t h = hash_function(key, key_len);
    size_t index = h & (d->capacity - 1);

    Entry **pp = &d->buckets[index];

    while (*pp != NULL)
    {
        Entry *e = *pp;

        if (e->hash == h &&
            e->key_len == key_len &&
            memcmp(e->key, key, key_len) == 0)
        {
            int64_t now = current_time_ms();

            if (now < 0)
            {
                pthread_mutex_unlock(&d->mutex);
                return NULL;
            }
            if (e->expiration_time != -1 && e->expiration_time <= now)
            {
                *pp = e->next;
                entry_free(e);
                d->count--;

                pthread_mutex_unlock(&d->mutex);
                return NULL;
            }

            pthread_mutex_unlock(&d->mutex);
            return e;
        }

        pp = &e->next;
    }

    pthread_mutex_unlock(&d->mutex);
    return NULL;
}

bool dict_delete(Dictionary *d, const char *key, size_t key_len)
{
    if (!dict_has_valid_key(d, key, key_len))
        return false;
    pthread_mutex_lock(&d->mutex);

    uint64_t h = hash_function(key, key_len);
    size_t index = h & (d->capacity - 1);

    Entry **pp = &d->buckets[index];
    while (*pp != NULL)
    {
        Entry *e = *pp;
        if (e->hash == h &&
            e->key_len == key_len &&
            memcmp(e->key, key, key_len) == 0)
        {
            *pp = e->next;
            entry_free(e);
            d->count--;

            if (dict_needs_shrink(d->count, d->capacity))
                dict_resize(d, d->capacity / 2);
            pthread_mutex_unlock(&d->mutex);
            return true;
        }
        pp = &e->next;
    }
    pthread_mutex_unlock(&d->mutex);

    return false;
}

size_t dict_count(Dictionary *d)
{
    if (d == NULL)
        return 0;

    pthread_mutex_lock(&d->mutex);

    size_t count = d->count;

    pthread_mutex_unlock(&d->mutex);

    return count;
}

const char *dict_stats(Dictionary *d)
{
    static _Thread_local char json[512];

    if (d == NULL)
    {
        snprintf(json, sizeof(json),
                 "{\"error\":\"invalid dictionary\"}");
        return json;
    }

    pthread_mutex_lock(&d->mutex);

    size_t occupied = 0;
    size_t used_bytes = 0;

    size_t bucket_bytes = d->capacity * sizeof(Entry *);

    for (size_t i = 0; i < d->capacity; i++)
    {
        Entry *entry = d->buckets[i];

        if (entry != NULL)
            occupied++;

        while (entry != NULL)
        {
            used_bytes += sizeof(Entry);
            used_bytes += entry->key_len + 1;
            used_bytes += entry->value_len + 1;
            entry = entry->next;
        }
    }

    size_t allocated_bytes =
        sizeof(Dictionary) + bucket_bytes + used_bytes;

    double load_factor = d->capacity
                             ? (double)d->count / d->capacity
                             : 0.0;

    snprintf(json, sizeof(json),
             "{\"capacity\":%zu,\"occupied\":%zu,"
             "\"count\":%zu,\"load_factor\":%.4f,"
             "\"allocated_bytes\":%zu,\"used_bytes\":%zu,"
             "\"bucket_bytes\":%zu}",
             d->capacity, occupied, d->count, load_factor,
             allocated_bytes, used_bytes, bucket_bytes);

    pthread_mutex_unlock(&d->mutex);

    return json;
}

void dict_delete_expired(Dictionary *d)
{
    if (!d || !d->buckets || d->capacity == 0)
        return;

    pthread_mutex_lock(&d->mutex);

    int64_t now = current_time_ms();

    if (now < 0)
    {
        pthread_mutex_unlock(&d->mutex);
        return;
    }

    for (size_t i = 0; i < d->capacity; i++)
    {
        Entry **pp = &d->buckets[i];

        while (*pp != NULL)
        {
            Entry *e = *pp;
            if (e->expiration_time != -1 &&
                e->expiration_time <= now)
            {
                *pp = e->next;
                entry_free(e);
                d->count--;
            }
            else
            {
                pp = &e->next;
            }
        }
    }

    if (dict_needs_shrink(d->count, d->capacity))
        dict_resize(d, d->capacity / 2);

    pthread_mutex_unlock(&d->mutex);
}

Entry **dict_find_slot(Dictionary *d, const char *key, size_t key_len)
{
    if (!dict_has_valid_key(d, key, key_len))
    {
        return NULL;
    }

    uint64_t h = hash_function(key, key_len);
    size_t index = h & (d->capacity - 1);

    Entry **pp = &d->buckets[index];
    while (*pp != NULL)
    {
        Entry *curr = *pp;

        if (strcmp(curr->key, key) == 0)
        {
            return pp;
        }

        pp = &curr->next;
    }

    return NULL;
}

bool dict_key_exists(Dictionary *d, const char *key, size_t key_len)
{
    return dict_find_slot(d, key, key_len) != NULL;
}

bool dict_remove_key_expiration(Dictionary *d, const char *key, size_t key_len)
{
    if (d == NULL)
    {
        return false;
    }
    pthread_mutex_lock(&d->mutex);
    Entry **slot = dict_find_slot(d, key, key_len);
    bool removed = false;
    if (slot != NULL)
    {
        Entry *entry = *slot;
        if (entry->expiration_time != -1)
        {
            entry->expiration_time = -1;
            removed = true;
        }
    }

    pthread_mutex_unlock(&d->mutex);
    return removed;
}

bool dict_flush(Dictionary *d)
{
    if (d == NULL)
    {
        return false;
    }
    pthread_mutex_lock(&d->mutex);
    if (d->buckets != NULL)
    {
        for (size_t i = 0; i < d->capacity; i++)
        {
            Entry *curr = d->buckets[i];
            while (curr != NULL)
            {
                Entry *next = curr->next;
                free(curr->key);
                free(curr->value);
                free(curr);
                curr = next;
            }
            d->buckets[i] = NULL;
        }
    }

    d->count = 0;
    pthread_mutex_unlock(&d->mutex);
    return true;
}
