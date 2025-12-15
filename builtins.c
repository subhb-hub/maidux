#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "builtins.h"
#include "history.h"

static int do_pwd(void) {
    char buf[1024];
    if (getcwd(buf, sizeof(buf)) == NULL) return -1;
    printf("%s\n", buf);
    return 0;
}

static int do_cd(Stage* st) {
    const char* target = st->argc > 1 ? st->argv[1] : getenv("HOME");
    if (!target) target = "/";
    return chdir(target);
}

static int do_echo(Stage* st) {
    for (int i = 1; i < st->argc; ++i) {
        fputs(st->argv[i], stdout);
        if (i + 1 < st->argc) fputc(' ', stdout);
    }
    fputc('\n', stdout);
    return 0;
}

int builtin_dispatch(Stage* st) {
    if (!st || st->argc == 0) return 0;
    const char* cmd = st->argv[0];
    if (strcmp(cmd, "xpwd") == 0) {
        return do_pwd() == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xcd") == 0) {
        return do_cd(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xecho") == 0) {
        return do_echo(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xhistory") == 0) {
        hist_print();
        return 1;
    }
    return 0;
}
