#ifndef MAIDUX_TRANSCRIPT_H
#define MAIDUX_TRANSCRIPT_H

#include <stddef.h>
#include <sys/types.h>

void transcript_init(const char* path);
void transcript_append(const char* data, size_t len);
void transcript_append_str(const char* s);
void transcript_appendf(const char* fmt, ...);
ssize_t transcript_read_delta(char** out);
void transcript_mark_sent(void);

#endif
