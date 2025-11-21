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
#include <ctype.h>
#include <stddef.h>
#include <sys/wait.h>
#include <time.h>
#include <endian.h>
#include <getopt.h> 

// Variable globale pour la racine 
char g_root_dir[MAX_PATH_LEN];

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
 * Libère récursivement le contenu d'une struct command.
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
 * Libère toute la mémoire associée à une commande.
 */
void free_command(struct command *cmd) {
    if (cmd == NULL) return;
    free_command_contents(cmd);
    free(cmd);
}

/**
 * Lit une chaîne depuis un descripteur de fichier.
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
    
    ssize_t bytes_read = read(fd, buffer, str_len);
    if (bytes_read == -1 || bytes_read != (ssize_t)str_len) {
        write_error("read_string: erreur de lecture");
        free(buffer);
        return NULL;
    }
    
    buffer[str_len] = '\0';
    return buffer;
}

/**
 * Construit une structure command à partir d'un répertoire.
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

    // Lire le type
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
 * Lit le fichier timing.
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
 * Comparateur pour qsort.
 */
int compare_tasks(const void *a, const void *b) {
    struct task *taskA = (struct task *)a;
    struct task *taskB = (struct task *)b;
    if (taskA->taskid < taskB->taskid) return -1;
    if (taskA->taskid > taskB->taskid) return 1;
    return 0;
}

/**
 * Charge toutes les tâches.
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


int execute_command_in_terminal(struct command *cmd, uint64_t taskid);

int execute_sequence_command_in_terminal(struct command *cmd, uint64_t taskid) {
    int last_exit_code = 0;
    if (cmd == NULL || cmd->subcount == 0) return 0;

    for (size_t i = 0; i < cmd->subcount; i++) {
        last_exit_code = execute_command_in_terminal(&cmd->subcommands[i], taskid);
        if (last_exit_code != 0) break;
    }
    return last_exit_code;
}

int execute_simple_command_in_terminal(struct command *cmd, uint64_t taskid) { 
    if (cmd == NULL || strcmp(cmd->type, "SI") != 0 || cmd->argc < 1) return -1;

    pid_t pid = fork();
    if (pid == -1) return -1;

    if (pid == 0) {
        char stdout_path[MAX_PATH_LEN], stderr_path[MAX_PATH_LEN];
        
        // Chemins des logs basés sur g_root_dir
        snprintf(stdout_path, MAX_PATH_LEN, "%s/tasks/%lu/stdout", g_root_dir, taskid);
        snprintf(stderr_path, MAX_PATH_LEN, "%s/tasks/%lu/stderr", g_root_dir, taskid);


        int fd_out = open(stdout_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        int fd_err = open(stderr_path, O_WRONLY | O_CREAT | O_APPEND, 0644);

        if (fd_out != -1) { dup2(fd_out, STDOUT_FILENO); close(fd_out); }
        if (fd_err != -1) { dup2(fd_err, STDERR_FILENO); close(fd_err); }

        execvp(cmd->argv[0], cmd->argv);
        exit(127);
    } else {
        int wstatus;
        if (waitpid(pid, &wstatus, 0) == -1) return -1;
        return WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;
    }
}

int execute_command_in_terminal(struct command *cmd, uint64_t taskid) { 
    if (!cmd) return -1;
    if (strcmp(cmd->type, "SI") == 0) return execute_simple_command_in_terminal(cmd, taskid);
    if (strcmp(cmd->type, "SQ") == 0) return execute_sequence_command_in_terminal(cmd, taskid);
    return -1;
}

int is_time_to_run(struct task *t, struct tm *now) {
    uint64_t min_bit = (uint64_t)1 << now->tm_min;
    uint32_t hour_bit = (uint32_t)1 << now->tm_hour;
    uint8_t  day_bit  = (uint8_t)1  << now->tm_wday;

    if ((t->timing.minutes & min_bit) == 0) return 0;
    if ((t->timing.hours & hour_bit) == 0) return 0;
    if ((t->timing.daysofweek & day_bit) == 0) return 0;
    return 1;
}

void log_execution(uint64_t taskid, int exit_code) {
    char log_path[MAX_PATH_LEN];
    snprintf(log_path, MAX_PATH_LEN, "%s/tasks/%lu/times-exitcodes", g_root_dir, taskid);

    int fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) return;

    uint64_t time_be = htobe64(time(NULL)); 
    uint16_t code_be = (exit_code == -1) ? htobe16(0xFFFF) : htobe16((uint16_t)exit_code);

    write(fd, &time_be, sizeof(time_be));
    write(fd, &code_be, sizeof(code_be));
    close(fd);
}


int main(int argc, char *argv[]) {
    // Initialisation du chemin racine par défaut
    const char *user = getenv("USER");
    if (!user) user = "unknown";
    snprintf(g_root_dir, sizeof(g_root_dir), "/tmp/%s/erraid", user);

    // Gestion de l'option -r (pour les tests)
    int opt;
    while ((opt = getopt(argc, argv, "r:")) != -1) {
        if (opt == 'r') {
            strncpy(g_root_dir, optarg, sizeof(g_root_dir) - 1);
            g_root_dir[sizeof(g_root_dir) - 1] = '\0';
        }
    }

    char buffer[MAX_PATH_LEN + 64];
    snprintf(buffer, sizeof(buffer), "=== Démon erraid démarré sur : %s ===", g_root_dir);
    write_line(buffer);

    //  Chargement
    size_t task_count = 0;
    struct task *all_tasks = load_all_tasks(&task_count);

    if (all_tasks == NULL && task_count == 0) {
        write_line("Aucune tâche chargée ou erreur fatale.");

    }

    snprintf(buffer, sizeof(buffer), "%zu tâches chargées.", task_count);
    write_line(buffer);

    // Synchronisation initiale sur la minute
    time_t start_now = time(NULL);
    int seconds_to_wait = 60 - (start_now % 60);
    write_line("Synchronisation horloge...");
    sleep(seconds_to_wait);

    // Boucle Principale 
    while (1) {
        time_t loop_start_time = time(NULL);
        struct tm *local_now = localtime(&loop_start_time);

        for (size_t i = 0; i < task_count; i++) {
            struct task *t = &all_tasks[i];
            
            if (is_time_to_run(t, local_now)) {
                snprintf(buffer, sizeof(buffer), "Exécution de la tâche ID %lu...", t->taskid);
                write_line(buffer);

                // Vider les logs AVANT l'exécution
                char p_out[MAX_PATH_LEN], p_err[MAX_PATH_LEN];
                snprintf(p_out, MAX_PATH_LEN, "%s/tasks/%lu/stdout", g_root_dir, t->taskid);
                snprintf(p_err, MAX_PATH_LEN, "%s/tasks/%lu/stderr", g_root_dir, t->taskid);
                
                int fd_trunc;
                fd_trunc = open(p_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd_trunc != -1) close(fd_trunc);
                fd_trunc = open(p_err, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd_trunc != -1) close(fd_trunc);


                int exit_code = execute_command_in_terminal(t->command, t->taskid);
                log_execution(t->taskid, exit_code);
            }
        }


        time_t now = time(NULL);
        sleep(60 - (now % 60));
    }


    for (size_t i = 0; i < task_count; i++) {
        free_command(all_tasks[i].command);
    }
    free(all_tasks);

    return 0;
}