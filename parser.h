#ifndef MAIDUX_PARSER_H
#define MAIDUX_PARSER_H

#define MAX_ARGV 64
#define MAX_STAGE 8

typedef struct {
    char* argv[MAX_ARGV];
    int argc;
    char* out_file;
    int out_append;
    char* err_file;
} Stage;

typedef struct {
    Stage stages[MAX_STAGE];
    int n;
} Pipeline;

int parse_single(const char* line, Stage* out);
int parse_pipeline(const char* line, Pipeline* pl);
void free_stage(Stage* st);
void free_pipeline(Pipeline* pl);

#endif
