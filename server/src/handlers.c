#include <stdio.h>
#include <string.h>

#include "../include/handlers.h"

static Dictionary *g_dict = NULL;

bool handlers_init(Dictionary *dict)
{
    if (dict == NULL)
        return false;

    g_dict = dict;

    return cmd_handler_register("PING", handle_ping) &&
           cmd_handler_register("GET", handle_get) &&
           cmd_handler_register("SET", handle_set) &&
           cmd_handler_register("DEL", handle_del);
}

bool handle_ping(const ParsedLine *line, char *out, size_t size)
{
    (void)line;
    snprintf(out, size, "+PONG\r\n");
    return true;
}

bool handle_get(const ParsedLine *line, char *out, size_t size)
{
    if (line->arg_count != 1)
    {
        snprintf(out, size, "-ERR wrong number of arguments\r\n");
        return true;
    }

    const char *key = line->args[0];

    Entry *entry = dict_get(g_dict, key, strlen(key));

    if (entry == NULL)
    {
        snprintf(out, size, "$-1\r\n");
        return true;
    }

    snprintf(out, size, "$%zu\r\n", entry->value_len);

    size_t used = strlen(out);

    if (used + entry->value_len + 2 > size)
    {
        snprintf(out, size, "-ERR response too large\r\n");
        return true;
    }

    memcpy(out + used, entry->value, entry->value_len);
    used += entry->value_len;

    memcpy(out + used, "\r\n", 2);
    out[used + 2] = '\0';

    return true;
}

bool handle_set(const ParsedLine *line, char *out, size_t size)
{
    if (line->arg_count != 2)
    {
        snprintf(out, size, "-ERR wrong number of arguments\r\n");
        return true;
    }

    const char *key = line->args[0];
    const char *value = line->args[1];

    bool ok = dict_set(
        g_dict,
        key, strlen(key),
        value, strlen(value),
        -1
    );

    snprintf(out, size, "%s",
             ok ? "+OK\r\n" : "-ERR SET failed\r\n");

    return true;
}

bool handle_del(const ParsedLine *line, char *out, size_t size)
{
    if (line->arg_count < 1)
    {
        snprintf(out, size, "-ERR wrong number of arguments\r\n");
        return true;
    }

    int deleted = 0;

    for (int i = 0; i < line->arg_count; i++)
    {
        const char *key = line->args[i];

        if (dict_delete(g_dict, key, strlen(key)))
            deleted++;
    }

    snprintf(out, size, ":%d\r\n", deleted);
    return true;
}