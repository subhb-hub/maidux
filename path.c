#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "path.h"

int resolve_in_path(const char* cmd, char* out, size_t outsz) {
    if (!cmd || !out || outsz == 0) return -1;
    if (strchr(cmd, '/')) {
        if (access(cmd, X_OK) == 0) {
            strncpy(out, cmd, outsz - 1);
            out[outsz - 1] = '\0';
            return 0;
        }
        return -1;
    }
    const char* path = getenv("PATH");
    if (!path) return -1;
    char* copy = strdup(path);
    char* saveptr = NULL;
    char* dir = strtok_r(copy, ":", &saveptr);
    while (dir) {
        snprintf(out, outsz, "%s/%s", dir, cmd);
        if (access(out, X_OK) == 0) {
            free(copy);
            return 0;
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }
    free(copy);
    return -1;
}
