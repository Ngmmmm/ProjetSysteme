#define ERRAID_H

#include <stdlib.h>    // getenv
#include <unistd.h>    // write, STDOUT_FILENO, STDERR_FILENO

#define MAX_PATH_LEN 1024

/**
 * Vérifie si le chemin donné correspond à un répertoire.
 */
int is_directory(const char *path);

/**
 * Parcourt le répertoire /tmp/$USER/erraid/tasks/
 * et affiche les identifiants des tâches (répertoires).
 */
void find_all_tasks(void);
