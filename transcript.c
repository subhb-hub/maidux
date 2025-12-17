#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "logger.h"
#include "transcript.h"

static int g_fd = -1;
static off_t g_last_sent = 0;
static char g_path[4096];

static void write_all(int fd, const char* data, size_t len) {
    if (fd < 0 || !data || len == 0) return;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, data + off, len - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        off += (size_t)w;
    }
}

void transcript_init(const char* path) {
    if (g_fd >= 0) return;
    const char* p = path ? path : "maidux.transcript.log";
    strncpy(g_path, p, sizeof(g_path) - 1);
    g_path[sizeof(g_path) - 1] = '\0';
    g_fd = open(g_path, O_CREAT | O_RDWR | O_APPEND, 0644);
    if (g_fd < 0) {
        log_error_errno("transcript open");
        return;
    }
    off_t end = lseek(g_fd, 0, SEEK_END);
    if (end >= 0) g_last_sent = end;
}

void transcript_append(const char* data, size_t len) {
    write_all(g_fd, data, len);
}

void transcript_append_str(const char* s) {
    if (!s) return;
    transcript_append(s, strlen(s));
}

void transcript_appendf(const char* fmt, ...) {
    if (!fmt) return;
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) {
        va_end(ap2);
        return;
    }
    size_t sz = (size_t)needed + 1;
    char* buf = malloc(sz);
    if (!buf) {
        va_end(ap2);
        return;
    }
    vsnprintf(buf, sz, fmt, ap2);
    va_end(ap2);
    transcript_append(buf, (size_t)needed);
    free(buf);
}

ssize_t transcript_read_delta(char** out) {
    if (out) *out = NULL;
    if (g_fd < 0) return -1;
    off_t end = lseek(g_fd, 0, SEEK_END);
    if (end < 0) return -1;
    if (end == g_last_sent) return 0;
    size_t diff = (size_t)(end - g_last_sent);
    char* buf = malloc(diff + 1);
    if (!buf) return -1;
    ssize_t r = pread(g_fd, buf, diff, g_last_sent);
    if (r < 0) {
        free(buf);
        return -1;
    }
    buf[r] = '\0';
    g_last_sent += r;
    if (out) {
        *out = buf;
    } else {
        free(buf);
    }
    return r;
}

void transcript_mark_sent(void) {
    if (g_fd < 0) return;
    off_t end = lseek(g_fd, 0, SEEK_END);
    if (end >= 0) g_last_sent = end;
}
