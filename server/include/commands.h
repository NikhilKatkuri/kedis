
#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_CMDS 64
#define MAX_OPTS 16
#define MAX_TOKENS 16
#define MAX_NAME 64

typedef struct
{
    const char *name;
    const char *value;
} KvOption;

typedef struct
{
    char *tokens[MAX_TOKENS];
    int argc;

    const char *args[MAX_TOKENS];
    int arg_count;

    KvOption opts[MAX_OPTS];
    int opt_count;
} ParsedLine;

typedef struct
{
    char name[MAX_NAME];
    int min_args;
    int max_args;

    char allowed_opts[MAX_OPTS][MAX_NAME];
    int allowed_opt_count;
} CommandSpec;

typedef struct
{
    CommandSpec specs[MAX_CMDS];
    int count;
} CommandRegistry;

typedef bool (*CommandHandler)(
    const ParsedLine *line,
    char *out_resp,
    size_t resp_sz
);

typedef struct
{
    char name[MAX_NAME];
    CommandHandler handler;
} CommandHandlerEntry;

bool parse_line_universal(char *scratch, ParsedLine *out);

bool cmd_registry_load(
    CommandRegistry *reg,
    const char *path
);

bool cmd_handler_register(
    const char *name,
    CommandHandler handler
);

void cmd_execute(
    const CommandRegistry *reg,
    const ParsedLine *line,
    char *out_resp,
    size_t resp_sz
);

const char *find_opt_value(
    const ParsedLine *line,
    const char *name
);

bool parse_ttl_expires_at(
    const char *s,
    int64_t *out_expires_at
);

#endif
