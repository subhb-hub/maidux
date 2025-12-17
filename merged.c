/*
 * Auto-merged source file
 * Generated at: 2025-12-17 17:29:09
 * Working dir:  /Users/border/Desktop/ks/maidux
 */


/* ============================ FILE: builtins.h ============================ */

#ifndef MAIDUX_BUILTINS_H
#define MAIDUX_BUILTINS_H

#include "parser.h"

int builtin_dispatch(Stage* st);
int builtin_dispatch_child(Stage* st, int allow_cd);
int builtin_is(const Stage* st);

#endif

/* ========================= END FILE: builtins.h ============================ */

/* ============================ FILE: executor.h ============================ */

#ifndef MAIDUX_EXECUTOR_H
#define MAIDUX_EXECUTOR_H

#include "parser.h"

int exec_stage(Stage* st);
int exec_pipeline(Pipeline* pl);
int exec_pipeline_with_capture(Pipeline* pl, char** out_buf, char** err_buf);

#endif

/* ========================= END FILE: executor.h ============================ */

/* ============================ FILE: history.h ============================ */

#ifndef MAIDUX_HISTORY_H
#define MAIDUX_HISTORY_H

#include <stddef.h>

void hist_init(size_t cap);
void hist_push(const char* line);
void hist_print(void);

#endif

/* ========================= END FILE: history.h ============================ */

/* ============================ FILE: logger.h ============================ */

#ifndef MAIDUX_LOGGER_H
#define MAIDUX_LOGGER_H

void log_init(const char* log_path);
void log_info(const char* fmt, ...);
void log_error(const char* fmt, ...);
void log_error_errno(const char* where);
const char* log_path(void);

#endif

/* ========================= END FILE: logger.h ============================ */

/* ============================ FILE: maid_client.h ============================ */

#ifndef MAIDUX_MAID_CLIENT_H
#define MAIDUX_MAID_CLIENT_H

#include <stddef.h>

typedef struct {
    int in_fd;   // parent -> python stdin
    int out_fd;  // python stdout -> parent
    int running;
    int pid;
} MaidClient;

extern MaidClient g_maid;

int maid_client_start(MaidClient* mc, const char* py_path, const char* script_path);
int maid_client_push_turn(MaidClient* mc, const char* cmd, const char* out, const char* err, int code);
int maid_client_request_suggest(MaidClient* mc, char* buf, size_t bufsz);
int maid_client_stop(MaidClient* mc);
int maid_client_is_running(const MaidClient* mc);

#endif

/* ========================= END FILE: maid_client.h ============================ */

/* ============================ FILE: parser.h ============================ */

#ifndef MAIDUX_PARSER_H
#define MAIDUX_PARSER_H

#define MAX_ARGV 64
#define MAX_STAGE 8

typedef struct {
    char* argv[MAX_ARGV];
    int argc;
    char* out_file;
    int out_append;
    char* err_file;
} Stage;

typedef struct {
    Stage stages[MAX_STAGE];
    int n;
} Pipeline;

int parse_single(const char* line, Stage* out);
int parse_pipeline(const char* line, Pipeline* pl);
void free_stage(Stage* st);
void free_pipeline(Pipeline* pl);

#endif

/* ========================= END FILE: parser.h ============================ */

/* ============================ FILE: path.h ============================ */

#ifndef MAIDUX_PATH_H
#define MAIDUX_PATH_H

#include <stddef.h>

int resolve_in_path(const char* cmd, char* out, size_t outsz);

#endif

/* ========================= END FILE: path.h ============================ */

/* ============================ FILE: prompt.h ============================ */

#ifndef MAIDUX_PROMPT_H
#define MAIDUX_PROMPT_H

void prompt_print(void);

#endif

/* ========================= END FILE: prompt.h ============================ */

/* ============================ FILE: utils.h ============================ */

#ifndef MAIDUX_UTILS_H
#define MAIDUX_UTILS_H

char* str_trim(char* s);

#endif

/* ========================= END FILE: utils.h ============================ */

/* ============================ FILE: builtins.c ============================ */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "builtins.h"
#include "history.h"
#include "logger.h"
#include "maid_client.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char prev_cwd[PATH_MAX];

static int copy_file(const char* src, const char* dst) {
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open(dst, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (out < 0) {
        close(in);
        return -1;
    }
    char buf[4096];
    ssize_t n;
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(out, buf + off, n - off);
            if (w < 0) {
                close(in);
                close(out);
                return -1;
            }
            off += w;
        }
    }
    if (n < 0) {
        close(in);
        close(out);
        return -1;
    }
    close(in);
    close(out);
    return 0;
}

static int copy_dir_recursive(const char* src, const char* dst) {
    DIR* dir = opendir(src);
    if (!dir) return -1;
    if (mkdir(dst, 0755) < 0) {
        if (errno != EEXIST) {
            closedir(dir);
            return -1;
        }
    }
    struct dirent* ent;
    int ret = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char src_path[PATH_MAX];
        char dst_path[PATH_MAX];
        snprintf(src_path, sizeof(src_path), "%s/%s", src, ent->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, ent->d_name);
        struct stat st;
        if (stat(src_path, &st) != 0) {
            ret = -1;
            break;
        }
        if (S_ISDIR(st.st_mode)) {
            if (copy_dir_recursive(src_path, dst_path) != 0) { ret = -1; break; }
        } else {
            if (copy_file(src_path, dst_path) != 0) { ret = -1; break; }
        }
    }
    closedir(dir);
    return ret;
}

static int remove_recursive(const char* path) {
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    if (S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(path);
        if (!dir) return -1;
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char child[PATH_MAX];
            snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
            if (remove_recursive(child) != 0) { closedir(dir); return -1; }
        }
        closedir(dir);
        return rmdir(path);
    }
    return unlink(path);
}

static void extract_json_field(const char* json, const char* key, char* out, size_t outsz) {
    if (!json || !key || !out || outsz == 0) return;
    const char* pos = strstr(json, key);
    if (!pos) return;
    pos = strchr(pos, ':');
    if (!pos) return;
    pos++;
    while (*pos == ' ' || *pos == '\t') pos++;
    if (*pos != '"') return;
    pos++;
    size_t j = 0;
    while (*pos && *pos != '"' && j + 1 < outsz) {
        if (*pos == '\\' && pos[1]) {
            pos++;
            char c = *pos;
            if (c == 'n') {
                out[j++] = '\n';
            } else if (c == 't') {
                out[j++] = '\t';
            } else {
                out[j++] = c;
            }
            pos++;
            continue;
        }
        out[j++] = *pos++;
    }
    out[j] = '\0';
}

static int do_maid(void) {
    if (!maid_client_is_running(&g_maid)) {
        fprintf(stderr, "maid helper is not running\n");
        return -1;
    }
    char line[2048];
    if (maid_client_request_suggest(&g_maid, line, sizeof(line)) != 0) {
        return -1;
    }
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

    if (strstr(line, "\"type\":\"error\"")) {
        char msg[512] = {0};
        extract_json_field(line, "\"message\"", msg, sizeof(msg));
        fprintf(stderr, "maid error: %s\n", msg[0] ? msg : "unknown");
        return -1;
    }

    char cmd[512] = {0};
    char reason[1024] = {0};
    extract_json_field(line, "\"command\"", cmd, sizeof(cmd));
    extract_json_field(line, "\"reason\"", reason, sizeof(reason));

    if (cmd[0] && reason[0]) {
        printf("maid suggests: %s    # %s\n", cmd, reason);
    } else if (cmd[0]) {
        printf("maid suggests: %s\n", cmd);
    } else {
        printf("%s\n", line);
    }
    return 1;
}

static int move_path(const char* src, const char* dst) {
    if (rename(src, dst) == 0) return 0;
    if (errno != EXDEV) return -1;
    // cross-device: copy then remove
    struct stat st;
    if (stat(src, &st) != 0) return -1;
    int ret;
    if (S_ISDIR(st.st_mode)) {
        ret = copy_dir_recursive(src, dst);
    } else {
        ret = copy_file(src, dst);
    }
    if (ret == 0) ret = remove_recursive(src);
    return ret;
}

static int do_pwd(void) {
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf)) == NULL) return -1;
    printf("%s\n", buf);
    return 0;
}

static int do_cd(Stage* st, int in_child) {
    (void)in_child;
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
    const char* target;
    if (st->argc < 2) {
        target = getenv("HOME");
        if (!target) target = "/";
    } else if (strcmp(st->argv[1], "-") == 0) {
        target = prev_cwd[0] ? prev_cwd : cwd;
    } else {
        target = st->argv[1];
    }
    if (chdir(target) == 0) {
        if (cwd[0]) {
            strncpy(prev_cwd, cwd, sizeof(prev_cwd) - 1);
            prev_cwd[sizeof(prev_cwd) - 1] = '\0';
        }
        return 0;
    }
    return -1;
}

static int do_echo(Stage* st) {
    for (int i = 1; i < st->argc; ++i) {
        fputs(st->argv[i], stdout);
        if (i + 1 < st->argc) fputc(' ', stdout);
    }
    fputc('\n', stdout);
    return 0;
}

static int do_ls(Stage* st) {
    const char* target = st->argc > 1 ? st->argv[1] : ".";
    DIR* dir = opendir(target);
    if (!dir) return -1;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", target, ent->d_name);
        struct stat stbuf;
        if (stat(path, &stbuf) == 0 && S_ISDIR(stbuf.st_mode)) {
            printf("Dir: %s\n", ent->d_name);
        } else {
            printf("File: %s\n", ent->d_name);
        }
    }
    closedir(dir);
    return 0;
}

static int do_touch(Stage* st) {
    if (st->argc < 2) { errno = EINVAL; return -1; }
    int fd = open(st->argv[1], O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) {
        if (errno == EEXIST) return 0;
        return -1;
    }
    close(fd);
    return 0;
}

static int do_cat(Stage* st) {
    if (st->argc < 2) { errno = EINVAL; return -1; }
    int fd = open(st->argv[1], O_RDONLY);
    if (fd < 0) return -1;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, n) < 0) { close(fd); return -1; }
    }
    close(fd);
    return n < 0 ? -1 : 0;
}

static int do_cp(Stage* st) {
    if (st->argc < 3) { errno = EINVAL; return -1; }
    int recursive = 0;
    const char* src = NULL;
    const char* dst = NULL;
    if (strcmp(st->argv[1], "-r") == 0) {
        recursive = 1;
        if (st->argc < 4) { errno = EINVAL; return -1; }
        src = st->argv[2];
        dst = st->argv[3];
    } else {
        src = st->argv[1];
        dst = st->argv[2];
    }
    struct stat stbuf;
    if (stat(src, &stbuf) != 0) return -1;
    if (S_ISDIR(stbuf.st_mode)) {
        if (!recursive) { errno = EISDIR; return -1; }
        return copy_dir_recursive(src, dst);
    }
    return copy_file(src, dst);
}

static int do_rm(Stage* st) {
    if (st->argc < 2) { errno = EINVAL; return -1; }
    int recursive = 0;
    const char* target = NULL;
    if (strcmp(st->argv[1], "-r") == 0) {
        recursive = 1;
        if (st->argc < 3) { errno = EINVAL; return -1; }
        target = st->argv[2];
    } else {
        target = st->argv[1];
    }
    struct stat stbuf;
    if (lstat(target, &stbuf) != 0) return -1;
    if (S_ISDIR(stbuf.st_mode) && !recursive) { errno = EISDIR; return -1; }
    return remove_recursive(target);
}

static int do_mv(Stage* st) {
    if (st->argc < 3) { errno = EINVAL; return -1; }
    int recursive = 0;
    const char* src = NULL;
    const char* dst = NULL;
    if (strcmp(st->argv[1], "-r") == 0) {
        recursive = 1;
        if (st->argc < 4) { errno = EINVAL; return -1; }
        src = st->argv[2];
        dst = st->argv[3];
    } else {
        src = st->argv[1];
        dst = st->argv[2];
    }
    struct stat stbuf;
    if (lstat(src, &stbuf) != 0) return -1;
    if (S_ISDIR(stbuf.st_mode) && !recursive) { errno = EISDIR; return -1; }
    return move_path(src, dst);
}

static int do_tee(Stage* st) {
    if (st->argc < 2) { errno = EINVAL; return -1; }
    int fd = open(st->argv[1], O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return -1;
    char buf[4096];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, n) < 0) { close(fd); return -1; }
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(fd, buf + off, n - off);
            if (w < 0) { close(fd); return -1; }
            off += w;
        }
    }
    close(fd);
    return n < 0 ? -1 : 0;
}

static int do_journalctl(void) {
    const char* path = log_path();
    if (!path) { errno = ENOENT; return -1; }
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, n) < 0) { close(fd); return -1; }
    }
    close(fd);
    return n < 0 ? -1 : 0;
}

static int handle_builtin(Stage* st, int allow_cd) {
    if (!st || st->argc == 0) return 0;
    const char* cmd = st->argv[0];
    if (strcmp(cmd, "xpwd") == 0) {
        return do_pwd() == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xcd") == 0) {
        if (!allow_cd) { errno = ENOTSUP; return -1; }
        return do_cd(st, 0) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xecho") == 0) {
        return do_echo(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xhistory") == 0) {
        hist_print();
        return 1;
    } else if (strcmp(cmd, "xls") == 0) {
        return do_ls(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xtouch") == 0) {
        return do_touch(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xcat") == 0) {
        return do_cat(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xcp") == 0) {
        return do_cp(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xrm") == 0) {
        return do_rm(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xmv") == 0) {
        return do_mv(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xtee") == 0) {
        return do_tee(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xjournalctl") == 0) {
        return do_journalctl() == 0 ? 1 : -1;
    } else if (strcmp(cmd, "maid") == 0) {
        if (st->argc != 1) { errno = EINVAL; return -1; }
        return do_maid();
    }
    return 0;
}

int builtin_dispatch(Stage* st) {
    return handle_builtin(st, 1);
}

int builtin_dispatch_child(Stage* st, int allow_cd) {
    return handle_builtin(st, allow_cd);
}

int builtin_is(const Stage* st) {
    if (!st || st->argc == 0) return 0;
    const char* cmd = st->argv[0];
    return strcmp(cmd, "xpwd") == 0 || strcmp(cmd, "xcd") == 0 || strcmp(cmd, "xecho") == 0 ||
           strcmp(cmd, "xhistory") == 0 || strcmp(cmd, "xls") == 0 || strcmp(cmd, "xtouch") == 0 ||
           strcmp(cmd, "xcat") == 0 || strcmp(cmd, "xcp") == 0 || strcmp(cmd, "xrm") == 0 ||
           strcmp(cmd, "xmv") == 0 || strcmp(cmd, "xtee") == 0 || strcmp(cmd, "xjournalctl") == 0 ||
           strcmp(cmd, "maid") == 0;
}

/* ========================= END FILE: builtins.c ============================ */

/* ============================ FILE: executor.c ============================ */

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
        append_limited(buf, len, cap, tmp, (size_t)r, max_keep);
    } else if (r == 0) {
        *open = 0;
    } else if (r < 0 && errno != EINTR) {
        *open = 0;
    }
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
    int saved_stdout = dup(STDOUT_FILENO);
    int saved_stderr = dup(STDERR_FILENO);
    if (saved_stdout < 0 || saved_stderr < 0) {
        if (saved_stdout >= 0) close(saved_stdout);
        if (saved_stderr >= 0) close(saved_stderr);
        return -1;
    }

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (pipe(out_pipe) < 0 || pipe(err_pipe) < 0) {
        close(saved_stdout);
        close(saved_stderr);
        if (out_pipe[0] >= 0) close(out_pipe[0]);
        if (out_pipe[1] >= 0) close(out_pipe[1]);
        if (err_pipe[0] >= 0) close(err_pipe[0]);
        if (err_pipe[1] >= 0) close(err_pipe[1]);
        return -1;
    }

    if (dup2(out_pipe[1], STDOUT_FILENO) < 0 || dup2(err_pipe[1], STDERR_FILENO) < 0) {
        close(saved_stdout);
        close(saved_stderr);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        return -1;
    }

    close(out_pipe[1]);
    close(err_pipe[1]);

    int status = 0;
    pid_t child_pid = -1;
    int child_done = 0;
    int out_open = 1, err_open = 1;
    char* out_data = NULL;
    char* err_data = NULL;
    size_t out_len = 0, out_cap = 0;
    size_t err_len = 0, err_cap = 0;
    if (pl->n == 1 && builtin_is(&pl->stages[0])) {
        child_pid = fork();
        if (child_pid == 0) {
            int st = exec_stage(&pl->stages[0]);
            fflush(NULL);
            _exit(st == 0 ? 0 : 1);
        } else if (child_pid < 0) {
            status = -1;
        }
    } else if (pl->n == 1) {
        char pathbuf[1024];
        if (resolve_in_path(pl->stages[0].argv[0], pathbuf, sizeof(pathbuf)) != 0) {
            errno = ENOENT;
            status = -1;
        } else {
            child_pid = fork();
            if (child_pid == 0) {
                if (apply_redirs(&pl->stages[0]) != 0) {
                    perror("maidux");
                    log_error_errno("redirection");
                    _exit(1);
                }
                execv(pathbuf, pl->stages[0].argv);
                perror("maidux");
                log_error_errno("execv");
                _exit(1);
            } else if (child_pid < 0) {
                status = -1;
            }
        }
        if (child_pid <= 0 && status != 0) {
            child_done = 1;
            out_open = 0;
            err_open = 0;
        }
    } else {
        child_pid = fork();
        if (child_pid == 0) {
            int st = exec_pipeline(pl);
            fflush(NULL);
            _exit(st == 0 ? 0 : 1);
        } else if (child_pid < 0) {
            status = -1;
        }
    }

    while (out_open || err_open || (!child_done && child_pid > 0)) {
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
        int r = select(maxfd + 1, &rfds, NULL, NULL, child_pid > 0 ? &(struct timeval){.tv_sec = 0, .tv_usec = 100000} : NULL);
        if (r < 0) {
            if (errno == EINTR) continue;
        } else if (r > 0) {
            if (out_open && FD_ISSET(out_pipe[0], &rfds)) {
                forward_and_collect(out_pipe[0], saved_stdout, out_buf ? &out_data : NULL, &out_len, &out_cap, MAX_KEEP, &out_open);
            }
            if (err_open && FD_ISSET(err_pipe[0], &rfds)) {
                forward_and_collect(err_pipe[0], saved_stderr, err_buf ? &err_data : NULL, &err_len, &err_cap, MAX_KEEP, &err_open);
            }
        }

        if (child_pid > 0 && !child_done) {
            int st;
            pid_t w = waitpid(child_pid, &st, WNOHANG);
            if (w == child_pid) {
                child_done = 1;
                if ((WIFEXITED(st) && WEXITSTATUS(st) != 0) || WIFSIGNALED(st)) {
                    status = -1;
                }
            }
        } else if (child_pid < 0) {
            child_done = 1;
        } else {
            child_done = 1;
        }

        if (child_pid > 0 && child_done && !out_open && !err_open) break;
    }

    if (child_pid > 0 && !child_done) {
        int st;
        if (waitpid(child_pid, &st, 0) == child_pid) {
            if ((WIFEXITED(st) && WEXITSTATUS(st) != 0) || WIFSIGNALED(st)) {
                status = -1;
            }
        } else {
            status = -1;
        }
    }

    dup2(saved_stdout, STDOUT_FILENO);
    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stdout);
    close(saved_stderr);
    close(out_pipe[0]);
    close(err_pipe[0]);

    if (out_buf) *out_buf = out_data;
    else free(out_data);
    if (err_buf) *err_buf = err_data;
    else free(err_data);

    return status;
}

/* ========================= END FILE: executor.c ============================ */

/* ============================ FILE: history.c ============================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "history.h"

static char** history = NULL;
static size_t hist_cap = 0;
static size_t hist_count = 0;

void hist_init(size_t cap) {
    hist_cap = cap;
    hist_count = 0;
    history = calloc(cap, sizeof(char*));
}

void hist_push(const char* line) {
    if (!history || hist_cap == 0 || !line) return;
    size_t idx = hist_count % hist_cap;
    free(history[idx]);
    history[idx] = strdup(line);
    hist_count++;
}

void hist_print(void) {
    if (!history) return;
    size_t start = hist_count > hist_cap ? hist_count - hist_cap : 0;
    for (size_t i = start; i < hist_count; ++i) {
        size_t idx = i % hist_cap;
        printf("%zu %s\n", i + 1, history[idx] ? history[idx] : "");
    }
}

/* ========================= END FILE: history.c ============================ */

/* ============================ FILE: logger.c ============================ */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "logger.h"

static FILE* log_fp = NULL;
static char log_file_path[4096];

static void log_vwrite(const char* prefix, const char* fmt, va_list ap) {
    if (!log_fp) return;
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    fprintf(log_fp, "%04d-%02d-%02d %02d:%02d:%02d %s",
            tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
            tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec, prefix);
    vfprintf(log_fp, fmt, ap);
    fputc('\n', log_fp);
    fflush(log_fp);
}

void log_init(const char* log_path) {
    if (log_fp) return;
    if (log_path) {
        strncpy(log_file_path, log_path, sizeof(log_file_path) - 1);
        log_file_path[sizeof(log_file_path) - 1] = '\0';
    }
    log_fp = fopen(log_file_path, "a");
}

void log_info(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vwrite("[INFO] ", fmt, ap);
    va_end(ap);
}

void log_error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vwrite("[ERROR] ", fmt, ap);
    va_end(ap);
}

void log_error_errno(const char* where) {
    if (!where) where = "";
    log_error("%s: %s", where, strerror(errno));
}

const char* log_path(void) {
    return log_file_path[0] ? log_file_path : NULL;
}

/* ========================= END FILE: logger.c ============================ */

/* ============================ FILE: maid_client.c ============================ */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "logger.h"
#include "maid_client.h"

MaidClient g_maid = { .in_fd = -1, .out_fd = -1, .running = 0, .pid = -1 };

static ssize_t readline_fd(int fd, char* buf, size_t bufsz) {
    if (!buf || bufsz == 0) return -1;
    size_t idx = 0;
    while (idx + 1 < bufsz) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r == 0) break;
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        buf[idx++] = c;
        if (c == '\n') break;
    }
    buf[idx] = '\0';
    return (ssize_t)idx;
}

static void json_escape(const char* src, char* dst, size_t dsz) {
    if (!dst || dsz == 0) return;
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j + 2 < dsz; ++i) {
        unsigned char c = (unsigned char)src[i];
        switch (c) {
            case '"':
            case '\\':
                dst[j++] = '\\';
                dst[j++] = (char)c;
                break;
            case '\n':
                dst[j++] = '\\';
                dst[j++] = 'n';
                break;
            case '\r':
                dst[j++] = '\\';
                dst[j++] = 'r';
                break;
            case '\t':
                dst[j++] = '\\';
                dst[j++] = 't';
                break;
            default:
                if (c < 0x20) {
                    if (j + 6 >= dsz) continue;
                    j += snprintf(dst + j, dsz - j, "\\u%04x", c);
                } else {
                    dst[j++] = (char)c;
                }
        }
    }
    dst[j] = '\0';
}

int maid_client_is_running(const MaidClient* mc) {
    return mc && mc->running;
}

int maid_client_start(MaidClient* mc, const char* py_path, const char* script_path) {
    if (!mc || !py_path || !script_path) return -1;
    int to_py[2];
    int from_py[2];
    if (pipe(to_py) < 0) return -1;
    if (pipe(from_py) < 0) {
        close(to_py[0]);
        close(to_py[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(to_py[0]);
        close(to_py[1]);
        close(from_py[0]);
        close(from_py[1]);
        return -1;
    }

    if (pid == 0) {
        dup2(to_py[0], STDIN_FILENO);
        dup2(from_py[1], STDOUT_FILENO);
        close(to_py[0]);
        close(to_py[1]);
        close(from_py[0]);
        close(from_py[1]);
        execlp(py_path, py_path, script_path, (char*)NULL);
        perror("maidux");
        _exit(1);
    }

    close(to_py[0]);
    close(from_py[1]);
    mc->in_fd = to_py[1];
    mc->out_fd = from_py[0];
    mc->pid = pid;
    mc->running = 1;

    int flags = fcntl(mc->out_fd, F_GETFD);
    if (flags != -1) {
        fcntl(mc->out_fd, F_SETFD, flags | FD_CLOEXEC);
    }
    flags = fcntl(mc->in_fd, F_GETFD);
    if (flags != -1) {
        fcntl(mc->in_fd, F_SETFD, flags | FD_CLOEXEC);
    }

    return 0;
}

static int write_json_line(MaidClient* mc, const char* json_line) {
    if (!maid_client_is_running(mc)) return -1;
    size_t len = strlen(json_line);
    ssize_t w = write(mc->in_fd, json_line, len);
    if (w < 0 || (size_t)w != len) return -1;
    return 0;
}

int maid_client_push_turn(MaidClient* mc, const char* cmd, const char* out, const char* err, int code) {
    if (!maid_client_is_running(mc) || !cmd) return -1;
    char esc_cmd[1024];
    char esc_out[1024];
    char esc_err[1024];
    json_escape(cmd, esc_cmd, sizeof(esc_cmd));
    json_escape(out ? out : "", esc_out, sizeof(esc_out));
    json_escape(err ? err : "", esc_err, sizeof(esc_err));
    char buf[1400];
    int n = snprintf(buf, sizeof(buf),
                     "{\"type\":\"turn\",\"cmd\":\"%s\",\"out\":\"%s\",\"err\":\"%s\",\"code\":%d}\n",
                     esc_cmd, esc_out, esc_err, code);
    if (n < 0 || (size_t)n >= sizeof(buf)) return -1;
    return write_json_line(mc, buf);
}

int maid_client_request_suggest(MaidClient* mc, char* buf, size_t bufsz) {
    if (!maid_client_is_running(mc) || !buf || bufsz == 0) return -1;
    if (write_json_line(mc, "{\"type\":\"maid\"}\n") != 0) return -1;
    ssize_t r = readline_fd(mc->out_fd, buf, bufsz);
    return r <= 0 ? -1 : 0;
}

int maid_client_stop(MaidClient* mc) {
    if (!maid_client_is_running(mc)) return 0;
    write_json_line(mc, "{\"type\":\"quit\"}\n");
    close(mc->in_fd);
    close(mc->out_fd);
    mc->in_fd = -1;
    mc->out_fd = -1;
    mc->running = 0;
    int status = 0;
    if (waitpid(mc->pid, &status, 0) < 0) {
        log_error_errno("maid waitpid");
        return -1;
    }
    return 0;
}

/* ========================= END FILE: maid_client.c ============================ */

/* ============================ FILE: main.c ============================ */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "executor.h"
#include "history.h"
#include "logger.h"
#include "parser.h"
#include "prompt.h"
#include "utils.h"
#include "maid_client.h"

int main(void) {
    log_init("maidux.log");
    hist_init(100);

    const char* py = getenv("MAID_PYTHON");
    if (!py) py = "python3";
    const char* script = getenv("MAID_BRIDGE");
    if (!script) script = "./maid_bridge.py";
    if (maid_client_start(&g_maid, py, script) != 0) {
        log_error("failed to start maid helper");
    }

    puts("(*•⌄•)===========Welcome to maidux!===========(✿•⌄•)");

    char line[1024];
    while (1) {
        prompt_print();
        if (!fgets(line, sizeof(line), stdin)) {
            putchar('\n');
            break;
        }
        str_trim(line);
        if (line[0] == '\0') continue;
        if (strcmp(line, "quit") == 0) break;

        hist_push(line);
        Pipeline pl;
        if (parse_pipeline(line, &pl) != 0) {
            perror("maidux");
            log_error_errno("parse");
            continue;
        }
        errno = 0;
        char* cap_out = NULL;
        char* cap_err = NULL;
        int exec_status = exec_pipeline_with_capture(&pl, &cap_out, &cap_err);
        if (exec_status != 0) {
            if (errno != 0) {
                perror("maidux");
                log_error_errno("exec");
            } else {
                fprintf(stderr, "maidux: command failed\n");
                log_error("exec failed");
            }
        }
        char errbuf[128] = {0};
        if (exec_status != 0) {
            if (errno != 0) {
                snprintf(errbuf, sizeof(errbuf), "%s", strerror(errno));
            } else {
                snprintf(errbuf, sizeof(errbuf), "command failed");
            }
        }
        if (strcmp(line, "maid") != 0) {
            maid_client_push_turn(&g_maid, line, cap_out ? cap_out : "", cap_err ? cap_err : errbuf, exec_status == 0 ? 0 : 1);
        }
        free_pipeline(&pl);
        free(cap_out);
        free(cap_err);
    }
    puts("(*•⌄•)===========Bye.===========(✿•⌄•)");
    maid_client_stop(&g_maid);
    return 0;
}

/* ========================= END FILE: main.c ============================ */

/* ============================ FILE: merged.c ============================ */

/*
 * Auto-merged source file
 * Generated at: 2025-12-17 17:29:09
 * Working dir:  /Users/border/Desktop/ks/maidux
 */


/* ============================ FILE: builtins.h ============================ */

#ifndef MAIDUX_BUILTINS_H
#define MAIDUX_BUILTINS_H

#include "parser.h"

int builtin_dispatch(Stage* st);
int builtin_dispatch_child(Stage* st, int allow_cd);
int builtin_is(const Stage* st);

#endif

/* ========================= END FILE: builtins.h ============================ */

/* ============================ FILE: executor.h ============================ */

#ifndef MAIDUX_EXECUTOR_H
#define MAIDUX_EXECUTOR_H

#include "parser.h"

int exec_stage(Stage* st);
int exec_pipeline(Pipeline* pl);
int exec_pipeline_with_capture(Pipeline* pl, char** out_buf, char** err_buf);

#endif

/* ========================= END FILE: executor.h ============================ */

/* ============================ FILE: history.h ============================ */

#ifndef MAIDUX_HISTORY_H
#define MAIDUX_HISTORY_H

#include <stddef.h>

void hist_init(size_t cap);
void hist_push(const char* line);
void hist_print(void);

#endif

/* ========================= END FILE: history.h ============================ */

/* ============================ FILE: logger.h ============================ */

#ifndef MAIDUX_LOGGER_H
#define MAIDUX_LOGGER_H

void log_init(const char* log_path);
void log_info(const char* fmt, ...);
void log_error(const char* fmt, ...);
void log_error_errno(const char* where);
const char* log_path(void);

#endif

/* ========================= END FILE: logger.h ============================ */

/* ============================ FILE: maid_client.h ============================ */

#ifndef MAIDUX_MAID_CLIENT_H
#define MAIDUX_MAID_CLIENT_H

#include <stddef.h>

typedef struct {
    int in_fd;   // parent -> python stdin
    int out_fd;  // python stdout -> parent
    int running;
    int pid;
} MaidClient;

extern MaidClient g_maid;

int maid_client_start(MaidClient* mc, const char* py_path, const char* script_path);
int maid_client_push_turn(MaidClient* mc, const char* cmd, const char* out, const char* err, int code);
int maid_client_request_suggest(MaidClient* mc, char* buf, size_t bufsz);
int maid_client_stop(MaidClient* mc);
int maid_client_is_running(const MaidClient* mc);

#endif

/* ========================= END FILE: maid_client.h ============================ */

/* ============================ FILE: parser.h ============================ */

#ifndef MAIDUX_PARSER_H
#define MAIDUX_PARSER_H

#define MAX_ARGV 64
#define MAX_STAGE 8

typedef struct {
    char* argv[MAX_ARGV];
    int argc;
    char* out_file;
    int out_append;
    char* err_file;
} Stage;

typedef struct {
    Stage stages[MAX_STAGE];
    int n;
} Pipeline;

int parse_single(const char* line, Stage* out);
int parse_pipeline(const char* line, Pipeline* pl);
void free_stage(Stage* st);
void free_pipeline(Pipeline* pl);

#endif

/* ========================= END FILE: parser.h ============================ */

/* ============================ FILE: path.h ============================ */

#ifndef MAIDUX_PATH_H
#define MAIDUX_PATH_H

#include <stddef.h>

int resolve_in_path(const char* cmd, char* out, size_t outsz);

#endif

/* ========================= END FILE: path.h ============================ */

/* ============================ FILE: prompt.h ============================ */

#ifndef MAIDUX_PROMPT_H
#define MAIDUX_PROMPT_H

void prompt_print(void);

#endif

/* ========================= END FILE: prompt.h ============================ */

/* ============================ FILE: utils.h ============================ */

#ifndef MAIDUX_UTILS_H
#define MAIDUX_UTILS_H

char* str_trim(char* s);

#endif

/* ========================= END FILE: utils.h ============================ */

/* ============================ FILE: builtins.c ============================ */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "builtins.h"
#include "history.h"
#include "logger.h"
#include "maid_client.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char prev_cwd[PATH_MAX];

static int copy_file(const char* src, const char* dst) {
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open(dst, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (out < 0) {
        close(in);
        return -1;
    }
    char buf[4096];
    ssize_t n;
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(out, buf + off, n - off);
            if (w < 0) {
                close(in);
                close(out);
                return -1;
            }
            off += w;
        }
    }
    if (n < 0) {
        close(in);
        close(out);
        return -1;
    }
    close(in);
    close(out);
    return 0;
}

static int copy_dir_recursive(const char* src, const char* dst) {
    DIR* dir = opendir(src);
    if (!dir) return -1;
    if (mkdir(dst, 0755) < 0) {
        if (errno != EEXIST) {
            closedir(dir);
            return -1;
        }
    }
    struct dirent* ent;
    int ret = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char src_path[PATH_MAX];
        char dst_path[PATH_MAX];
        snprintf(src_path, sizeof(src_path), "%s/%s", src, ent->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, ent->d_name);
        struct stat st;
        if (stat(src_path, &st) != 0) {
            ret = -1;
            break;
        }
        if (S_ISDIR(st.st_mode)) {
            if (copy_dir_recursive(src_path, dst_path) != 0) { ret = -1; break; }
        } else {
            if (copy_file(src_path, dst_path) != 0) { ret = -1; break; }
        }
    }
    closedir(dir);
    return ret;
}

static int remove_recursive(const char* path) {
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    if (S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(path);
        if (!dir) return -1;
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char child[PATH_MAX];
            snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
            if (remove_recursive(child) != 0) { closedir(dir); return -1; }
        }
        closedir(dir);
        return rmdir(path);
    }
    return unlink(path);
}

static void extract_json_field(const char* json, const char* key, char* out, size_t outsz) {
    if (!json || !key || !out || outsz == 0) return;
    const char* pos = strstr(json, key);
    if (!pos) return;
    pos = strchr(pos, ':');
    if (!pos) return;
    pos++;
    while (*pos == ' ' || *pos == '\t') pos++;
    if (*pos != '"') return;
    pos++;
    size_t j = 0;
    while (*pos && *pos != '"' && j + 1 < outsz) {
        if (*pos == '\\' && pos[1]) {
            pos++;
            char c = *pos;
            if (c == 'n') {
                out[j++] = '\n';
            } else if (c == 't') {
                out[j++] = '\t';
            } else {
                out[j++] = c;
            }
            pos++;
            continue;
        }
        out[j++] = *pos++;
    }
    out[j] = '\0';
}

static int do_maid(void) {
    if (!maid_client_is_running(&g_maid)) {
        fprintf(stderr, "maid helper is not running\n");
        return -1;
    }
    char line[2048];
    if (maid_client_request_suggest(&g_maid, line, sizeof(line)) != 0) {
        return -1;
    }
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

    if (strstr(line, "\"type\":\"error\"")) {
        char msg[512] = {0};
        extract_json_field(line, "\"message\"", msg, sizeof(msg));
        fprintf(stderr, "maid error: %s\n", msg[0] ? msg : "unknown");
        return -1;
    }

    char cmd[512] = {0};
    char reason[1024] = {0};
    extract_json_field(line, "\"command\"", cmd, sizeof(cmd));
    extract_json_field(line, "\"reason\"", reason, sizeof(reason));

    if (cmd[0] && reason[0]) {
        printf("maid suggests: %s    # %s\n", cmd, reason);
    } else if (cmd[0]) {
        printf("maid suggests: %s\n", cmd);
    } else {
        printf("%s\n", line);
    }
    return 1;
}

static int move_path(const char* src, const char* dst) {
    if (rename(src, dst) == 0) return 0;
    if (errno != EXDEV) return -1;
    // cross-device: copy then remove
    struct stat st;
    if (stat(src, &st) != 0) return -1;
    int ret;
    if (S_ISDIR(st.st_mode)) {
        ret = copy_dir_recursive(src, dst);
    } else {
        ret = copy_file(src, dst);
    }
    if (ret == 0) ret = remove_recursive(src);
    return ret;
}

static int do_pwd(void) {
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf)) == NULL) return -1;
    printf("%s\n", buf);
    return 0;
}

static int do_cd(Stage* st, int in_child) {
    (void)in_child;
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
    const char* target;
    if (st->argc < 2) {
        target = getenv("HOME");
        if (!target) target = "/";
    } else if (strcmp(st->argv[1], "-") == 0) {
        target = prev_cwd[0] ? prev_cwd : cwd;
    } else {
        target = st->argv[1];
    }
    if (chdir(target) == 0) {
        if (cwd[0]) {
            strncpy(prev_cwd, cwd, sizeof(prev_cwd) - 1);
            prev_cwd[sizeof(prev_cwd) - 1] = '\0';
        }
        return 0;
    }
    return -1;
}

static int do_echo(Stage* st) {
    for (int i = 1; i < st->argc; ++i) {
        fputs(st->argv[i], stdout);
        if (i + 1 < st->argc) fputc(' ', stdout);
    }
    fputc('\n', stdout);
    return 0;
}

static int do_ls(Stage* st) {
    const char* target = st->argc > 1 ? st->argv[1] : ".";
    DIR* dir = opendir(target);
    if (!dir) return -1;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", target, ent->d_name);
        struct stat stbuf;
        if (stat(path, &stbuf) == 0 && S_ISDIR(stbuf.st_mode)) {
            printf("Dir: %s\n", ent->d_name);
        } else {
            printf("File: %s\n", ent->d_name);
        }
    }
    closedir(dir);
    return 0;
}

static int do_touch(Stage* st) {
    if (st->argc < 2) { errno = EINVAL; return -1; }
    int fd = open(st->argv[1], O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) {
        if (errno == EEXIST) return 0;
        return -1;
    }
    close(fd);
    return 0;
}

static int do_cat(Stage* st) {
    if (st->argc < 2) { errno = EINVAL; return -1; }
    int fd = open(st->argv[1], O_RDONLY);
    if (fd < 0) return -1;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, n) < 0) { close(fd); return -1; }
    }
    close(fd);
    return n < 0 ? -1 : 0;
}

static int do_cp(Stage* st) {
    if (st->argc < 3) { errno = EINVAL; return -1; }
    int recursive = 0;
    const char* src = NULL;
    const char* dst = NULL;
    if (strcmp(st->argv[1], "-r") == 0) {
        recursive = 1;
        if (st->argc < 4) { errno = EINVAL; return -1; }
        src = st->argv[2];
        dst = st->argv[3];
    } else {
        src = st->argv[1];
        dst = st->argv[2];
    }
    struct stat stbuf;
    if (stat(src, &stbuf) != 0) return -1;
    if (S_ISDIR(stbuf.st_mode)) {
        if (!recursive) { errno = EISDIR; return -1; }
        return copy_dir_recursive(src, dst);
    }
    return copy_file(src, dst);
}

static int do_rm(Stage* st) {
    if (st->argc < 2) { errno = EINVAL; return -1; }
    int recursive = 0;
    const char* target = NULL;
    if (strcmp(st->argv[1], "-r") == 0) {
        recursive = 1;
        if (st->argc < 3) { errno = EINVAL; return -1; }
        target = st->argv[2];
    } else {
        target = st->argv[1];
    }
    struct stat stbuf;
    if (lstat(target, &stbuf) != 0) return -1;
    if (S_ISDIR(stbuf.st_mode) && !recursive) { errno = EISDIR; return -1; }
    return remove_recursive(target);
}

static int do_mv(Stage* st) {
    if (st->argc < 3) { errno = EINVAL; return -1; }
    int recursive = 0;
    const char* src = NULL;
    const char* dst = NULL;
    if (strcmp(st->argv[1], "-r") == 0) {
        recursive = 1;
        if (st->argc < 4) { errno = EINVAL; return -1; }
        src = st->argv[2];
        dst = st->argv[3];
    } else {
        src = st->argv[1];
        dst = st->argv[2];
    }
    struct stat stbuf;
    if (lstat(src, &stbuf) != 0) return -1;
    if (S_ISDIR(stbuf.st_mode) && !recursive) { errno = EISDIR; return -1; }
    return move_path(src, dst);
}

static int do_tee(Stage* st) {
    if (st->argc < 2) { errno = EINVAL; return -1; }
    int fd = open(st->argv[1], O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return -1;
    char buf[4096];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, n) < 0) { close(fd); return -1; }
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(fd, buf + off, n - off);
            if (w < 0) { close(fd); return -1; }
            off += w;
        }
    }
    close(fd);
    return n < 0 ? -1 : 0;
}

static int do_journalctl(void) {
    const char* path = log_path();
    if (!path) { errno = ENOENT; return -1; }
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, n) < 0) { close(fd); return -1; }
    }
    close(fd);
    return n < 0 ? -1 : 0;
}

static int handle_builtin(Stage* st, int allow_cd) {
    if (!st || st->argc == 0) return 0;
    const char* cmd = st->argv[0];
    if (strcmp(cmd, "xpwd") == 0) {
        return do_pwd() == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xcd") == 0) {
        if (!allow_cd) { errno = ENOTSUP; return -1; }
        return do_cd(st, 0) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xecho") == 0) {
        return do_echo(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xhistory") == 0) {
        hist_print();
        return 1;
    } else if (strcmp(cmd, "xls") == 0) {
        return do_ls(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xtouch") == 0) {
        return do_touch(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xcat") == 0) {
        return do_cat(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xcp") == 0) {
        return do_cp(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xrm") == 0) {
        return do_rm(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xmv") == 0) {
        return do_mv(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xtee") == 0) {
        return do_tee(st) == 0 ? 1 : -1;
    } else if (strcmp(cmd, "xjournalctl") == 0) {
        return do_journalctl() == 0 ? 1 : -1;
    } else if (strcmp(cmd, "maid") == 0) {
        if (st->argc != 1) { errno = EINVAL; return -1; }
        return do_maid();
    }
    return 0;
}

int builtin_dispatch(Stage* st) {
    return handle_builtin(st, 1);
}

int builtin_dispatch_child(Stage* st, int allow_cd) {
    return handle_builtin(st, allow_cd);
}

int builtin_is(const Stage* st) {
    if (!st || st->argc == 0) return 0;
    const char* cmd = st->argv[0];
    return strcmp(cmd, "xpwd") == 0 || strcmp(cmd, "xcd") == 0 || strcmp(cmd, "xecho") == 0 ||
           strcmp(cmd, "xhistory") == 0 || strcmp(cmd, "xls") == 0 || strcmp(cmd, "xtouch") == 0 ||
           strcmp(cmd, "xcat") == 0 || strcmp(cmd, "xcp") == 0 || strcmp(cmd, "xrm") == 0 ||
           strcmp(cmd, "xmv") == 0 || strcmp(cmd, "xtee") == 0 || strcmp(cmd, "xjournalctl") == 0 ||
           strcmp(cmd, "maid") == 0;
}

/* ========================= END FILE: builtins.c ============================ */

/* ============================ FILE: executor.c ============================ */

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
        append_limited(buf, len, cap, tmp, (size_t)r, max_keep);
    } else if (r == 0) {
        *open = 0;
    } else if (r < 0 && errno != EINTR) {
        *open = 0;
    }
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
    int saved_stdout = dup(STDOUT_FILENO);
    int saved_stderr = dup(STDERR_FILENO);
    if (saved_stdout < 0 || saved_stderr < 0) {
        if (saved_stdout >= 0) close(saved_stdout);
        if (saved_stderr >= 0) close(saved_stderr);
        return -1;
    }

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (pipe(out_pipe) < 0 || pipe(err_pipe) < 0) {
        close(saved_stdout);
        close(saved_stderr);
        if (out_pipe[0] >= 0) close(out_pipe[0]);
        if (out_pipe[1] >= 0) close(out_pipe[1]);
        if (err_pipe[0] >= 0) close(err_pipe[0]);
        if (err_pipe[1] >= 0) close(err_pipe[1]);
        return -1;
    }

    if (dup2(out_pipe[1], STDOUT_FILENO) < 0 || dup2(err_pipe[1], STDERR_FILENO) < 0) {
        close(saved_stdout);
        close(saved_stderr);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        return -1;
    }

    close(out_pipe[1]);
    close(err_pipe[1]);

    int status = 0;
    pid_t child_pid = -1;
    int child_done = 0;
    int out_open = 1, err_open = 1;
    char* out_data = NULL;
    char* err_data = NULL;
    size_t out_len = 0, out_cap = 0;
    size_t err_len = 0, err_cap = 0;
    if (pl->n == 1 && builtin_is(&pl->stages[0])) {
        child_pid = fork();
        if (child_pid == 0) {
            int st = exec_stage(&pl->stages[0]);
            fflush(NULL);
            _exit(st == 0 ? 0 : 1);
        } else if (child_pid < 0) {
            status = -1;
        }
    } else if (pl->n == 1) {
        char pathbuf[1024];
        if (resolve_in_path(pl->stages[0].argv[0], pathbuf, sizeof(pathbuf)) != 0) {
            errno = ENOENT;
            status = -1;
        } else {
            child_pid = fork();
            if (child_pid == 0) {
                if (apply_redirs(&pl->stages[0]) != 0) {
                    perror("maidux");
                    log_error_errno("redirection");
                    _exit(1);
                }
                execv(pathbuf, pl->stages[0].argv);
                perror("maidux");
                log_error_errno("execv");
                _exit(1);
            } else if (child_pid < 0) {
                status = -1;
            }
        }
        if (child_pid <= 0 && status != 0) {
            child_done = 1;
            out_open = 0;
            err_open = 0;
        }
    } else {
        child_pid = fork();
        if (child_pid == 0) {
            int st = exec_pipeline(pl);
            fflush(NULL);
            _exit(st == 0 ? 0 : 1);
        } else if (child_pid < 0) {
            status = -1;
        }
    }

    while (out_open || err_open || (!child_done && child_pid > 0)) {
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
        int r = select(maxfd + 1, &rfds, NULL, NULL, child_pid > 0 ? &(struct timeval){.tv_sec = 0, .tv_usec = 100000} : NULL);
        if (r < 0) {
            if (errno == EINTR) continue;
        } else if (r > 0) {
            if (out_open && FD_ISSET(out_pipe[0], &rfds)) {
                forward_and_collect(out_pipe[0], saved_stdout, out_buf ? &out_data : NULL, &out_len, &out_cap, MAX_KEEP, &out_open);
            }
            if (err_open && FD_ISSET(err_pipe[0], &rfds)) {
                forward_and_collect(err_pipe[0], saved_stderr, err_buf ? &err_data : NULL, &err_len, &err_cap, MAX_KEEP, &err_open);
            }
        }

        if (child_pid > 0 && !child_done) {
            int st;
            pid_t w = waitpid(child_pid, &st, WNOHANG);
            if (w == child_pid) {
                child_done = 1;
                if ((WIFEXITED(st) && WEXITSTATUS(st) != 0) || WIFSIGNALED(st)) {
                    status = -1;
                }
            }
        } else if (child_pid < 0) {
            child_done = 1;
        } else {
            child_done = 1;
        }

        if (child_pid > 0 && child_done && !out_open && !err_open) break;
    }

    if (child_pid > 0 && !child_done) {
        int st;
        if (waitpid(child_pid, &st, 0) == child_pid) {
            if ((WIFEXITED(st) && WEXITSTATUS(st) != 0) || WIFSIGNALED(st)) {
                status = -1;
            }
        } else {
            status = -1;
        }
    }

    dup2(saved_stdout, STDOUT_FILENO);
    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stdout);
    close(saved_stderr);
    close(out_pipe[0]);
    close(err_pipe[0]);

    if (out_buf) *out_buf = out_data;
    else free(out_data);
    if (err_buf) *err_buf = err_data;
    else free(err_data);

    return status;
}

/* ========================= END FILE: executor.c ============================ */

/* ============================ FILE: history.c ============================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "history.h"

static char** history = NULL;
static size_t hist_cap = 0;
static size_t hist_count = 0;

void hist_init(size_t cap) {
    hist_cap = cap;
    hist_count = 0;
    history = calloc(cap, sizeof(char*));
}

void hist_push(const char* line) {
    if (!history || hist_cap == 0 || !line) return;
    size_t idx = hist_count % hist_cap;
    free(history[idx]);
    history[idx] = strdup(line);
    hist_count++;
}

void hist_print(void) {
    if (!history) return;
    size_t start = hist_count > hist_cap ? hist_count - hist_cap : 0;
    for (size_t i = start; i < hist_count; ++i) {
        size_t idx = i % hist_cap;
        printf("%zu %s\n", i + 1, history[idx] ? history[idx] : "");
    }
}

/* ========================= END FILE: history.c ============================ */

/* ============================ FILE: logger.c ============================ */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "logger.h"

static FILE* log_fp = NULL;
static char log_file_path[4096];

static void log_vwrite(const char* prefix, const char* fmt, va_list ap) {
    if (!log_fp) return;
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    fprintf(log_fp, "%04d-%02d-%02d %02d:%02d:%02d %s",
            tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
            tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec, prefix);
    vfprintf(log_fp, fmt, ap);
    fputc('\n', log_fp);
    fflush(log_fp);
}

void log_init(const char* log_path) {
    if (log_fp) return;
    if (log_path) {
        strncpy(log_file_path, log_path, sizeof(log_file_path) - 1);
        log_file_path[sizeof(log_file_path) - 1] = '\0';
    }
    log_fp = fopen(log_file_path, "a");
}

void log_info(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vwrite("[INFO] ", fmt, ap);
    va_end(ap);
}

void log_error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vwrite("[ERROR] ", fmt, ap);
    va_end(ap);
}

void log_error_errno(const char* where) {
    if (!where) where = "";
    log_error("%s: %s", where, strerror(errno));
}

const char* log_path(void) {
    return log_file_path[0] ? log_file_path : NULL;
}

/* ========================= END FILE: logger.c ============================ */

/* ============================ FILE: maid_client.c ============================ */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "logger.h"
#include "maid_client.h"

MaidClient g_maid = { .in_fd = -1, .out_fd = -1, .running = 0, .pid = -1 };

static ssize_t readline_fd(int fd, char* buf, size_t bufsz) {
    if (!buf || bufsz == 0) return -1;
    size_t idx = 0;
    while (idx + 1 < bufsz) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r == 0) break;
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        buf[idx++] = c;
        if (c == '\n') break;
    }
    buf[idx] = '\0';
    return (ssize_t)idx;
}

static void json_escape(const char* src, char* dst, size_t dsz) {
    if (!dst || dsz == 0) return;
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j + 2 < dsz; ++i) {
        unsigned char c = (unsigned char)src[i];
        switch (c) {
            case '"':
            case '\\':
                dst[j++] = '\\';
                dst[j++] = (char)c;
                break;
            case '\n':
                dst[j++] = '\\';
                dst[j++] = 'n';
                break;
            case '\r':
                dst[j++] = '\\';
                dst[j++] = 'r';
                break;
            case '\t':
                dst[j++] = '\\';
                dst[j++] = 't';
                break;
            default:
                if (c < 0x20) {
                    if (j + 6 >= dsz) continue;
                    j += snprintf(dst + j, dsz - j, "\\u%04x", c);
                } else {
                    dst[j++] = (char)c;
                }
        }
    }
    dst[j] = '\0';
}

int maid_client_is_running(const MaidClient* mc) {
    return mc && mc->running;
}

int maid_client_start(MaidClient* mc, const char* py_path, const char* script_path) {
    if (!mc || !py_path || !script_path) return -1;
    int to_py[2];
    int from_py[2];
    if (pipe(to_py) < 0) return -1;
    if (pipe(from_py) < 0) {
        close(to_py[0]);
        close(to_py[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(to_py[0]);
        close(to_py[1]);
        close(from_py[0]);
        close(from_py[1]);
        return -1;
    }

    if (pid == 0) {
        dup2(to_py[0], STDIN_FILENO);
        dup2(from_py[1], STDOUT_FILENO);
        close(to_py[0]);
        close(to_py[1]);
        close(from_py[0]);
        close(from_py[1]);
        execlp(py_path, py_path, script_path, (char*)NULL);
        perror("maidux");
        _exit(1);
    }

    close(to_py[0]);
    close(from_py[1]);
    mc->in_fd = to_py[1];
    mc->out_fd = from_py[0];
    mc->pid = pid;
    mc->running = 1;

    int flags = fcntl(mc->out_fd, F_GETFD);
    if (flags != -1) {
        fcntl(mc->out_fd, F_SETFD, flags | FD_CLOEXEC);
    }
    flags = fcntl(mc->in_fd, F_GETFD);
    if (flags != -1) {
        fcntl(mc->in_fd, F_SETFD, flags | FD_CLOEXEC);
    }

    return 0;
}

static int write_json_line(MaidClient* mc, const char* json_line) {
    if (!maid_client_is_running(mc)) return -1;
    size_t len = strlen(json_line);
    ssize_t w = write(mc->in_fd, json_line, len);
    if (w < 0 || (size_t)w != len) return -1;
    return 0;
}

int maid_client_push_turn(MaidClient* mc, const char* cmd, const char* out, const char* err, int code) {
    if (!maid_client_is_running(mc) || !cmd) return -1;
    char esc_cmd[1024];
    char esc_out[1024];
    char esc_err[1024];
    json_escape(cmd, esc_cmd, sizeof(esc_cmd));
    json_escape(out ? out : "", esc_out, sizeof(esc_out));
    json_escape(err ? err : "", esc_err, sizeof(esc_err));
    char buf[1400];
    int n = snprintf(buf, sizeof(buf),
                     "{\"type\":\"turn\",\"cmd\":\"%s\",\"out\":\"%s\",\"err\":\"%s\",\"code\":%d}\n",
                     esc_cmd, esc_out, esc_err, code);
    if (n < 0 || (size_t)n >= sizeof(buf)) return -1;
    return write_json_line(mc, buf);
}

int maid_client_request_suggest(MaidClient* mc, char* buf, size_t bufsz) {
    if (!maid_client_is_running(mc) || !buf || bufsz == 0) return -1;
    if (write_json_line(mc, "{\"type\":\"maid\"}\n") != 0) return -1;
    ssize_t r = readline_fd(mc->out_fd, buf, bufsz);
    return r <= 0 ? -1 : 0;
}

int maid_client_stop(MaidClient* mc) {
    if (!maid_client_is_running(mc)) return 0;
    write_json_line(mc, "{\"type\":\"quit\"}\n");
    close(mc->in_fd);
    close(mc->out_fd);
    mc->in_fd = -1;
    mc->out_fd = -1;
    mc->running = 0;
    int status = 0;
    if (waitpid(mc->pid, &status, 0) < 0) {
        log_error_errno("maid waitpid");
        return -1;
    }
    return 0;
}

/* ========================= END FILE: maid_client.c ============================ */

/* ============================ FILE: main.c ============================ */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "executor.h"
#include "history.h"
#include "logger.h"
#include "parser.h"
#include "prompt.h"
#include "utils.h"
#include "maid_client.h"

int main(void) {
    log_init("maidux.log");
    hist_init(100);

    const char* py = getenv("MAID_PYTHON");
    if (!py) py = "python3";
    const char* script = getenv("MAID_BRIDGE");
    if (!script) script = "./maid_bridge.py";
    if (maid_client_start(&g_maid, py, script) != 0) {
        log_error("failed to start maid helper");
    }

    puts("(*•⌄•)===========Welcome to maidux!===========(✿•⌄•)");

    char line[1024];
    while (1) {
        prompt_print();
        if (!fgets(line, sizeof(line), stdin)) {
            putchar('\n');
            break;
        }
        str_trim(line);
        if (line[0] == '\0') continue;
        if (strcmp(line, "quit") == 0) break;

        hist_push(line);
        Pipeline pl;
        if (parse_pipeline(line, &pl) != 0) {
            perror("maidux");
            log_error_errno("parse");
            continue;
        }
        errno = 0;
        char* cap_out = NULL;
        char* cap_err = NULL;
        int exec_status = exec_pipeline_with_capture(&pl, &cap_out, &cap_err);
        if (exec_status != 0) {
            if (errno != 0) {
                perror("maidux");
                log_error_errno("exec");
            } else {
                fprintf(stderr, "maidux: command failed\n");
                log_error("exec failed");
            }
        }
        char errbuf[128] = {0};
        if (exec_status != 0) {
            if (errno != 0) {
                snprintf(errbuf, sizeof(errbuf), "%s", strerror(errno));
            } else {
                snprintf(errbuf, sizeof(errbuf), "command failed");
            }
        }
        if (strcmp(line, "maid") != 0) {
            maid_client_push_turn(&g_maid, line, cap_out ? cap_out : "", cap_err ? cap_err : errbuf, exec_status == 0 ? 0 : 1);
        }
        free_pipeline(&pl);
        free(cap_out);
        free(cap_err);
    }
    puts("(*•⌄•)===========Bye.===========(✿•⌄•)");
    maid_client_stop(&g_maid);
    return 0;
}

/* ========================= END FILE: merged.c ============================ */

/* ============================ FILE: parser.c ============================ */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "utils.h"

static void stage_init(Stage* st) {
    memset(st, 0, sizeof(*st));
}

static char* expand_operators(const char* line) {
    if (!line) return NULL;
    size_t len = strlen(line);
    size_t cap = len * 3 + 1;  // enough for inserting spaces
    char* out = malloc(cap);
    if (!out) return NULL;

    size_t oi = 0;
    for (size_t i = 0; i < len; ++i) {
        if (line[i] == '>' && i + 1 < len && line[i + 1] == '>') {
            out[oi++] = ' ';
            out[oi++] = '>';
            out[oi++] = '>';
            out[oi++] = ' ';
            i++;
        } else if (line[i] == '2' && i + 1 < len && line[i + 1] == '>') {
            out[oi++] = ' ';
            out[oi++] = '2';
            out[oi++] = '>';
            out[oi++] = ' ';
            i++;
        } else if (line[i] == '>' || line[i] == '|') {
            out[oi++] = ' ';
            out[oi++] = line[i];
            out[oi++] = ' ';
        } else {
            out[oi++] = line[i];
        }
    }
    out[oi] = '\0';
    return out;
}

void free_stage(Stage* st) {
    if (!st) return;
    for (int i = 0; i < st->argc; ++i) {
        free(st->argv[i]);
        st->argv[i] = NULL;
    }
    st->argc = 0;
    free(st->out_file);
    st->out_file = NULL;
    free(st->err_file);
    st->err_file = NULL;
    st->out_append = 0;
}

void free_pipeline(Pipeline* pl) {
    if (!pl) return;
    for (int i = 0; i < pl->n; ++i) {
        free_stage(&pl->stages[i]);
    }
    pl->n = 0;
}

static int parse_tokens(char* segment, Stage* st) {
    stage_init(st);
    char* saveptr = NULL;
    char* tok = strtok_r(segment, " \t", &saveptr);
    while (tok) {
        if (strcmp(tok, ">") == 0 || strcmp(tok, ">>") == 0) {
            st->out_append = (tok[1] == '>');
            tok = strtok_r(NULL, " \t", &saveptr);
            if (!tok) return -1;
            free(st->out_file);
            st->out_file = strdup(tok);
        } else if (strcmp(tok, "2>") == 0) {
            tok = strtok_r(NULL, " \t", &saveptr);
            if (!tok) return -1;
            free(st->err_file);
            st->err_file = strdup(tok);
        } else {
            if (st->argc >= MAX_ARGV - 1) return -1;
            st->argv[st->argc++] = strdup(tok);
        }
        tok = strtok_r(NULL, " \t", &saveptr);
    }
    st->argv[st->argc] = NULL;
    return st->argc > 0 ? 0 : -1;
}

int parse_single(const char* line, Stage* out) {
    if (!line || !out) return -1;
    char* expanded = expand_operators(line);
    if (!expanded) return -1;
    char* trimmed = str_trim(expanded);
    int ret = parse_tokens(trimmed, out);
    if (ret != 0) free_stage(out);
    free(expanded);
    return ret;
}

int parse_pipeline(const char* line, Pipeline* pl) {
    if (!line || !pl) return -1;
    pl->n = 0;
    char* expanded = expand_operators(line);
    if (!expanded) return -1;

    char* cursor = expanded;
    while (*cursor) {
        while (*cursor && isspace((unsigned char)*cursor)) cursor++;
        if (*cursor == '\0') break;

        char* seg_start = cursor;
        while (*cursor && *cursor != '|') cursor++;
        char saved = *cursor;
        *cursor = '\0';

        char* trimmed = str_trim(seg_start);
        if (trimmed[0] == '\0') {
            *cursor = saved;
            free(expanded);
            free_pipeline(pl);
            return -1;
        }

        if (pl->n >= MAX_STAGE) {
            *cursor = saved;
            free(expanded);
            free_pipeline(pl);
            return -1;
        }

        if (parse_tokens(trimmed, &pl->stages[pl->n]) != 0) {
            *cursor = saved;
            free_stage(&pl->stages[pl->n]);
            free(expanded);
            free_pipeline(pl);
            return -1;
        }
        pl->n++;

        if (saved == '|') {
            *cursor = saved;
            cursor++;
            if (*cursor == '\0') {
                free(expanded);
                free_pipeline(pl);
                return -1;
            }
        }
    }

    free(expanded);
    return pl->n > 0 ? 0 : -1;
}

/* ========================= END FILE: parser.c ============================ */

/* ============================ FILE: path.c ============================ */

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

/* ========================= END FILE: path.c ============================ */

/* ============================ FILE: prompt.c ============================ */

#include <stdio.h>
#include <unistd.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#include "prompt.h"

void prompt_print(void) {
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        seeded = 1;
    }

    // 5类：敬礼 / 打扫 / 微笑 / 咖啡 / 空白（保持很短）
    static const char *tokens[] = {
        // salute
        "🎀(｀･ω･´)ゞ", "🎀(≧▽≦)ゞ", "🎀(*´꒳`*)ゞ",
        // cleaning
        "🧹( •̀ω•́ )", "🧽(｡•̀ᴗ-)✧", "🧹(´▽`)/",
        // smile
        "♪(๑˃ᴗ˂)ﻭ", "(｡•̀ᴗ-)✧", "(´▽`*)",
        // coffee/tea
        "☕(＾-＾)", "🫖(˘ω˘)", "☕(´▽`)",
        // nothing (empty)
        "", "", ""
    };
    const int nt = (int)(sizeof(tokens) / sizeof(tokens[0]));
    const char *tok = tokens[rand() % nt];

    // 彩色：仅在终端输出时启用
    const int use_color = isatty(STDOUT_FILENO) && tok[0] != '\0';
    static const char *cols[] = { "\x1b[95m", "\x1b[96m", "\x1b[92m", "\x1b[93m", "\x1b[91m" };
    const int nc = (int)(sizeof(cols) / sizeof(cols[0]));
    const char *col = use_color ? cols[rand() % nc] : "";
    const char *rst = use_color ? "\x1b[0m" : "";

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("maidux");
        if (tok[0] != '\0') printf("%s%s%s ", col, tok, rst);
        fputs("maidux$ ", stdout);
        fflush(stdout);
        return;
    }

    // 不换行：token + 空格 + 提示符；token为空就不输出空格
    if (tok[0] != '\0') printf("%s%s%s ", col, tok, rst);
    printf("%s$ ", cwd);
    fflush(stdout);
}

/* ========================= END FILE: prompt.c ============================ */

/* ============================ FILE: utils.c ============================ */

#include <ctype.h>
#include <string.h>

#include "utils.h"

char* str_trim(char* s) {
    if (!s) return s;
    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start])) {
        start++;
    }
    size_t end = strlen(s);
    while (end > start && isspace((unsigned char)s[end - 1])) {
        end--;
    }
    if (start > 0) {
        memmove(s, s + start, end - start);
    }
    s[end - start] = '\0';
    return s;
}

/* ========================= END FILE: utils.c ============================ */
