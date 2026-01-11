#ifndef TASK_EXECUTOR_H
#define TASK_EXECUTOR_H

#include "../include/erraid.h"
#include <time.h>

/**
 * Gestionnaire du signal SIGCHLD pour éviter les processus zombies.
 */
void sigchld_handler(int signum);

/**
 * Exécute une commande (SI, SQ, PL, IF) dans un terminal.
 * La sortie standard et d'erreur sont redirigées vers les fichiers
 * correspondants à la tâche.
 */
int execute_command_in_terminal(struct command *cmd, uint64_t taskid);

/**
 * Vérifie si une tâche doit être exécutée à l'instant donné.
 */
int is_time_to_run(struct task *t, struct tm *now);

/**
 * Enregistre le timestamp et le code de sortie d'une exécution
 * dans le fichier binaire 'times-exitcodes' de la tâche.
 */
void log_execution(uint64_t taskid, int exit_code);

#endif
