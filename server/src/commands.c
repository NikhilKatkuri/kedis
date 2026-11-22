#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <ctype.h>
#include <limits.h>
#include "commands.h"
#define MAX_CMDS 64
#define MAX_OPTS 16
#define MAX_TOKENS 16
#define MAX_NAME 64
#define MAX_FILE_SIZE (1024 * 1024)

static void normalize_command(char *name)
{
    for (size_t i = 0; name[i] != '\0'; i++)
    {
        name[i] = (char)toupper((unsigned char)name[i]);
    }
}

bool parse_line_universal(char *scratch, ParsedLine *out)
{
    if (!scratch || !out)
        return false;

    memset(out, 0, sizeof(*out));

    char *tok = strtok(scratch, " \t\r\n");

    while (tok != NULL)
    {
        if (out->argc >= MAX_TOKENS)
            return false;

        out->tokens[out->argc++] = tok;

        if (out->argc == 1)
        {
            // Normalize only the command name to uppercase
            normalize_command(tok);

            tok = strtok(NULL, " \t\r\n");
            continue;
        }

        if (tok[0] == '-')
        {
            char *eq = strchr(tok + 1, '=');

            if (eq != NULL)
            {
                if (eq == tok + 1 || *(eq + 1) == '\0')
                    return false;

                if (out->opt_count >= MAX_OPTS)
                    return false;

                *eq = '\0';

                /* Reject duplicate options. */
                for (int i = 0; i < out->opt_count; i++)
                {
                    if (strcmp(out->opts[i].name, tok + 1) == 0)
                        return false;
                }

                out->opts[out->opt_count].name = tok + 1;
                out->opts[out->opt_count].value = eq + 1;
                out->opt_count++;
            }
            else
            {
                if (out->arg_count >= MAX_TOKENS)
                    return false;

                out->args[out->arg_count++] = tok;
            }
        }
        else
        {
            if (out->arg_count >= MAX_TOKENS)
                return false;

            out->args[out->arg_count++] = tok;
        }

        tok = strtok(NULL, " \t\r\n");
    }

    return out->argc > 0;
}

static bool opt_is_allowed(
    const CommandSpec *spec,
    const char *opt_name)
{
    for (int i = 0; i < spec->allowed_opt_count; i++)
    {
        if (strcmp(spec->allowed_opts[i], opt_name) == 0)
            return true;
    }

    return false;
}

const char *find_opt_value(
    const ParsedLine *line,
    const char *name)
{
    for (int i = 0; i < line->opt_count; i++)
    {
        if (strcmp(line->opts[i].name, name) == 0)
            return line->opts[i].value;
    }

    return NULL;
}

bool parse_ttl_expires_at(
    const char *s,
    int64_t *out_expires_at)
{
    if (!s || !*s || !out_expires_at)
        return false;

    errno = 0;

    char *end = NULL;
    int64_t ttl_ms = strtoll(s, &end, 10);

    if (errno == ERANGE ||
        end == s ||
        *end != '\0' ||
        ttl_ms <= 0)
        return false;

    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return false;

    int64_t now_ms =
        (int64_t)ts.tv_sec * 1000 +
        ts.tv_nsec / 1000000;

    if (ttl_ms > INT64_MAX - now_ms)
        return false;

    *out_expires_at = now_ms + ttl_ms;

    return true;
}

static CommandHandlerEntry handlers[MAX_CMDS];
static int handler_count = 0;

bool cmd_handler_register(
    const char *name,
    CommandHandler handler)
{
    if (!name || !handler ||
        name[0] == '\0' ||
        handler_count >= MAX_CMDS ||
        strlen(name) >= MAX_NAME)
        return false;

    for (int i = 0; i < handler_count; i++)
    {
        if (strcmp(handlers[i].name, name) == 0)
            return false;
    }

    CommandHandlerEntry *entry = &handlers[handler_count++];

    snprintf(entry->name, sizeof(entry->name), "%s", name);
    entry->handler = handler;

    return true;
}

static CommandHandler cmd_handler_find(const char *name)
{
    for (int i = 0; i < handler_count; i++)
    {
        if (strcmp(handlers[i].name, name) == 0)
            return handlers[i].handler;
    }

    return NULL;
}

bool cmd_registry_load(
    CommandRegistry *reg,
    const char *path)
{
    if (!reg || !path)
        return false;

    memset(reg, 0, sizeof(*reg));

    FILE *fp = fopen(path, "rb");

    if (!fp)
    {
        perror("fopen commands.json");
        return false;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return false;
    }

    long sz = ftell(fp);

    if (sz <= 0 || sz > MAX_FILE_SIZE)
    {
        fclose(fp);
        return false;
    }

    rewind(fp);

    char *buf = malloc((size_t)sz + 1);

    if (!buf)
    {
        fclose(fp);
        return false;
    }

    size_t read_n = fread(buf, 1, (size_t)sz, fp);

    bool read_ok = !ferror(fp);

    fclose(fp);

    if (!read_ok || read_n != (size_t)sz)
    {
        free(buf);
        return false;
    }

    buf[read_n] = '\0';

    char *cursor = strstr(buf, "\"commands\"");

    if (!cursor)
    {
        free(buf);
        return false;
    }

    char *arr_start = strchr(cursor, '[');

    if (!arr_start)
    {
        free(buf);
        return false;
    }

    cursor = arr_start + 1;

    while (*cursor)
    {
        while (*cursor == ' ' || *cursor == '\n' ||
               *cursor == '\t' || *cursor == '\r' ||
               *cursor == ',')
            cursor++;

        if (*cursor == ']')
            break;

        if (*cursor != '{' || reg->count >= MAX_CMDS)
        {
            free(buf);
            return false;
        }

        char *obj_start = cursor;
        char *obj_end = strchr(obj_start, '}');

        if (!obj_end)
        {
            free(buf);
            return false;
        }

        size_t obj_len = (size_t)(obj_end - obj_start + 1);

        char *obj = malloc(obj_len + 1);

        if (!obj)
        {
            free(buf);
            return false;
        }

        memcpy(obj, obj_start, obj_len);
        obj[obj_len] = '\0';

        CommandSpec spec = {0};

        char *name_key = strstr(obj, "\"name\"");
        char *min_key = strstr(obj, "\"min_args\"");
        char *max_key = strstr(obj, "\"max_args\"");

        if (!name_key || !min_key || !max_key)
        {
            free(obj);
            free(buf);
            return false;
        }

        char *q1 = strchr(name_key + 6, '"');
        char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;

        if (!q1 || !q2 ||
            (size_t)(q2 - q1 - 1) >= sizeof(spec.name))
        {
            free(obj);
            free(buf);
            return false;
        }

        memcpy(spec.name, q1 + 1, (size_t)(q2 - q1 - 1));
        spec.name[q2 - q1 - 1] = '\0';

        char *c_min = strchr(min_key, ':');
        char *c_max = strchr(max_key, ':');

        if (!c_min || !c_max)
        {
            free(obj);
            free(buf);
            return false;
        }

        char *end_min;
        char *end_max;

        errno = 0;
        long min_value = strtol(c_min + 1, &end_min, 10);

        if (errno || end_min == c_min + 1 ||
            min_value < 0 || min_value > MAX_TOKENS)
        {
            free(obj);
            free(buf);
            return false;
        }

        errno = 0;
        long max_value = strtol(c_max + 1, &end_max, 10);

        if (errno || end_max == c_max + 1 ||
            (max_value != -1 && (max_value < min_value || max_value > MAX_TOKENS)))
        {
            free(obj);
            free(buf);
            return false;
        }

        spec.min_args = (int)min_value;
        spec.max_args = (int)max_value;

        char *opts_key = strstr(obj, "\"allowed_opts\"");

        if (opts_key)
        {
            char *a = strchr(opts_key, '[');
            char *b = a ? strchr(a, ']') : NULL;

            if (!a || !b)
            {
                free(obj);
                free(buf);
                return false;
            }

            char *p = a + 1;

            while (p < b)
            {
                while (p < b && (*p == ' ' || *p == ',' ||
                                 *p == '\n' || *p == '\t'))
                    p++;

                if (p >= b)
                    break;

                if (*p != '"')
                {
                    free(obj);
                    free(buf);
                    return false;
                }

                char *oq1 = p + 1;
                char *oq2 = strchr(oq1, '"');

                if (!oq2 || oq2 > b)
                {
                    free(obj);
                    free(buf);
                    return false;
                }

                size_t olen = (size_t)(oq2 - oq1);

                if (olen == 0 || olen >= MAX_NAME ||
                    spec.allowed_opt_count >= MAX_OPTS)
                {
                    free(obj);
                    free(buf);
                    return false;
                }

                memcpy(
                    spec.allowed_opts[spec.allowed_opt_count],
                    oq1,
                    olen);

                spec.allowed_opts[spec.allowed_opt_count][olen] = '\0';

                for (int i = 0; i < spec.allowed_opt_count; i++)
                {
                    if (strcmp(
                            spec.allowed_opts[i],
                            spec.allowed_opts[spec.allowed_opt_count]) == 0)
                    {
                        free(obj);
                        free(buf);
                        return false;
                    }
                }

                spec.allowed_opt_count++;

                p = oq2 + 1;
            }
        }

        for (int i = 0; i < reg->count; i++)
        {
            if (strcmp(reg->specs[i].name, spec.name) == 0)
            {
                free(obj);
                free(buf);
                return false;
            }
        }

        reg->specs[reg->count++] = spec;

        free(obj);

        cursor = obj_end + 1;
    }

    free(buf);

    return reg->count > 0;
}

static const CommandSpec *cmd_registry_find(
    const CommandRegistry *reg,
    const char *name)
{
    if (!reg || !name)
        return NULL;

    for (int i = 0; i < reg->count; i++)
    {
        if (strcasecmp(reg->specs[i].name, name) == 0)
            return &reg->specs[i];
    }

    return NULL;
}

void cmd_execute(
    const CommandRegistry *reg,
    const ParsedLine *line,
    char *out_resp,
    size_t resp_sz)
{
    if (!reg || !line || !out_resp || resp_sz == 0 ||
        line->argc == 0)
        return;

    out_resp[0] = '\0';

    const char *cmd = line->tokens[0];

    const CommandSpec *spec = cmd_registry_find(reg, cmd);

    if (!spec)
    {
        snprintf(out_resp, resp_sz, "-ERR unknown command\r\n");
        return;
    }

    if (line->arg_count < spec->min_args ||
        (spec->max_args >= 0 && line->arg_count > spec->max_args))
    {
        snprintf(
            out_resp,
            resp_sz,
            "-ERR wrong number of arguments for '%s'\r\n",
            cmd);

        return;
    }

    for (int i = 0; i < line->opt_count; i++)
    {
        if (!opt_is_allowed(spec, line->opts[i].name))
        {
            snprintf(
                out_resp,
                resp_sz,
                "-ERR unknown option '-%s' for '%s'\r\n",
                line->opts[i].name,
                cmd);

            return;
        }
    }

    CommandHandler handler = cmd_handler_find(cmd);

    if (!handler)
    {
        snprintf(
            out_resp,
            resp_sz,
            "-ERR command '%s' has no handler\r\n",
            cmd);

        return;
    }

    handler(line, out_resp, resp_sz);
}