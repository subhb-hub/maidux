#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#include "prompt.h"

size_t prompt_build(char* buf, size_t bufsz) {
    if (!buf || bufsz == 0) return 0;

    static int seeded = 0;
    if (!seeded) {
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        seeded = 1;
    }

    // 5类：敬礼 / 打扫 / 微笑 / 咖啡 / 空白（保持很短）
    static const char *tokens[] = {
        // salute
        "🎀(｀･ω･´)ゞ", "🎀(≧▽≦)ゞ", "🎀(*´꒳`*)ゞ",
        // cleaning
        "🧹( •̀ω•́ )", "🧽(｡•̀ᴗ-)✧", "🧹(´▽`)/",
        // smile
        "♪(๑˃ᴗ˂)ﻭ", "(｡•̀ᴗ-)✧", "(´▽`*)",
        // coffee/tea
        "☕(＾-＾)", "🫖(˘ω˘)", "☕(´▽`)",
        // nothing (empty)
        "", "", ""
    };
    const int nt = (int)(sizeof(tokens) / sizeof(tokens[0]));
    const char *tok = tokens[rand() % nt];

    // 彩色：仅在终端输出时启用
    const int use_color = isatty(STDOUT_FILENO) && tok[0] != '\0';
    static const char *cols[] = { "\x1b[95m", "\x1b[96m", "\x1b[92m", "\x1b[93m", "\x1b[91m" };
    const int nc = (int)(sizeof(cols) / sizeof(cols[0]));
    const char *col = use_color ? cols[rand() % nc] : "";
    const char *rst = use_color ? "\x1b[0m" : "";

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        size_t n = 0;
        if (tok[0] != '\0') {
            n += (size_t)snprintf(buf + n, bufsz - n, "%s%s%s ", col, tok, rst);
        }
        snprintf(buf + n, bufsz - n, "maidux$ ");
        return strnlen(buf, bufsz);
    }

    size_t n = 0;
    if (tok[0] != '\0') {
        n += (size_t)snprintf(buf + n, bufsz - n, "%s%s%s ", col, tok, rst);
    }
    snprintf(buf + n, bufsz - n, "%s$ ", cwd);
    return strnlen(buf, bufsz);
}

void prompt_print(void) {
    char buf[PATH_MAX + 64];
    prompt_build(buf, sizeof(buf));
    fputs(buf, stdout);
    fflush(stdout);
}
