#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include <stdbool.h>
#include "dictionary.h"
#define default_file_path "./db/dictionary.db"

bool snapshot_save(const Dictionary *d, const char *path);

bool snapshot_load(Dictionary *d, const char *path);

#endif /* SNAPSHOT_H */