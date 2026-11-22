#ifndef DICTIONARY_H
#define DICTIONARY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define INITIAL_CAPCITY 4

typedef struct Entry
{
    char *key;
    size_t key_len;
    char *value;
    size_t value_len;
    int64_t expires_at_ms;
    uint64_t hash;
    struct Entry *next;
} Entry;

typedef struct
{
    Entry **buckets;
    size_t capacity;
    size_t count;
} Dictionary;

/* Creates a dictionary. capacity is rounded up to a power of two (min 4). */
Dictionary *dict_create(size_t capacity);

/* Frees the dictionary and every entry in it. */
void dict_free(Dictionary *d);

/* Inserts or overwrites a key. expires_at_ms = -1 means no TTL. */
bool dict_set(Dictionary *d, const char *key, size_t key_len, const char *value, size_t value_len, int64_t expires_at_ms);

/* Returns the entry or NULL. The entry belongs to the dictionary. */
Entry *dict_get(Dictionary *d, const char *key, size_t key_len);

/* Deletes a key. Returns true if it existed. */
bool dict_delete(Dictionary *d, const char *key, size_t key_len);

#endif /* DICTIONARY_H */