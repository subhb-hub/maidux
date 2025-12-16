#include <stdio.h>
#include <unistd.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "prompt.h"

void prompt_print(void) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("maidux");
        fputs("maidux$ ", stdout);
        fflush(stdout);
        return;
    }
    printf("%s$ ", cwd);
    fflush(stdout);
}
