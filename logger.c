#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "logger.h"

static FILE* log_fp = NULL;

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
    log_fp = fopen(log_path, "a");
}

void log_info(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vwrite("[INFO] ", fmt, ap);
    va_end(ap);
}

void log_error_errno(const char* where) {
    if (!where) where = "";
    log_info("[ERROR] %s: %s", where, strerror(errno));
}
