#ifndef TASK_LOADER_H
#define TASK_LOADER_H

#include "../include/erraid.h"
#include <stddef.h>

int is_directory(const char *path);
char* read_string_from_fd(int fd);
struct command *parse_command(const char *cmd_path);
int parse_timing(const char *task_path, struct timing *task_timing);
void free_command(struct command *cmd);
void free_command_contents(struct command *cmd);
struct task *load_all_tasks(size_t *task_count);

#endif