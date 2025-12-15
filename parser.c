#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "utils.h"

static void stage_init(Stage* st) {
    memset(st, 0, sizeof(*st));
}

void free_stage(Stage* st) {
    if (!st) return;
    for (int i = 0; i < st->argc; ++i) {
        free(st->argv[i]);
        st->argv[i] = NULL;
    }
    st->argc = 0;
    free(st->out_file);
    st->out_file = NULL;
    free(st->err_file);
    st->err_file = NULL;
    st->out_append = 0;
}

void free_pipeline(Pipeline* pl) {
    if (!pl) return;
    for (int i = 0; i < pl->n; ++i) {
        free_stage(&pl->stages[i]);
    }
    pl->n = 0;
}

static int parse_tokens(char* segment, Stage* st) {
    stage_init(st);
    char* saveptr = NULL;
    char* tok = strtok_r(segment, " \t", &saveptr);
    while (tok) {
        if (strcmp(tok, ">") == 0 || strcmp(tok, ">>") == 0) {
            st->out_append = (tok[1] == '>');
            tok = strtok_r(NULL, " \t", &saveptr);
            if (!tok) return -1;
            st->out_file = strdup(tok);
        } else if (strcmp(tok, "2>") == 0) {
            tok = strtok_r(NULL, " \t", &saveptr);
            if (!tok) return -1;
            st->err_file = strdup(tok);
        } else {
            if (st->argc >= MAX_ARGV - 1) return -1;
            st->argv[st->argc++] = strdup(tok);
        }
        tok = strtok_r(NULL, " \t", &saveptr);
    }
    st->argv[st->argc] = NULL;
    return st->argc > 0 ? 0 : -1;
}

int parse_single(const char* line, Stage* out) {
    if (!line || !out) return -1;
    char* copy = strdup(line);
    char* trimmed = str_trim(copy);
    int ret = parse_tokens(trimmed, out);
    free(copy);
    return ret;
}

int parse_pipeline(const char* line, Pipeline* pl) {
    if (!line || !pl) return -1;
    pl->n = 0;
    char* copy = strdup(line);
    char* saveptr = NULL;
    char* seg = strtok_r(copy, "|", &saveptr);
    while (seg) {
        if (pl->n >= MAX_STAGE) {
            free(copy);
            return -1;
        }
        char* trimmed = str_trim(seg);
        if (parse_tokens(trimmed, &pl->stages[pl->n]) != 0) {
            free(copy);
            return -1;
        }
        pl->n++;
        seg = strtok_r(NULL, "|", &saveptr);
    }
    free(copy);
    return pl->n > 0 ? 0 : -1;
}
