#ifndef MAIDUX_PATH_H
#define MAIDUX_PATH_H

#include <stddef.h>

int resolve_in_path(const char* cmd, char* out, size_t outsz);

#endif
