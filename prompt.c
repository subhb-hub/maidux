#include <stdio.h>
#include <unistd.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#include "prompt.h"

void prompt_print(void) {
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
        perror("maidux");
        if (tok[0] != '\0') printf("%s%s%s ", col, tok, rst);
        fputs("maidux$ ", stdout);
        fflush(stdout);
        return;
    }

    // 不换行：token + 空格 + 提示符；token为空就不输出空格
    if (tok[0] != '\0') printf("%s%s%s ", col, tok, rst);
    printf("%s$ ", cwd);
    fflush(stdout);
}
