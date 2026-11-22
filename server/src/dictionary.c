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

static size_t next_pow2(size_t n)
{
    size_t p = DICT_MIN_CAPACITY;
    while (p < n)
        p <<= 1;
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
    return (count + 1) * LOAD_DEN > capacity * LOAD_NUM;
}

static bool dict_needs_shrink(size_t count, size_t capacity)
{
    return capacity > DICT_MIN_CAPACITY &&
           count * SHRINK_DEN < capacity * SHRINK_NUM;
}

Dictionary *dict_create(size_t capacity)
{
    Dictionary *d = calloc(1, sizeof(Dictionary));
    if (d == NULL)
    {
        return NULL;
    }

    d->capacity = next_pow2(capacity);
    d->count = 0;
    d->buckets = calloc(d->capacity, sizeof(Entry *));
    if (d->buckets == NULL)
    {
        free(d);
        return NULL;
    }

    return d;
}

void dict_free(Dictionary *d)
{
    if (d == NULL)
        return;
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
    free(d->buckets);
    free(d);
}

static Entry *entry_create(const char *key, size_t key_len, const char *value, size_t value_len, int64_t expires_at_ms, uint64_t hash)
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

    e->expires_at_ms = expires_at_ms;
    e->hash = hash;

    return e;
}

static bool dict_resize(Dictionary *d, size_t capacity)
{
    if (d == NULL || capacity < DICT_MIN_CAPACITY ||
        (capacity & (capacity - 1)) != 0)
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

bool dict_set(Dictionary *d, const char *key, size_t key_len, const char *value, size_t value_len, int64_t expires_at_ms)
{
    if (d == NULL || key == NULL || value == NULL || d->capacity == 0)
        return false;

    uint64_t h = hash_function(key, key_len);
    size_t index = h & (d->capacity - 1);

    for (Entry *e = d->buckets[index]; e != NULL; e = e->next)
    {
        if (e->hash == h && e->key_len == key_len &&
            memcmp(e->key, key, key_len) == 0)
        {
            char *copy = malloc(value_len + 1);
            if (copy == NULL)
                return false;
            memcpy(copy, value, value_len);
            copy[value_len] = '\0';

            free(e->value);
            e->value = copy;
            e->value_len = value_len;
            e->expires_at_ms = expires_at_ms;
            return true;
        }
    }

    if (dict_needs_grow(d->count, d->capacity))
        dict_resize(d, d->capacity * 2);
    index = h & (d->capacity - 1);

    Entry *e = entry_create(key, key_len, value, value_len, expires_at_ms, h);
    if (e == NULL)
        return false;

    e->next = d->buckets[index];
    d->buckets[index] = e;
    d->count++;
    return true;
}

Entry *dict_get(Dictionary *d, const char *key, size_t key_len)
{
    if (d == NULL || key == NULL || d->capacity == 0 || d->count == 0)
        return NULL;

    uint64_t h = hash_function(key, key_len);
    size_t index = h & (d->capacity - 1);

    for (Entry *e = d->buckets[index]; e != NULL; e = e->next)
    {
        if (e->hash == h &&
            e->key_len == key_len &&
            memcmp(e->key, key, key_len) == 0)
            return e;
    }
    return NULL;
}

bool dict_delete(Dictionary *d, const char *key, size_t key_len)
{
    if (d == NULL || key == NULL || d->capacity == 0 || d->count == 0)
        return false;

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
            printf("{\n"
                   "  \"event\": \"deleted\",\n"
                   "  \"key\": \"%.*s\",\n"
                   "  \"value\": \"%.*s\"\n"
                   "}\n",
                   (int)e->key_len, e->key,
                   (int)e->value_len, e->value);
            *pp = e->next;
            entry_free(e);
            d->count--;

            if (dict_needs_shrink(d->count, d->capacity))
                dict_resize(d, d->capacity / 2);

            return true;
        }
        pp = &e->next;
    }
    return false;
}
