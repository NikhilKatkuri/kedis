#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../include/handlers.h"

/* Builds a ParsedLine from a list of positional arguments. */
static ParsedLine make_line(int argc, const char *const *argv)
{
    ParsedLine line = {0};

    line.arg_count = argc;

    for (int i = 0; i < argc; i++)
        line.args[i] = argv[i];

    return line;
}

/* Runs a handler and compares its output to the expected reply. */
static void expect(
    CommandHandler handler,
    int argc,
    const char *const *argv,
    const char *expected)
{
    ParsedLine line = make_line(argc, argv);
    char response[256] = {0};

    assert(handler(&line, response, sizeof(response)));

    if (strcmp(response, expected) != 0)
    {
        fprintf(stderr, "expected: \"%s\"\n   found: \"%s\"\n",
                expected, response);
        assert(0 && "handler response mismatch");
    }
}

static void test_ping(void)
{
    expect(handle_ping, 0, NULL, "PONG\r\n");
    printf("PASS: PING\n");
}

static void test_set_get(void)
{
    const char *set_args[] = {"name", "Nikhil"};
    const char *get_args[] = {"name"};
    const char *missing[] = {"nope"};

    expect(handle_set, 2, set_args, "OK\r\n");
    expect(handle_get, 1, get_args, "Nikhil\r\n");
    expect(handle_get, 1, missing, "(nil)\r\n");

    /* Overwriting a key replaces its value. */
    const char *overwrite[] = {"name", "Kedis"};
    expect(handle_set, 2, overwrite, "OK\r\n");
    expect(handle_get, 1, get_args, "Kedis\r\n");

    expect(handle_set, 1, set_args, "-ERR wrong number of arguments\r\n");

    printf("PASS: SET and GET\n");
}

static void test_set_ttl(void)
{
    const char *args[] = {"temp", "value"};
    const char *get_args[] = {"temp"};

    ParsedLine line = make_line(2, args);
    char response[256] = {0};

    /* Invalid TTLs are rejected. */
    line.opts[0].name = "ttl";
    line.opts[0].value = "abc";
    line.opt_count = 1;
    assert(handle_set(&line, response, sizeof(response)));
    assert(strcmp(response, "-ERR invalid TTL\r\n") == 0);

    line.opts[0].value = "0";
    memset(response, 0, sizeof(response));
    assert(handle_set(&line, response, sizeof(response)));
    assert(strcmp(response, "-ERR invalid TTL\r\n") == 0);

    /* A valid TTL is accepted and the key is readable until it expires. */
    line.opts[0].value = "60000";
    memset(response, 0, sizeof(response));
    assert(handle_set(&line, response, sizeof(response)));
    assert(strcmp(response, "OK\r\n") == 0);

    expect(handle_get, 1, get_args, "value\r\n");

    /* PERSIST removes the TTL; a second call has nothing to remove. */
    expect(handle_persist, 1, get_args, "persisted\r\n");
    expect(handle_persist, 1, get_args, "no ttl or key missing\r\n");

    const char *del_args[] = {"temp"};
    expect(handle_del, 1, del_args, "dropped 1 key\r\n");

    printf("PASS: SET -ttl and PERSIST\n");
}

static void test_exists(void)
{
    const char *present[] = {"name"};
    const char *absent[] = {"nope"};

    expect(handle_exists, 1, present, "1\r\n");
    expect(handle_exists, 1, absent, "0\r\n");

    printf("PASS: EXISTS\n");
}

static void test_setnx(void)
{
    const char *existing[] = {"name", "Other"};
    const char *get_name[] = {"name"};
    const char *fresh[] = {"fresh", "1"};

    expect(handle_setnx, 2, existing, "already exists\r\n");
    expect(handle_get, 1, get_name, "Kedis\r\n"); /* value unchanged */
    expect(handle_setnx, 2, fresh, "ok\r\n");

    printf("PASS: SETNX\n");
}

static void test_mset_mget(void)
{
    const char *mset_args[] = {"k1", "v1", "k2", "v2"};
    const char *odd_args[] = {"k1", "v1", "k2"};
    const char *mget_args[] = {"k1", "missing", "k2"};

    expect(handle_mset, 4, mset_args, "OK\r\n");
    expect(handle_mset, 3, odd_args,
           "-ERR wrong number of arguments for 'mset' command\r\n");
    expect(handle_mget, 3, mget_args, "1) v1\r\n2) (nil)\r\n3) v2\r\n");

    printf("PASS: MSET and MGET\n");
}

static void test_count_and_del(void)
{
    /* name, fresh, k1, k2 */
    expect(handle_count, 0, NULL, "4\r\n");

    const char *one[] = {"name"};
    expect(handle_del, 1, one, "dropped 1 key\r\n");
    expect(handle_get, 1, one, "(nil)\r\n");
    expect(handle_del, 1, one, "no keys deleted\r\n");

    const char *many[] = {"k1", "k2", "ghost"};
    expect(handle_del, 3, many, "dropped 2 keys\r\n");

    /* only "fresh" is left */
    expect(handle_count, 0, NULL, "1\r\n");

    printf("PASS: COUNT and DEL\n");
}

int main(void)
{
    Dictionary *dict = dict_create(INITIAL_CAPCITY);
    assert(dict != NULL);

    assert(handlers_init(dict));

    /* Tests share one dictionary and run in this order.
     * FLUSHDB is not tested here: it also deletes the real snapshot file. */
    test_ping();
    test_set_get();
    test_set_ttl();
    test_exists();
    test_setnx();
    test_mset_mget();
    test_count_and_del();

    dict_free(dict);

    printf("\nAll handler tests passed.\n");
    return 0;
}