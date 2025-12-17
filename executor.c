#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "builtins.h"
#include "executor.h"
#include "logger.h"
#include "transcript.h"
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

static void append_limited(char** buf, size_t* len, size_t* cap, const char* data, size_t n, size_t max_keep) {
    if (!buf || !len || !cap || !data || n == 0 || max_keep == 0) return;
    size_t keep = n;
    if (*len + keep > max_keep) {
        size_t overflow = *len + keep - max_keep;
        if (overflow >= *len) {
            *len = 0;
        } else {
            memmove(*buf, *buf + overflow, *len - overflow);
            *len -= overflow;
        }
    }
    if (*len + keep + 1 > *cap) {
        size_t new_cap = (*cap ? *cap : 256);
        while (new_cap < *len + keep + 1) new_cap *= 2;
        char* nb = realloc(*buf, new_cap);
        if (!nb) return;
        *buf = nb;
        *cap = new_cap;
    }
    memcpy(*buf + *len, data, keep);
    *len += keep;
    (*buf)[*len] = '\0';
}

static void forward_and_collect(int fd, int out_fd, char** buf, size_t* len, size_t* cap, size_t max_keep, int* open) {
    char tmp[512];
    ssize_t r = read(fd, tmp, sizeof(tmp));
    if (r > 0) {
        if (out_fd >= 0) {
            ssize_t off = 0;
            while (off < r) {
                ssize_t w = write(out_fd, tmp + off, (size_t)(r - off));
                if (w < 0) {
                    if (errno == EINTR) continue;
                    break;
                }
                off += w;
            }
        }
        transcript_append(tmp, (size_t)r);
        append_limited(buf, len, cap, tmp, (size_t)r, max_keep);
    } else if (r == 0) {
        *open = 0;
    } else if (r < 0 && errno != EINTR) {
        *open = 0;
    }
}

int exec_builtin_parent_with_capture(Stage* st, char** out_buf, char** err_buf) {
    if (out_buf) *out_buf = NULL;
    if (err_buf) *err_buf = NULL;
    if (!st || !builtin_requires_parent(st)) return -1;

    const size_t MAX_KEEP = 8192;

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (pipe(out_pipe) < 0) return -1;
    if (pipe(err_pipe) < 0) {
        close(out_pipe[0]); close(out_pipe[1]);
        return -1;
    }

    int saved_out = dup(STDOUT_FILENO);
    int saved_err = dup(STDERR_FILENO);
    if (saved_out < 0 || saved_err < 0) {
        if (saved_out >= 0) close(saved_out);
        if (saved_err >= 0) close(saved_err);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        return -1;
    }

    if (dup2(out_pipe[1], STDOUT_FILENO) < 0 || dup2(err_pipe[1], STDERR_FILENO) < 0) {
        close(saved_out); close(saved_err);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        return -1;
    }

    close(out_pipe[1]);
    close(err_pipe[1]);

    int ret = exec_stage(st);
    fflush(NULL);

    int restore_fail = 0;
    if (dup2(saved_out, STDOUT_FILENO) < 0) restore_fail = 1;
    if (dup2(saved_err, STDERR_FILENO) < 0) restore_fail = 1;
    close(saved_out);
    close(saved_err);

    int out_open = 1, err_open = 1;
    char* out_data = NULL;
    char* err_data = NULL;
    size_t out_len = 0, out_cap = 0;
    size_t err_len = 0, err_cap = 0;

    while (out_open || err_open) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = -1;
        if (out_open) {
            FD_SET(out_pipe[0], &rfds);
            if (out_pipe[0] > maxfd) maxfd = out_pipe[0];
        }
        if (err_open) {
            FD_SET(err_pipe[0], &rfds);
            if (err_pipe[0] > maxfd) maxfd = err_pipe[0];
        }

        struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
        int r = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
        } else if (r > 0) {
            if (out_open && FD_ISSET(out_pipe[0], &rfds)) {
                forward_and_collect(out_pipe[0], STDOUT_FILENO,
                                    out_buf ? &out_data : NULL, &out_len, &out_cap,
                                    MAX_KEEP, &out_open);
            }
            if (err_open && FD_ISSET(err_pipe[0], &rfds)) {
                forward_and_collect(err_pipe[0], STDERR_FILENO,
                                    err_buf ? &err_data : NULL, &err_len, &err_cap,
                                    MAX_KEEP, &err_open);
            }
        }

        if (!out_open && !err_open) break;
    }

    close(out_pipe[0]);
    close(err_pipe[0]);

    if (out_buf) *out_buf = out_data; else free(out_data);
    if (err_buf) *err_buf = err_data; else free(err_data);

    if (restore_fail) return -1;
    return ret == 0 ? 0 : -1;
}

int exec_stage(Stage* st) {
    if (builtin_is(st)) {
        int saved_stdout = -1, saved_stderr = -1;
        int redirected = 0;
        int restore_failed = 0;
        if (st->out_file || st->err_file) {
            saved_stdout = dup(STDOUT_FILENO);
            saved_stderr = dup(STDERR_FILENO);
            if (saved_stdout < 0 || saved_stderr < 0) {
                if (saved_stdout >= 0) close(saved_stdout);
                if (saved_stderr >= 0) close(saved_stderr);
                return -1;
            }
            if (apply_redirs(st) != 0) {
                dup2(saved_stdout, STDOUT_FILENO);
                dup2(saved_stderr, STDERR_FILENO);
                close(saved_stdout);
                close(saved_stderr);
                return -1;
            }
            redirected = 1;
        }

        int bi = builtin_dispatch(st);
        int ret = bi != 0 ? (bi > 0 ? 0 : -1) : 0;

        if (redirected) {
            if (dup2(saved_stdout, STDOUT_FILENO) < 0) restore_failed = 1;
            if (dup2(saved_stderr, STDERR_FILENO) < 0) restore_failed = 1;
            close(saved_stdout);
            close(saved_stderr);
        }

        return restore_failed ? -1 : ret;
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
            perror("maidux");
            log_error_errno("redirection");
            _exit(1);
        }
        execv(pathbuf, st->argv);
        perror("maidux");
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
                perror("maidux");
                log_error_errno("redirection");
                _exit(1);
            }
            int bi = builtin_dispatch_child(&pl->stages[i], 0);
            if (bi != 0) {
                _exit(bi > 0 ? 0 : 1);
            }
            char pathbuf[1024];
            if (resolve_in_path(pl->stages[i].argv[0], pathbuf, sizeof(pathbuf)) != 0) {
                perror("maidux");
                log_error_errno("resolve");
                _exit(1);
            }
            execv(pathbuf, pl->stages[i].argv);
            perror("maidux");
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
        if (waitpid(pids[i], &st, 0) < 0) {
            status = -1;
            continue;
        }
        if ((WIFEXITED(st) && WEXITSTATUS(st) != 0) || WIFSIGNALED(st)) {
            status = -1;
        }
    }
    return status;
}

int exec_pipeline_with_capture(Pipeline* pl, char** out_buf, char** err_buf) {
    if (out_buf) *out_buf = NULL;
    if (err_buf) *err_buf = NULL;
    if (!pl || pl->n == 0) return -1;

    const size_t MAX_KEEP = 8192;

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (pipe(out_pipe) < 0) return -1;
    if (pipe(err_pipe) < 0) {
        close(out_pipe[0]); close(out_pipe[1]);
        return -1;
    }

    pid_t child_pid = fork();
    if (child_pid < 0) {
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        return -1;
    }

    if (child_pid == 0) {
        // 子进程：把 stdout/stderr 指向 pipe（父进程 stdout/stderr 完全不动）
        if (dup2(out_pipe[1], STDOUT_FILENO) < 0) _exit(1);
        if (dup2(err_pipe[1], STDERR_FILENO) < 0) _exit(1);

        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);

        // 注意：这里执行会继承上面设置好的 stdout/stderr
        if (pl->n == 1 && builtin_is(&pl->stages[0])) {
            int st = exec_stage(&pl->stages[0]);
            fflush(NULL);
            _exit(st == 0 ? 0 : 1);
        } else if (pl->n == 1) {
            char pathbuf[1024];
            if (resolve_in_path(pl->stages[0].argv[0], pathbuf, sizeof(pathbuf)) != 0) {
                errno = ENOENT;
                _exit(1);
            }
            if (apply_redirs(&pl->stages[0]) != 0) {
                perror("maidux");
                log_error_errno("redirection");
                _exit(1);
            }
            execv(pathbuf, pl->stages[0].argv);
            perror("maidux");
            log_error_errno("execv");
            _exit(1);
        } else {
            int st = exec_pipeline(pl);
            fflush(NULL);
            _exit(st == 0 ? 0 : 1);
        }
    }

    // 父进程：只读 pipe，把内容转发到终端，同时收集到 out_data/err_data
    close(out_pipe[1]);
    close(err_pipe[1]);

    int status = 0;
    int child_done = 0;
    int out_open = 1, err_open = 1;

    char* out_data = NULL;
    char* err_data = NULL;
    size_t out_len = 0, out_cap = 0;
    size_t err_len = 0, err_cap = 0;

    while (out_open || err_open || !child_done) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = -1;

        if (out_open) {
            FD_SET(out_pipe[0], &rfds);
            if (out_pipe[0] > maxfd) maxfd = out_pipe[0];
        }
        if (err_open) {
            FD_SET(err_pipe[0], &rfds);
            if (err_pipe[0] > maxfd) maxfd = err_pipe[0];
        }

        struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 }; // 100ms
        int r = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            // select 出错也别死循环，尝试退出
        } else if (r > 0) {
            if (out_open && FD_ISSET(out_pipe[0], &rfds)) {
                forward_and_collect(out_pipe[0], STDOUT_FILENO,
                                    out_buf ? &out_data : NULL, &out_len, &out_cap,
                                    MAX_KEEP, &out_open);
            }
            if (err_open && FD_ISSET(err_pipe[0], &rfds)) {
                forward_and_collect(err_pipe[0], STDERR_FILENO,
                                    err_buf ? &err_data : NULL, &err_len, &err_cap,
                                    MAX_KEEP, &err_open);
            }
        }

        if (!child_done) {
            int st;
            pid_t w = waitpid(child_pid, &st, WNOHANG);
            if (w == child_pid) {
                child_done = 1;
                if ((WIFEXITED(st) && WEXITSTATUS(st) != 0) || WIFSIGNALED(st)) {
                    status = -1;
                }
            }
        }

        if (child_done && !out_open && !err_open) break;
    }

    if (!child_done) {
        int st;
        if (waitpid(child_pid, &st, 0) == child_pid) {
            if ((WIFEXITED(st) && WEXITSTATUS(st) != 0) || WIFSIGNALED(st)) {
                status = -1;
            }
        } else {
            status = -1;
        }
    }

    close(out_pipe[0]);
    close(err_pipe[0]);

    if (out_buf) *out_buf = out_data;
    else free(out_data);

    if (err_buf) *err_buf = err_data;
    else free(err_data);

    return status;
}
