#ifndef MAIDUX_BUILTINS_H
#define MAIDUX_BUILTINS_H

#include "parser.h"

int builtin_dispatch(Stage* st);
int builtin_dispatch_child(Stage* st, int allow_cd);

#endif
