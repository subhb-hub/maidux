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

static void builtin_err(const char* cmd, const char* arg) {
    int e = errno;
    if (arg) {
        dprintf(STDERR_FILENO, "%s: %s: %s\n", cmd, arg, strerror(e));
    } else {
        dprintf(STDERR_FILENO, "%s: %s\n", cmd, strerror(e));
    }
    errno = e;
}
static int copy_file(const char* cmd, const char* src, const char* dst) {
    int in = open(src, O_RDONLY);
    if (in < 0) { builtin_err(cmd, src); return -1; }
    int out = open(dst, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (out < 0) {
        builtin_err(cmd, dst);
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
                builtin_err(cmd, dst);
                close(in);
                close(out);
                return -1;
            }
            off += w;
        }
    }
    if (n < 0) {
        builtin_err(cmd, src);
        close(in);
        close(out);
        return -1;
    }
    close(in);
    close(out);
    return 0;
}

static int copy_dir_recursive(const char* cmd, const char* src, const char* dst) {
    DIR* dir = opendir(src);
    if (!dir) { builtin_err(cmd, src); return -1; }
    if (mkdir(dst, 0755) < 0) {
        if (errno != EEXIST) {
            builtin_err(cmd, dst);
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
            builtin_err(cmd, src_path);
            ret = -1;
            break;
        }
        if (S_ISDIR(st.st_mode)) {
            if (copy_dir_recursive(cmd, src_path, dst_path) != 0) { ret = -1; break; }
        } else {
            if (copy_file(cmd, src_path, dst_path) != 0) { ret = -1; break; }
        }
    }
    closedir(dir);
    return ret;
}

static int remove_recursive(const char* cmd, const char* path) {
    struct stat st;
    if (lstat(path, &st) != 0) { builtin_err(cmd, path); return -1; }
    if (S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(path);
        if (!dir) { builtin_err(cmd, path); return -1; }
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char child[PATH_MAX];
            snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
            if (remove_recursive(cmd, child) != 0) { closedir(dir); return -1; }
        }
        closedir(dir);
        if (rmdir(path) != 0) { builtin_err(cmd, path); return -1; }
        return 0;
    }
    if (unlink(path) != 0) { builtin_err(cmd, path); return -1; }
    return 0;
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

static int move_path(const char* cmd, const char* src, const char* dst) {
    if (rename(src, dst) == 0) return 0;
    if (errno != EXDEV) { builtin_err(cmd, src); return -1; }
    // cross-device: copy then remove
    struct stat st;
    if (stat(src, &st) != 0) { builtin_err(cmd, src); return -1; }
    int ret;
    if (S_ISDIR(st.st_mode)) {
        ret = copy_dir_recursive(cmd, src, dst);
    } else {
        ret = copy_file(cmd, src, dst);
    }
    if (ret == 0) ret = remove_recursive(cmd, src);
    return ret;
}

static int do_pwd(void) {
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf)) == NULL) {
        builtin_err("xpwd", NULL);
        return -1;
    }
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
    builtin_err("xcd", target);
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
    if (!dir) { builtin_err("xls", target); return -1; }
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
    if (st->argc < 2) { errno = EINVAL; builtin_err("xtouch", NULL); return -1; }
    int fd = open(st->argv[1], O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) {
        if (errno == EEXIST) return 0;
        builtin_err("xtouch", st->argv[1]);
        return -1;
    }
    close(fd);
    return 0;
}

static int do_cat(Stage* st) {
    if (st->argc < 2) { errno = EINVAL; builtin_err("xcat", NULL); return -1; }

    int fd = open(st->argv[1], O_RDONLY);
    if (fd < 0) { builtin_err("xcat", st->argv[1]); return -1; }
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, n) < 0) {
            close(fd);
            builtin_err("xcat", "write");
            return -1;
        }
    }
    close(fd);
    if (n < 0) { builtin_err("xcat", "read"); return -1; }
    return 0;
}

static int do_cp(Stage* st) {
    if (st->argc < 3) { errno = EINVAL; builtin_err("xcp", NULL); return -1; }
    int recursive = 0;
    const char* src = NULL;
    const char* dst = NULL;
    if (strcmp(st->argv[1], "-r") == 0) {
        recursive = 1;
        if (st->argc < 4) { errno = EINVAL; builtin_err("xcp", NULL); return -1; }
        src = st->argv[2];
        dst = st->argv[3];
    } else {
        src = st->argv[1];
        dst = st->argv[2];
    }
    struct stat stbuf;
    if (stat(src, &stbuf) != 0) { builtin_err("xcp", src); return -1; }
    if (S_ISDIR(stbuf.st_mode)) {
        if (!recursive) { errno = EISDIR; builtin_err("xcp", src); return -1; }
        return copy_dir_recursive("xcp", src, dst);
    }
    return copy_file("xcp", src, dst);
}

static int do_rm(Stage* st) {
    if (st->argc < 2) { errno = EINVAL; builtin_err("xrm", NULL); return -1; }
    int recursive = 0;
    const char* target = NULL;
    if (strcmp(st->argv[1], "-r") == 0) {
        recursive = 1;
        if (st->argc < 3) { errno = EINVAL; builtin_err("xrm", NULL); return -1; }
        target = st->argv[2];
    } else {
        target = st->argv[1];
    }
    struct stat stbuf;
    if (lstat(target, &stbuf) != 0) { builtin_err("xrm", target); return -1; }
    if (S_ISDIR(stbuf.st_mode) && !recursive) { errno = EISDIR; builtin_err("xrm", target); return -1; }
    return remove_recursive("xrm", target);
}

static int do_mv(Stage* st) {
    if (st->argc < 3) { errno = EINVAL; builtin_err("xmv", NULL); return -1; }
    int recursive = 0;
    const char* src = NULL;
    const char* dst = NULL;
    if (strcmp(st->argv[1], "-r") == 0) {
        recursive = 1;
        if (st->argc < 4) { errno = EINVAL; builtin_err("xmv", NULL); return -1; }
        src = st->argv[2];
        dst = st->argv[3];
    } else {
        src = st->argv[1];
        dst = st->argv[2];
    }
    struct stat stbuf;
    if (lstat(src, &stbuf) != 0) { builtin_err("xmv", src); return -1; }
    if (S_ISDIR(stbuf.st_mode) && !recursive) { errno = EISDIR; builtin_err("xmv", src); return -1; }
    return move_path("xmv", src, dst);
}

static int do_tee(Stage* st) {
    if (st->argc < 2) { errno = EINVAL; builtin_err("xtee", NULL); return -1; }
    int fd = open(st->argv[1], O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) { builtin_err("xtee", st->argv[1]); return -1; }
    char buf[4096];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, n) < 0) { close(fd); builtin_err("xtee", "write"); return -1; }
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(fd, buf + off, n - off);
            if (w < 0) { close(fd); builtin_err("xtee", st->argv[1]); return -1; }
            off += w;
        }
    }
    close(fd);
    if (n < 0) { builtin_err("xtee", "read"); return -1; }
    return 0;
}

static int do_journalctl(void) {
    const char* path = log_path();
    if (!path) { errno = ENOENT; builtin_err("xjournalctl", NULL); return -1; }
    int fd = open(path, O_RDONLY);
    if (fd < 0) { builtin_err("xjournalctl", path); return -1; }
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, n) < 0) { close(fd); builtin_err("xjournalctl", "write"); return -1; }
    }
    close(fd);
    if (n < 0) { builtin_err("xjournalctl", "read"); return -1; }
    return 0;
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

int builtin_requires_parent(const Stage* st) {
    if (!st || st->argc == 0) return 0;
    return strcmp(st->argv[0], "xcd") == 0;
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
