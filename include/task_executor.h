#ifndef TASK_EXECUTOR_H
#define TASK_EXECUTOR_H

#include "../include/erraid.h"
#include <time.h>

void sigchld_handler(int signum);
int execute_command_in_terminal(struct command *cmd, uint64_t taskid);
int is_time_to_run(struct task *t, struct tm *now);
void log_execution(uint64_t taskid, int exit_code);

#endif