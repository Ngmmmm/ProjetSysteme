#include "../include/erraid.h"
#include "../include/utils.h"
#include "../include/globals.h"
#include "../include/task_loader.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>


/**
 * Vérifie si le chemin donné correspond à un répertoire.
 * @param path Le chemin du fichier à vérifier.
 * @return 1 si c'est un répertoire, 0 sinon (y compris en cas d'erreur d'accès).
 */
int is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) == -1) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

/**
 * Libère récursivement les ressources allouées d'une struct command.
 * @note Libère seulement les champs (argv, subcommands).
 * @param cmd Pointeur vers la structure à nettoyer (peut être NULL).
 */
void free_command_contents(struct command *cmd) {
    if (cmd == NULL) return;

    if (strcmp(cmd->type, "SI") == 0) {
        for (int i = 0; i < cmd->argc; i++) {
            free(cmd->argv[i]);
        }
        free(cmd->argv);
    } else if (strcmp(cmd->type, "SQ") == 0) {
        for (size_t i = 0; i < cmd->subcount; i++) {
            free_command_contents(&cmd->subcommands[i]);
        }
        free(cmd->subcommands);
    }
}

/**
 * Libère toute la mémoire associée à une commande (structure + contenu).
 * @warning Ne doit être appelée que sur un pointeur reçu de parse_command.
 * @param cmd La commande à libérer (peut être NULL).
 */
void free_command(struct command *cmd) {
    if (cmd == NULL) return;
    free_command_contents(cmd);
    free(cmd);
}

/**
 * Lit une chaîne (taille u32be + contenu) depuis fd.
 * @param fd Descripteur de fichier source.
 * @return Une chaîne allouée (à libérer) ou NULL si erreur.
 */
char* read_string_from_fd(int fd) {
    uint32_t str_len;

    if (read_u32_be(fd, &str_len) == -1) {
        write_error("read_string: impossible de lire la longueur");
        return NULL;
    }
    
    if (str_len == 0) {
        char *empty_str = malloc(1);
        if (empty_str) empty_str[0] = '\0';
        return empty_str;
    }

    char *buffer = malloc(str_len + 1);
    if (!buffer) {
        write_error("read_string: malloc a échoué");
        return NULL;
    }
    
    size_t total = 0;
    while (total < str_len) {
        ssize_t r = read(fd, buffer + total, str_len - total);
        if (r == -1) {
            if (errno == EINTR) continue;
            write_error("read_string: erreur de lecture");
            free(buffer);
            return NULL;
        }
        if (r == 0) { 
            write_error("read_string: fin de fichier inattendue");
            free(buffer);
            return NULL;
        }
        total += (size_t)r;
    }
    
    buffer[str_len] = '\0';
    return buffer;
}

/**
 * Construit une structure command à partir d'un répertoire (récursivement pour SQ).
 * @param cmd_path Chemin du répertoire contenant la définition (fichiers type, argv...).
 * @return Une commande allouée, ou NULL si il y a une erreur.
 */
struct command *parse_command(const char *cmd_path) {
    char file_path[MAX_PATH_LEN];
    int fd;

    struct command *cmd = malloc(sizeof(struct command));
    if (!cmd) {
        write_error("malloc a échoué pour cmd");
        return NULL;
    }
    memset(cmd, 0, sizeof(struct command));

    snprintf(file_path, MAX_PATH_LEN, "%s/type", cmd_path);
    fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        write_error("impossible d'ouvrir le fichier type"); // Peut arriver si dossier vide/incomplet
        free(cmd);
        return NULL;
    }

    uint16_t type_from_file;
    if (read_u16_be(fd, &type_from_file) == -1) {
        close(fd); free(cmd); 
        return NULL;
    }
    close(fd);

    if (type_from_file == FILE_TYPE_SI) {
        strcpy(cmd->type, "SI");
    } else if (type_from_file == FILE_TYPE_SQ) {
        strcpy(cmd->type, "SQ");
    } else {
        free(cmd); 
        return NULL;
    }

    // Parsing selon le type
    if (strcmp(cmd->type, "SI") == 0) {
        snprintf(file_path, MAX_PATH_LEN, "%s/argv", cmd_path);
        fd = open(file_path, O_RDONLY);
        if (fd == -1) { free(cmd); return NULL; }

        uint32_t argc_from_file;
        if (read_u32_be(fd, &argc_from_file) == -1) { close(fd); free(cmd); return NULL; }
        cmd->argc = (int)argc_from_file;
        
        if (cmd->argc < 1) { close(fd); free(cmd); return NULL; }

        cmd->argv = malloc(sizeof(char *) * (cmd->argc + 1));
        if (!cmd->argv) { close(fd); free(cmd); return NULL; }

        for (int i = 0; i < cmd->argc; i++) {
            cmd->argv[i] = read_string_from_fd(fd);
            if (cmd->argv[i] == NULL) {
                for (int j = 0; j < i; j++) free(cmd->argv[j]);
                free(cmd->argv);
                close(fd); free(cmd);
                return NULL;
            }
        }
        cmd->argv[cmd->argc] = NULL;
        close(fd);

    } else if (strcmp(cmd->type, "SQ") == 0) {
        DIR *d = opendir(cmd_path);
        if (!d) { free(cmd); return NULL; }

        struct dirent *entry;
        size_t count = 0;
        
        // Compter les sous-dossiers
        while ((entry = readdir(d)) != NULL) {
            if (isdigit(entry->d_name[0])) count++;
        }
        
        cmd->subcount = count;
        
        if (cmd->subcount > 0) {
            cmd->subcommands = malloc(cmd->subcount * sizeof(struct command));
            if (!cmd->subcommands) { closedir(d); free(cmd); return NULL; }
            memset(cmd->subcommands, 0, cmd->subcount * sizeof(struct command));
            
            rewinddir(d);
            while ((entry = readdir(d)) != NULL) {
                if (isdigit(entry->d_name[0])) {
                    int index = atoi(entry->d_name);
                    if (index >= 0 && (size_t)index < cmd->subcount) {
                        snprintf(file_path, MAX_PATH_LEN, "%s/%s", cmd_path, entry->d_name);
                        struct command *temp_cmd = parse_command(file_path);
                        
                        if (temp_cmd) {
                            cmd->subcommands[index] = *temp_cmd; // Copie contenu
                            free(temp_cmd); 
                        }
                    }
                }
            }
        }
        closedir(d);
    }
    return cmd;
}

/**
 * Lit et parse le fichier 'timing' pour une tâche donnée.
 * @param task_path Le chemin vers le dossier de la tâche (ex: "/tmp/user/erraid/tasks/10").
 * @param task_timing Un pointeur vers une struct timing.
 * @return 0 en cas de succès, -1 en cas d'erreur.
 */
int parse_timing(const char *task_path, struct timing *task_timing) {
    char timing_path[MAX_PATH_LEN];
    snprintf(timing_path, sizeof(timing_path), "%s/timing", task_path);

    int fd = open(timing_path, O_RDONLY);
    if (fd == -1) return -1;

    int error = 0;
    if (read_u64_be(fd, &task_timing->minutes) == -1) error = 1;
    if (!error && read_u32_be(fd, &task_timing->hours) == -1) error = 1;
    // Lecture directe pour 1 octet
    if (!error && read(fd, &task_timing->daysofweek, 1) != 1) error = 1;

    close(fd);
    return error ? -1 : 0;
}

/**
 * Fonction de comparaison pour qsort, pour trier les tâches par taskid.
 * @param a, b Pointeurs vers les structures task.
 * @return -1 si a < b, 1 si a > b, 0 si égal.
 */
int compare_tasks(const void *a, const void *b) {
    struct task *taskA = (struct task *)a;
    struct task *taskB = (struct task *)b;
    if (taskA->taskid < taskB->taskid) return -1;
    if (taskA->taskid > taskB->taskid) return 1;
    return 0;
}

/**
 * Charge toutes les tâches en utilisant un tableau dynamique et les trie par ID.
 * @param task_count Un pointeur de sortie pour stocker le nombre de tâches trouvées.
 * @return Un pointeur vers le tableau de 'struct task', ou NULL en cas d'erreur.
 */
struct task *load_all_tasks(size_t *task_count) {
    char base_dir[MAX_PATH_LEN];
    
    snprintf(base_dir, sizeof(base_dir), "%s/tasks", g_root_dir);

    DIR *dirp = opendir(base_dir);
    if (!dirp) return NULL;

    size_t count = 0;     
    size_t capacity = 10;
    struct task *task_array = malloc(sizeof(struct task) * capacity);
    if (!task_array) { closedir(dirp); return NULL; }

    struct dirent *entry;
    while ((entry = readdir(dirp)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char current_task_path[MAX_PATH_LEN];
        snprintf(current_task_path, sizeof(current_task_path), "%s/%s", base_dir, entry->d_name);
        
        if (is_directory(current_task_path)) {
            if (count == capacity) {
                capacity *= 2;
                struct task *new_array = realloc(task_array, sizeof(struct task) * capacity);
                if (!new_array) { free(task_array); closedir(dirp); return NULL; }
                task_array = new_array;
            }

            struct task *current_task = &task_array[count];
            // Conversion sécurisée string -> uint64
            current_task->taskid = strtoull(entry->d_name, NULL, 10);

            if (parse_timing(current_task_path, &current_task->timing) != 0) continue;

            char cmd_path[MAX_PATH_LEN];
            snprintf(cmd_path, sizeof(cmd_path), "%s/cmd", current_task_path);
            current_task->command = parse_command(cmd_path);
            
            if (current_task->command) {
                count++;
            }
        }
    }
    closedir(dirp);

    if (count == 0) {
        free(task_array);
        *task_count = 0;
        return NULL;
    }

    qsort(task_array, count, sizeof(struct task), compare_tasks);
    *task_count = count;
    return task_array;
}