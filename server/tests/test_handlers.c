
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../include/handlers.h"

static void test_ping(void)
{
    ParsedLine line = {0};
    char response[256] = {0};

    assert(handle_ping(&line, response, sizeof(response)));
    assert(strcmp(response, "+PONG\r\n") == 0);

    printf("PASS: PING\n");
}

static void test_set_get(void)
{
    char response[256] = {0};

    ParsedLine set_line = {0};
    set_line.arg_count = 2;
    set_line.args[0] = "name";
    set_line.args[1] = "Nikhil";

    assert(handle_set(&set_line, response, sizeof(response)));
    assert(strcmp(response, "+OK\r\n") == 0);

    ParsedLine get_line = {0};
    get_line.arg_count = 1;
    get_line.args[0] = "name";

    memset(response, 0, sizeof(response));

    assert(handle_get(&get_line, response, sizeof(response)));
    assert(strcmp(response, "$6\r\nNikhil\r\n") == 0);

    printf("PASS: SET and GET\n");
}

static void test_del(void)
{
    char response[256] = {0};

    ParsedLine line = {0};
    line.arg_count = 1;
    line.args[0] = "name";

    assert(handle_del(&line, response, sizeof(response)));
    assert(strcmp(response, ":1\r\n") == 0);

    memset(response, 0, sizeof(response));

    ParsedLine get_line = {0};
    get_line.arg_count = 1;
    get_line.args[0] = "name";

    assert(handle_get(&get_line, response, sizeof(response)));
    assert(strcmp(response, "$-1\r\n") == 0);

    printf("PASS: DEL\n");
}

int main(void)
{
    Dictionary *dict = dict_create(INITIAL_CAPCITY);
    assert(dict != NULL);

    assert(handlers_init(dict));

    test_ping();
    test_set_get();
    test_del();

    dict_free(dict);

    printf("\nAll handler tests passed.\n");
    return 0;
}