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
