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
