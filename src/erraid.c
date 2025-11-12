#include "../include/erraid.h"
#include "../include/utils.h" 
#include <stdlib.h>  
#include <dirent.h>   
#include <string.h>  
#include <sys/stat.h> 
#include <unistd.h>    
#include <errno.h>     
#include <stdio.h>    
#include <fcntl.h> 
#include <stddef.h>



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

int read_timing_struct(int fd, struct timing *timing_out) {
    
    //MINUTES
    if (read_u64_be(fd, &timing_out->minutes) == -1) {
        return -1;
    }
    //HOURS 
    if (read_u32_be(fd, &timing_out->hours) == -1) {
        return -1;
    }
    //DAYSOFWEEK 
    if (read_u8(fd, &timing_out->daysofweek) == -1) {
        return -1;
    }
    return 0;
}

/**
 * Parcourt le répertoire /tmp/$USER/erraid/tasks/
 * et affiche les identifiants des tâches (répertoires).
 */
void find_all_tasks(void) {
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

        char task_path[MAX_PATH_LEN];
        len = snprintf(task_path, sizeof(task_path), "%s/%s", base_dir, entry->d_name);
        
        if (len >= sizeof(task_path)) {
            snprintf(message_buffer, sizeof(message_buffer), "Erreur: Chemin trop long pour %s", entry->d_name);
            write_line(message_buffer);
            continue;
        }

        if (is_directory(task_path)) {
            snprintf(message_buffer, sizeof(message_buffer), "Tâche trouvée : %s", entry->d_name);
            write_line(message_buffer);

            // === Lecture du fichier timing ===
            char timing_path[MAX_PATH_LEN];
            snprintf(timing_path, sizeof(timing_path), "%s/timing", task_path);

            int fd = open(timing_path, O_RDONLY);
            if (fd == -1) {
                write_error("Impossible d'ouvrir le fichier timing");
                continue;
            }

            struct timing task_timing;

            if (read_timing_struct(fd, &task_timing) == -1) {
                write_error("  - Erreur de lecture du fichier timing");
                close(fd);
                continue;
            }
            close(fd); 
        }
    }

    closedir(dirp);
}



int main(void) {
    write_line("=== Démon erraid - Étape 1 (parcours des tâches) ===");
    find_all_tasks();
    return 0;
}