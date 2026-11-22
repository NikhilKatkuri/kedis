#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "../include/handlers.h"
#include "../include/snapshot.h"

static Dictionary *g_dict = NULL;

bool handlers_init(Dictionary *dict)
{
    if (!dict)
        return false;

    g_dict = dict;

    static const struct
    {
        const char *name;
        CommandHandler fn;
    } cmds[] = {
        {"PING", handle_ping},
        {"GET", handle_get},
        {"SET", handle_set},
        {"DEL", handle_del},
        {"COUNT", handle_count},
        {"STATS", handle_stats},
        {"EXISTS", handle_exists},
        {"PERSIST", handle_persist},
        {"FLUSHDB", handle_flush},
        {"MGET", handle_mget},
        {"MSET", handle_mset},
        {"SETNX", handle_setnx},
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
    {
        if (!cmd_handler_register(cmds[i].name, cmds[i].fn))
            return false;
    }
    return true;
}

bool handle_ping(const ParsedLine *line, char *out, size_t size)
{
    (void)line;
    snprintf(out, size, "PONG\r\n");
    return true;
}

bool handle_get(const ParsedLine *line, char *out, size_t size)
{
    if (line->arg_count != 1)
    {
        snprintf(out, size, "-ERR wrong number of arguments\r\n");
        return true;
    }

    if (size == 0)
    {
        return false;
    }

    const char *key = line->args[0];
    Entry *entry = dict_get(g_dict, key, strlen(key));

    if (entry == NULL || entry->value == NULL)
    {
        snprintf(out, size, "(nil)\r\n");
        return true;
    }

    if (entry->value_len + 3 > size) // value + "\r\n" + '\0'
    {
        snprintf(out, size, "-ERR response too large\r\n");
        return true;
    }

    memcpy(out, entry->value, entry->value_len);
    out[entry->value_len] = '\r';
    out[entry->value_len + 1] = '\n';
    out[entry->value_len + 2] = '\0';

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

    snprintf(out, size, "%d\r\n", exists ? 1 : 0);

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
             ok ? "OK\r\n" : "-ERR SET failed\r\n");

    return true;
}

bool handle_del(const ParsedLine *line, char *out, size_t size)
{
    if (out == NULL || size == 0)
    {
        return false;
    }

    if (line == NULL || line->arg_count < 1)
    {
        snprintf(out, size, "ERR wrong number of arguments\r\n");
        return true;
    }

    int deleted = 0;

    for (int i = 0; i < line->arg_count; i++)
    {
        const char *key = line->args[i];

        if (dict_delete(g_dict, key, strlen(key)))
        {
            deleted++;
        }
    }

    if (deleted == 0)
    {
        snprintf(out, size, "no keys deleted\r\n");
    }
    else if (deleted == 1)
    {
        snprintf(out, size, "dropped 1 key\r\n");
    }
    else
    {
        snprintf(out, size, "dropped %d keys\r\n", deleted);
    }

    return true;
}

bool handle_count(const ParsedLine *line, char *out, size_t size)
{
    (void)line;

    size_t count = dict_count(g_dict);

    snprintf(out, size, "%zu\r\n", count);
    return true;
}

bool handle_stats(const ParsedLine *line, char *out, size_t size)
{
    (void)line;

    const char *stats = dict_stats(g_dict);

    snprintf(out, size, "%s\r\n", stats);
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

    snprintf(out, size, "%s",
             success ? "persisted\r\n" : "no ttl or key missing\r\n");
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

    snprintf(out, size, "OK\r\n");
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

    snprintf(out, size, "OK\r\n");
    return true;
}

bool handle_mget(const ParsedLine *line, char *out, size_t size)
{
    if (out == NULL || size == 0)
    {
        return false;
    }

    if (line == NULL || line->arg_count < 1)
    {
        snprintf(out, size, "ERR wrong number of arguments for 'mget' command\r\n");
        return true;
    }

    size_t pos = 0;

    for (int i = 0; i < line->arg_count; i++)
    {
        const char *key = line->args[i];
        size_t key_len = strlen(key);

        Entry *entry = dict_get(g_dict, key, key_len);
        const char *val_str = (entry == NULL || entry->value == NULL) ? "(nil)" : entry->value;
        size_t val_len = (entry == NULL || entry->value == NULL) ? 5 : entry->value_len;

        char prefix[32];
        int prefix_len = snprintf(prefix, sizeof(prefix), "%d) ", i + 1);
        if (prefix_len < 0)
        {
            return false;
        }

        size_t needed = (size_t)prefix_len + val_len + 2;
        if (needed >= size - pos)
        {
            if (pos == 0)
            {
                snprintf(out, size, "ERR response too large\r\n");
            }
            else
            {
                out[pos] = '\0';
            }
            return true;
        }

        memcpy(out + pos, prefix, (size_t)prefix_len);
        pos += (size_t)prefix_len;

        memcpy(out + pos, val_str, val_len);
        pos += val_len;

        memcpy(out + pos, "\r\n", 2);
        pos += 2;
    }

    out[pos] = '\0';
    return true;
}

bool handle_setnx(const ParsedLine *line, char *out, size_t size)
{
    if (size == 0 || out == NULL)
    {
        return false;
    }

    if (line == NULL || line->arg_count != 2)
    {
        snprintf(out, size, "-ERR wrong number of arguments for 'setnx' command\r\n");
        return true;
    }

    const char *key = line->args[0];
    const char *val = line->args[1];
    size_t key_len = strlen(key);
    size_t val_len = strlen(val);

    if (dict_get(g_dict, key, key_len) != NULL)
    {
        snprintf(out, size, "already exists\r\n");
        return true;
    }

    bool created = dict_set(g_dict, key, key_len, val, val_len, -1);
    if (!created)
    {
        snprintf(out, size, "ERR out of memory or failed to set key\r\n");
        return true;
    }

    snprintf(out, size, "ok\r\n");
    return true;
}