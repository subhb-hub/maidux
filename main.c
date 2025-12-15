#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "executor.h"
#include "history.h"
#include "logger.h"
#include "parser.h"
#include "prompt.h"
#include "utils.h"

int main(void) {
    log_init("maidux.log");
    hist_init(100);

    puts("Welcome to maidux!");

    char line[1024];
    while (1) {
        prompt_print();
        if (!fgets(line, sizeof(line), stdin)) {
            putchar('\n');
            break;
        }
        str_trim(line);
        if (line[0] == '\0') continue;
        if (strcmp(line, "quit") == 0) break;

        hist_push(line);
        Pipeline pl;
        if (parse_pipeline(line, &pl) != 0) {
            perror("maidux");
            log_error_errno("parse");
            continue;
        }
        if (exec_pipeline(&pl) != 0) {
            log_error_errno("exec");
            perror("maidux");
        }
        free_pipeline(&pl);
    }
    puts("Bye.");
    return 0;
}
