#ifndef SERVER_COMM_H
#define SERVER_COMM_H

#include "../include/erraid.h"
#include <stddef.h>

void init_communication();
void handle_client_request(int req_fd, struct task **tasks, size_t *task_count);

#endif