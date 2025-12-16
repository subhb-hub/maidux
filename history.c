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
