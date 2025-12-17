#ifndef MAIDUX_EXECUTOR_H
#define MAIDUX_EXECUTOR_H

#include "parser.h"

int exec_stage(Stage* st);
int exec_pipeline(Pipeline* pl);
int exec_pipeline_with_capture(Pipeline* pl, char** out_buf, char** err_buf);
int exec_builtin_parent_with_capture(Stage* st, char** out_buf, char** err_buf);

#endif
