#include <stdlib.h>    // getenv, exit
#include <dirent.h>    // opendir, readdir, closedir
#include <string.h>    // strcmp, strlen, strerror
#include <sys/stat.h>  // stat, S_ISDIR
#include <unistd.h>    // write, STDOUT_FILENO, STDERR_FILENO, getuid
#include <errno.h>     // errno
#include <stdio.h>     // snprintf 

#define MAX_PATH_LEN 1024

/**
 * Écrit une chaîne suivie d'un retour à la ligne sur la sortie standard (stdout).
 * Remplace l'utilisation simple de printf(..., "\n").
 */
void write_line(const char *line) {
    write(STDOUT_FILENO, line, strlen(line));
    write(STDOUT_FILENO, "\n", 1);
}

/**
 * Écrit un message d'erreur préfixé sur la sortie d'erreur standard (stderr).
 * Récupère le message d'erreur système via strerror(errno).
 */
void write_error(const char *prefix) {
    const char *err_msg = strerror(errno);
    write(STDERR_FILENO, prefix, strlen(prefix));
    write(STDERR_FILENO, ": ", 2);
    write(STDERR_FILENO, err_msg, strlen(err_msg));
    write(STDERR_FILENO, "\n", 1);
}

/**
 * Vérifie si le chemin donné correspond à un répertoire.
 */
int is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) == -1) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

/**
 * Parcourt le répertoire /tmp/$USER/erraid/tasks/
 * et affiche les identifiants des tâches (répertoires).
 */
void load_all_tasks(void) {
    char base_dir[MAX_PATH_LEN];
    char message_buffer[MAX_PATH_LEN * 2];
    int len;

    const char *user = getenv("USER");
    if (!user) user = "unknown";

    // Construit le chemin : /tmp/$USER/erraid/tasks
    // (Utilisation de snprintf car il n'écrit pas dans un FILE*)
    len = snprintf(base_dir, sizeof(base_dir), "/tmp/%s/erraid/tasks", user);
    if (len >= sizeof(base_dir)) {
        write_line("Erreur: Le chemin du répertoire de base est trop long.");
        return;
    }

    DIR *dirp = opendir(base_dir);
    if (!dirp) {
        write_error("Impossible d'ouvrir le répertoire des tâches");
        return;
    }

    // Affiche le répertoire des tâches
    len = snprintf(message_buffer, sizeof(message_buffer), "Répertoire des tâches : %s", base_dir);
    write_line(message_buffer);

    struct dirent *entry;

    while ((entry = readdir(dirp)) != NULL) {
        // Ignore "." et ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char full_path[MAX_PATH_LEN];
        len = snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, entry->d_name);
        
        if (len >= sizeof(full_path)) {
            // Gère le cas où le chemin complet est trop long
            len = snprintf(message_buffer, sizeof(message_buffer), "Erreur: Chemin trop long pour %s", entry->d_name);
            write_line(message_buffer);
            continue;
        }

        if (is_directory(full_path)) {
            len = snprintf(message_buffer, sizeof(message_buffer), "Tâche trouvée : %s", entry->d_name);
            write_line(message_buffer);
        }
    }

    closedir(dirp);
}

/**
 * Point d'entrée du démon.
 */
int main(void) {
    write_line("=== Démon erraid – Étape 1 (parcours des tâches) ===");
    load_all_tasks();
    return 0;
}