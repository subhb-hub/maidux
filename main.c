#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "executor.h"
#include "history.h"
#include "builtins.h"
#include "logger.h"
#include "parser.h"
#include "prompt.h"
#include "utils.h"
#include "maid_client.h"
#include "transcript.h"

int main(void) {
    log_init("maidux.log");
    transcript_init("maidux.transcript.log");
    hist_init(100);

    const char* py = getenv("MAID_PYTHON");
    if (!py) py = "python3";
    const char* script = getenv("MAID_BRIDGE");
    if (!script) script = "./maid_bridge.py";
    if (maid_client_start(&g_maid, py, script) != 0) {
        log_error("failed to start maid helper");
    }

    const char* welcome = "(*•⌄•)===========Welcome to maidux!===========(✿•⌄•)";
    puts(welcome);
    transcript_appendf("%s\n", welcome);

    char line[1024];
    char prompt_buf[PATH_MAX + 64];
    while (1) {
        prompt_build(prompt_buf, sizeof(prompt_buf));
        fputs(prompt_buf, stdout);
        fflush(stdout);
        transcript_append_str(prompt_buf);
        if (!fgets(line, sizeof(line), stdin)) {
            putchar('\n');
            transcript_append("\n", 1);
            break;
        }
        transcript_append_str(line);
        str_trim(line);
        if (line[0] == '\0') continue;
        if (strcmp(line, "quit") == 0) break;

        hist_push(line);
        Pipeline pl;
        int parse_status = parse_pipeline(line, &pl);
        int exec_status = -1;
        char* cap_out = NULL;
        char* cap_err = NULL;
        char errbuf[128] = {0};

        if (parse_status != 0) {
            const char* perr = errno ? strerror(errno) : "parse failed";
            snprintf(errbuf, sizeof(errbuf), "maidux: %s", perr);
            fprintf(stderr, "%s\n", errbuf);
            transcript_appendf("%s\n", errbuf);
            log_error_errno("parse");
        } else {
            errno = 0;
            if (pl.n == 1 && builtin_requires_parent(&pl.stages[0])) {
                exec_status = exec_builtin_parent_with_capture(&pl.stages[0], &cap_out, &cap_err);
            } else {
                exec_status = exec_pipeline_with_capture(&pl, &cap_out, &cap_err);
            }

            if (exec_status != 0) {
                if (errno != 0) {
                    snprintf(errbuf, sizeof(errbuf), "maidux: %s", strerror(errno));
                    fprintf(stderr, "%s\n", errbuf);
                    transcript_appendf("%s\n", errbuf);
                    log_error_errno("exec");
                } else {
                    snprintf(errbuf, sizeof(errbuf), "maidux: command failed");
                    fprintf(stderr, "%s\n", errbuf);
                    transcript_appendf("%s\n", errbuf);
                    log_error("exec failed");
                }
            }
        }

        if (strcmp(line, "maid") != 0) {
            char* delta = NULL;
            ssize_t dlen = transcript_read_delta(&delta);
            const char* maid_err = errbuf[0] ? errbuf : (cap_err ? cap_err : "");
            if (dlen > 0 && delta) {
                maid_client_push_turn(&g_maid, line, delta, maid_err, exec_status == 0 ? 0 : 1);
            }
            free(delta);
        } else {
            transcript_mark_sent();
        }

        free_pipeline(&pl);
        free(cap_out);
        free(cap_err);
    }
    puts("(*•⌄•)===========Bye.===========(✿•⌄•)");
    transcript_append_str("(*•⌄•)===========Bye.===========(✿•⌄•)\n");
    maid_client_stop(&g_maid);
    return 0;
}
