#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include <stdbool.h>
#include "dictionary.h"

const char *default_snapshot_file(void);

bool snapshot_save(Dictionary *d, const char *path);

bool snapshot_load(Dictionary *d, const char *path);

#endif /* SNAPSHOT_H */