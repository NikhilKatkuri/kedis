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

bool handlers_init(Dictionary *dict);

#endif