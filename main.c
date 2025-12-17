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
#include "maid_client.h"

int main(void) {
    log_init("maidux.log");
    hist_init(100);

    const char* py = getenv("MAID_PYTHON");
    if (!py) py = "python3";
    const char* script = getenv("MAID_BRIDGE");
    if (!script) script = "./maid_bridge.py";
    if (maid_client_start(&g_maid, py, script) != 0) {
        log_error("failed to start maid helper");
    }

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
        char* cap_out = NULL;
        char* cap_err = NULL;
        int exec_status = exec_pipeline_with_capture(&pl, &cap_out, &cap_err);
        if (exec_status != 0) {
            if (errno != 0) {
                perror("maidux");
                log_error_errno("exec");
            } else {
                fprintf(stderr, "maidux: command failed\n");
                log_error("exec failed");
            }
        }
        char errbuf[128] = {0};
        if (exec_status != 0) {
            if (errno != 0) {
                snprintf(errbuf, sizeof(errbuf), "%s", strerror(errno));
            } else {
                snprintf(errbuf, sizeof(errbuf), "command failed");
            }
        }
        if (strcmp(line, "maid") != 0) {
            maid_client_push_turn(&g_maid, line, cap_out ? cap_out : "", cap_err ? cap_err : errbuf, exec_status == 0 ? 0 : 1);
        }
        free_pipeline(&pl);
        free(cap_out);
        free(cap_err);
    }
    puts("Bye.");
    maid_client_stop(&g_maid);
    return 0;
}
