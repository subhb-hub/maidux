#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "builtins.h"
#include "executor.h"
#include "logger.h"
#include "path.h"

static int apply_redirs(Stage* st) {
    if (st->out_file) {
        int flags = O_CREAT | O_WRONLY | (st->out_append ? O_APPEND : O_TRUNC);
        int fd = open(st->out_file, flags, 0644);
        if (fd < 0) return -1;
        if (dup2(fd, STDOUT_FILENO) < 0) {
            close(fd);
            return -1;
        }
        close(fd);
    }
    if (st->err_file) {
        int fd = open(st->err_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0) return -1;
        if (dup2(fd, STDERR_FILENO) < 0) {
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

int exec_stage(Stage* st) {
    int bi = builtin_dispatch(st);
    if (bi != 0) {
        return bi > 0 ? 0 : -1;
    }

    char pathbuf[1024];
    if (resolve_in_path(st->argv[0], pathbuf, sizeof(pathbuf)) != 0) {
        errno = ENOENT;
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (apply_redirs(st) != 0) {
            log_error_errno("redirection");
            _exit(1);
        }
        execv(pathbuf, st->argv);
        log_error_errno("execv");
        _exit(1);
    }
    int status;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

int exec_pipeline(Pipeline* pl) {
    if (!pl || pl->n == 0) return -1;
    if (pl->n == 1) {
        return exec_stage(&pl->stages[0]);
    }

    int pipes[MAX_STAGE - 1][2];
    for (int i = 0; i < pl->n - 1; ++i) {
        if (pipe(pipes[i]) < 0) return -1;
    }

    pid_t pids[MAX_STAGE];
    for (int i = 0; i < pl->n; ++i) {
        if (builtin_dispatch(&pl->stages[i]) != 0) {
            errno = ENOTSUP;
            return -1;
        }
        pids[i] = fork();
        if (pids[i] < 0) return -1;
        if (pids[i] == 0) {
            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            if (i < pl->n - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }
            for (int j = 0; j < pl->n - 1; ++j) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            if (apply_redirs(&pl->stages[i]) != 0) {
                log_error_errno("redirection");
                _exit(1);
            }
            char pathbuf[1024];
            if (resolve_in_path(pl->stages[i].argv[0], pathbuf, sizeof(pathbuf)) != 0) {
                log_error_errno("resolve");
                _exit(1);
            }
            execv(pathbuf, pl->stages[i].argv);
            log_error_errno("execv");
            _exit(1);
        }
    }

    for (int i = 0; i < pl->n - 1; ++i) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    int status = 0;
    for (int i = 0; i < pl->n; ++i) {
        int st;
        if (waitpid(pids[i], &st, 0) < 0) status = -1;
    }
    return status;
}
