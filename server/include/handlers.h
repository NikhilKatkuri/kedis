#ifndef HANDLERS_H
#define HANDLERS_H

#include "commands.h"
#include "dictionary.h"

bool handle_ping(const ParsedLine *, char *, size_t);
bool handle_get(const ParsedLine *, char *, size_t);
bool handle_set(const ParsedLine *, char *, size_t);
bool handle_del(const ParsedLine *, char *, size_t);
bool handle_count(const ParsedLine *, char *, size_t);
bool handle_stats(const ParsedLine *, char *, size_t);
bool handle_exists(const ParsedLine *line, char *out, size_t size);
bool handle_persist(const ParsedLine *line, char *out, size_t size);
bool handle_flush(const ParsedLine *line, char *out, size_t size);
bool handle_mset(const ParsedLine *line, char *out, size_t size);
bool handle_mget(const ParsedLine *line, char *out, size_t size);
bool handlers_init(Dictionary *dict);

#endif