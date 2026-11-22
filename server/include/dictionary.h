#ifndef DICTIONARY_H
#define DICTIONARY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#define INITIAL_CAPCITY 4

typedef struct Entry
{
    char *key;
    size_t key_len;
    char *value;
    size_t value_len;
    int64_t expiration_time; // monotonic milliseconds; -1 = no expiration;
    uint64_t hash;
    struct Entry *next;
} Entry;

typedef struct
{
    Entry **buckets;
    size_t capacity;
    size_t count;
    pthread_mutex_t mutex;

} Dictionary;

/* Creates a dictionary. capacity is rounded up to a power of two (min 4). */
Dictionary *dict_create(size_t capacity);

/* Frees the dictionary and every entry in it. */
void dict_free(Dictionary *d);

/* Inserts or overwrites a key. TTL = -1 means no TTL. */
bool dict_set(Dictionary *d, const char *key, size_t key_len, const char *value, size_t value_len, int64_t TTL);

/* Returns the entry or NULL. The entry belongs to the dictionary. */
Entry *dict_get(Dictionary *d, const char *key, size_t key_len);

/* Deletes a key. Returns true if it existed. */
bool dict_delete(Dictionary *d, const char *key, size_t key_len);

/* Deletes all expired keys. */
void dict_delete_expired(Dictionary *d);

/** return count of dictionary items*/
size_t dict_count(Dictionary *d);

/* size of dictionary */
const char *dict_stats(Dictionary *d);

/** Returns the key if it exists, otherwise returns NULL. */
bool dict_key_exists(Dictionary *d, const char *key, size_t key_len);

/** remove exipration */
bool dict_remove_key_expiration(Dictionary *d, const char *key, size_t key_len);

// flush
bool dict_flush(Dictionary *d);

/** find a slot */
Entry **dict_find_slot(Dictionary *d, const char *key, size_t key_len);

#endif /* DICTIONARY_H */