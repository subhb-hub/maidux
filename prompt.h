#ifndef MAIDUX_PROMPT_H
#define MAIDUX_PROMPT_H

#include <stddef.h>

void prompt_print(void);
size_t prompt_build(char* buf, size_t bufsz);

#endif
