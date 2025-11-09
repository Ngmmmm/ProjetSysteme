#define ERRAID_H

#include <stdlib.h>    // getenv
#include <unistd.h>    // write, STDOUT_FILENO, STDERR_FILENO

#define MAX_PATH_LEN 1024

/**
 * Écrit une chaîne suivie d'un retour à la ligne sur la sortie standard (stdout).
 */
void write_line(const char *line);

/**
 * Écrit un message d'erreur sur la sortie d'erreur standard (stderr).
 * Récupère le message d'erreur système via strerror(errno).
 */
void write_error(const char *prefix);

/**
 * Vérifie si le chemin donné correspond à un répertoire.
 */
int is_directory(const char *path);

/**
 * Parcourt le répertoire /tmp/$USER/erraid/tasks/
 * et affiche les identifiants des tâches (répertoires).
 */
void find_all_tasks(void);
