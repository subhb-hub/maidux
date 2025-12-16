#ifndef MAIDUX_LOGGER_H
#define MAIDUX_LOGGER_H

void log_init(const char* log_path);
void log_info(const char* fmt, ...);
void log_error(const char* fmt, ...);
void log_error_errno(const char* where);
const char* log_path(void);

#endif
