#include "../include/dictionary.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define S(x) (x), strlen(x)

static void test_set_get_overwrite(void)
{
    Dictionary *d = dict_create(4);
    assert(d);

    assert(dict_set(d, S("a"), S("1"), -1));
    Entry *e = dict_get(d, S("a"));
    assert(e && e->value_len == 1 && memcmp(e->value, "1", 1) == 0);

    assert(dict_set(d, S("a"), S("22"), -1)); /* overwrite */
    e = dict_get(d, S("a"));
    assert(e && e->value_len == 2 && memcmp(e->value, "22", 2) == 0);
    assert(d->count == 1);

    assert(dict_get(d, S("missing")) == NULL);
    dict_free(d);
}

static void test_binary_key(void)
{
    Dictionary *d = dict_create(4);
    assert(dict_set(d, "a\0b", 3, S("x"), -1));
    assert(dict_get(d, "a\0b", 3) != NULL);
    assert(dict_get(d, "a", 1) == NULL); /* must not match */
    dict_free(d);
}

static void test_empty_value(void)
{
    Dictionary *d = dict_create(4);
    assert(dict_set(d, S("k"), "", 0, -1));
    Entry *e = dict_get(d, S("k"));
    assert(e && e->value_len == 0);
    dict_free(d);
}

static void test_growth_and_shrink(void)
{
    Dictionary *d = dict_create(4);
    char key[32], val[32];

    for (int i = 0; i < 5000; i++)
    {
        snprintf(key, sizeof key, "key%d", i);
        snprintf(val, sizeof val, "val%d", i);
        assert(dict_set(d, S(key), S(val), -1));
    }
    assert(d->count == 5000);
    assert(d->capacity >= 6667);                    /* 5000 / 0.75 */
    assert((d->capacity & (d->capacity - 1)) == 0); /* power of two */

    for (int i = 0; i < 5000; i++)
    {
        snprintf(key, sizeof key, "key%d", i);
        snprintf(val, sizeof val, "val%d", i);
        Entry *e = dict_get(d, S(key));
        assert(e && e->value_len == strlen(val) &&
               memcmp(e->value, val, e->value_len) == 0);
    }

    size_t big = d->capacity;
    for (int i = 0; i < 4990; i++)
    {
        snprintf(key, sizeof key, "key%d", i);
        assert(dict_delete(d, S(key)));
    }
    assert(d->count == 10);
    assert(d->capacity < big); /* shrank */

    for (int i = 4990; i < 5000; i++)
    {
        snprintf(key, sizeof key, "key%d", i);
        assert(dict_get(d, S(key)) != NULL);
    }
    assert(!dict_delete(d, S("key0"))); /* already gone */
    dict_free(d);
}

static void test_chain_deletes(void)
{
    /* capacity 4 holds 3 keys at most before growing, so force
       collisions by inserting many keys and deleting in mixed order. */
    Dictionary *d = dict_create(4);
    char key[16];
    for (int i = 0; i < 5000; i++)
    {
        snprintf(key, sizeof key, "c%d", i);
        assert(dict_set(d, S(key), S("v"), -1));
    }
    for (int i = 0; i < 5000; i += 2)
    { /* delete every other */
        snprintf(key, sizeof key, "c%d", i);
        assert(dict_delete(d, S(key)));
    }
    for (int i = 0; i < 5000; i++)
    {
        snprintf(key, sizeof key, "c%d", i);
        assert((dict_get(d, S(key)) != NULL) == (i % 2 == 1));
    }
    assert(d->count == 2500);
    dict_free(d);
}

int main(void)
{
    test_set_get_overwrite();
    test_binary_key();
    test_empty_value();
    test_growth_and_shrink();
    test_chain_deletes();
    printf("all dict tests passed\n");
    return 0;
}