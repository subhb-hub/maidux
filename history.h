#ifndef MAIDUX_HISTORY_H
#define MAIDUX_HISTORY_H

#include <stddef.h>

void hist_init(size_t cap);
void hist_push(const char* line);
void hist_print(void);

#endif
