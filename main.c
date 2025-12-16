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
        errno = 0;
        if (exec_pipeline(&pl) != 0) {
            if (errno != 0) {
                perror("maidux");
                log_error_errno("exec");
            } else {
                fprintf(stderr, "maidux: command failed\n");
                log_error("exec failed");
            }
        }
        free_pipeline(&pl);
    }
    puts("Bye.");
    return 0;
}
