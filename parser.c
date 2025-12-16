#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "utils.h"

static void stage_init(Stage* st) {
    memset(st, 0, sizeof(*st));
}

static char* expand_operators(const char* line) {
    if (!line) return NULL;
    size_t len = strlen(line);
    size_t cap = len * 3 + 1;  // enough for inserting spaces
    char* out = malloc(cap);
    if (!out) return NULL;

    size_t oi = 0;
    for (size_t i = 0; i < len; ++i) {
        if (line[i] == '>' && i + 1 < len && line[i + 1] == '>') {
            out[oi++] = ' ';
            out[oi++] = '>';
            out[oi++] = '>';
            out[oi++] = ' ';
            i++;
        } else if (line[i] == '2' && i + 1 < len && line[i + 1] == '>') {
            out[oi++] = ' ';
            out[oi++] = '2';
            out[oi++] = '>';
            out[oi++] = ' ';
            i++;
        } else if (line[i] == '>' || line[i] == '|') {
            out[oi++] = ' ';
            out[oi++] = line[i];
            out[oi++] = ' ';
        } else {
            out[oi++] = line[i];
        }
    }
    out[oi] = '\0';
    return out;
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
            free(st->out_file);
            st->out_file = strdup(tok);
        } else if (strcmp(tok, "2>") == 0) {
            tok = strtok_r(NULL, " \t", &saveptr);
            if (!tok) return -1;
            free(st->err_file);
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
    char* expanded = expand_operators(line);
    if (!expanded) return -1;
    char* trimmed = str_trim(expanded);
    int ret = parse_tokens(trimmed, out);
    if (ret != 0) free_stage(out);
    free(expanded);
    return ret;
}

int parse_pipeline(const char* line, Pipeline* pl) {
    if (!line || !pl) return -1;
    pl->n = 0;
    char* expanded = expand_operators(line);
    if (!expanded) return -1;

    char* cursor = expanded;
    while (*cursor) {
        while (*cursor && isspace((unsigned char)*cursor)) cursor++;
        if (*cursor == '\0') break;

        char* seg_start = cursor;
        while (*cursor && *cursor != '|') cursor++;
        char saved = *cursor;
        *cursor = '\0';

        char* trimmed = str_trim(seg_start);
        if (trimmed[0] == '\0') {
            *cursor = saved;
            free(expanded);
            free_pipeline(pl);
            return -1;
        }

        if (pl->n >= MAX_STAGE) {
            *cursor = saved;
            free(expanded);
            free_pipeline(pl);
            return -1;
        }

        if (parse_tokens(trimmed, &pl->stages[pl->n]) != 0) {
            *cursor = saved;
            free_stage(&pl->stages[pl->n]);
            free(expanded);
            free_pipeline(pl);
            return -1;
        }
        pl->n++;

        if (saved == '|') {
            *cursor = saved;
            cursor++;
            if (*cursor == '\0') {
                free(expanded);
                free_pipeline(pl);
                return -1;
            }
        }
    }

    free(expanded);
    return pl->n > 0 ? 0 : -1;
}
