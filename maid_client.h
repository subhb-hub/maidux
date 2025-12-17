#ifndef MAIDUX_MAID_CLIENT_H
#define MAIDUX_MAID_CLIENT_H

#include <stddef.h>

typedef struct {
    int in_fd;   // parent -> python stdin
    int out_fd;  // python stdout -> parent
    int running;
    int pid;
} MaidClient;

extern MaidClient g_maid;

int maid_client_start(MaidClient* mc, const char* py_path, const char* script_path);
int maid_client_push_turn(MaidClient* mc, const char* cmd, const char* out, const char* err, int code);
int maid_client_request_suggest(MaidClient* mc, char* buf, size_t bufsz);
int maid_client_stop(MaidClient* mc);
int maid_client_is_running(const MaidClient* mc);

#endif
