#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "../include/handlers.h"
#include "../include/snapshot.h"

static Dictionary *g_dict = NULL;

bool handlers_init(Dictionary *dict)
{
    if (dict == NULL)
        return false;

    g_dict = dict;

    return cmd_handler_register("PING", handle_ping) &&
           cmd_handler_register("GET", handle_get) &&
           cmd_handler_register("SET", handle_set) &&
           cmd_handler_register("DEL", handle_del) && cmd_handler_register("COUNT", handle_count) &&
           cmd_handler_register("STATS", handle_stats) && cmd_handler_register("EXISTS", handle_exists) && cmd_handler_register("PERSIST", handle_persist) && cmd_handler_register("FLUSHDB", handle_flush) && cmd_handler_register("MGET", handle_mget) && cmd_handler_register("MSET", handle_mset);
    ;
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

    int header_len = snprintf(out, size, "$%zu\r\n", entry->value_len);

    if (header_len < 0 || (size_t)header_len + entry->value_len + 3 > size)
    {
        snprintf(out, size, "-ERR response too large\r\n");
        return true;
    }

    size_t used = (size_t)header_len;

    memcpy(out + used, entry->value, entry->value_len);
    used += entry->value_len;

    memcpy(out + used, "\r\n", 2);
    out[used + 2] = '\0';

    return true;
}

bool handle_exists(const ParsedLine *line, char *out, size_t size)
{
    if (line->arg_count != 1)
    {
        snprintf(out, size, "-ERR wrong number of arguments\r\n");
        return true;
    }

    const char *key = line->args[0];

    bool exists = dict_key_exists(g_dict, key, strlen(key));

    snprintf(out, size, ":%d\r\n", exists ? 1 : 0);

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

    int64_t out_ttl = -1;

    const char *ttl_option = find_opt_value(line, "ttl");

    if (ttl_option != NULL)
    {
        int64_t expiry;

        if (!parse_ttl_expires_at(ttl_option, &expiry))
        {
            snprintf(out, size, "-ERR invalid TTL\r\n");
            return true;
        }
        out_ttl = expiry;
    }

    bool ok = dict_set(
        g_dict,
        key, strlen(key),
        value, strlen(value),
        out_ttl);

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

bool handle_count(const ParsedLine *line, char *out, size_t size)
{
    (void)line;

    size_t count = dict_count(g_dict);

    snprintf(out, size, ":%zu\r\n", count);
    return true;
}

bool handle_stats(const ParsedLine *line, char *out, size_t size)
{
    (void)line;

    const char *stats = dict_stats(g_dict);

    snprintf(out, size, "+%s\r\n", stats);
    return true;
}

bool handle_persist(const ParsedLine *line, char *out, size_t size)
{
    if (line->arg_count != 1)
    {
        snprintf(out, size, "-ERR wrong number of arguments\r\n");
        return true;
    }

    const char *key = line->args[0];

    bool success = dict_remove_key_expiration(g_dict, key, strlen(key));

    snprintf(out, size, ":%d\r\n", success ? 1 : 0);

    return true;
}

bool handle_flush(const ParsedLine *line, char *out, size_t size)
{
    if (line->arg_count != 0)
    {
        snprintf(out, size, "-ERR wrong number of arguments\r\n");
        return true;
    }

    if (!dict_flush(g_dict))
    {
        snprintf(out, size, "-ERR failed to flush dictionary\r\n");
        return true;
    }

    if (!snapshot_delete(default_snapshot_file()))
    {
        perror("snapshot_delete");
    }

    snprintf(out, size, "+OK\r\n");
    return true;
}

bool handle_mset(const ParsedLine *line, char *out, size_t size)
{
    if (line->arg_count < 2 || (line->arg_count % 2) != 0)
    {
        snprintf(out, size, "-ERR wrong number of arguments for 'mset' command\r\n");
        return true;
    }

    for (int i = 0; i < line->arg_count; i += 2)
    {
        const char *key = line->args[i];
        const char *val = line->args[i + 1];

        if (!dict_set(g_dict, key, strlen(key), val, strlen(val), -1))
        {
            snprintf(out, size, "-ERR out of memory or failed to set key\r\n");
            return true;
        }
    }

    snprintf(out, size, "+OK\r\n");
    return true;
}

bool handle_mget(const ParsedLine *line, char *out, size_t size)
{
    if (line->arg_count < 1 || size == 0)
    {
        snprintf(out, size, "-ERR wrong number of arguments for 'mget' command\r\n");
        return true;
    }

    size_t pos = 0;
    int written = snprintf(out + pos, size - pos, "*%d\r\n", line->arg_count);
    if (written < 0 || (size_t)written >= size - pos)
    {
        snprintf(out, size, "-ERR response too large\r\n");
        return true;
    }
    pos += (size_t)written;

    for (int i = 0; i < line->arg_count; i++)
    {
        const char *key = line->args[i];
        size_t key_len = strlen(key);

        bool found = dict_get(g_dict, key, key_len);
        Entry **slot = found ? dict_find_slot(g_dict, key, key_len) : NULL;

        if (!found || slot == NULL || *slot == NULL || (*slot)->value == NULL)
        {
            written = snprintf(out + pos, size - pos, "$-1\r\n");
        }
        else
        {
            Entry *entry = *slot;
            written = snprintf(out + pos, size - pos, "$%zu\r\n%.*s\r\n",
                               entry->value_len, (int)entry->value_len, entry->value);
        }

        if (written < 0 || (size_t)written >= size - pos)
        {
            snprintf(out, size, "-ERR response buffer overflow\r\n");
            return true;
        }
        pos += (size_t)written;
    }

    return true;
}