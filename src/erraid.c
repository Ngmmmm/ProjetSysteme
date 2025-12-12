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
#include <sys/wait.h>
#include <time.h>
#include <endian.h>
#include <getopt.h> 
#include <ctype.h>
#include <sys/select.h>

// Variable globale pour la racine 
char g_root_dir[MAX_PATH_LEN];
int g_request_fd;

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


int execute_command_in_terminal(struct command *cmd, uint64_t taskid);

/**
 * Exécute les sous-commandes d'une séquence l'une après l'autre.
 * S'interrompt immédiatement si une sous-commande échoue.
 * @param cmd La commande séquence (SQ).
 * @param taskid Identifiant de la tâche (pour les logs).
 * @return 0 si tout réussit, ou le code de retour de la commande qui ne fonctionne pas.
 */
int execute_sequence_command_in_terminal(struct command *cmd, uint64_t taskid) {
    int last_exit_code = 0;
    if (cmd == NULL || cmd->subcount == 0) return 0;

    for (size_t i = 0; i < cmd->subcount; i++) {
        last_exit_code = execute_command_in_terminal(&cmd->subcommands[i], taskid);
        if (last_exit_code != 0) break;
    }
    return last_exit_code;
}

/**
 * Exécute une commande simple (SI) et attend son achèvement.
 * @param cmd La structure de commande à exécuter (doit être de type "SI").
 * @param taskid ajout de taskid pour les logs
 * @return Le code de sortie de la commande 0, ou -1 en cas d'échec de fork.
 */
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

/**
 * Fonction pour exécuter n'importe quel type de commande.
 * La sortie et l'erreur sont maintenant redirigées vers les fichiers.
 * @param cmd La structure de commande (SI ou SQ).
 * @param taskid ajout de taskid
 * @return Le code de sortie final, ou -1 en cas d'erreur.
 */
int execute_command_in_terminal(struct command *cmd, uint64_t taskid) { 
    if (!cmd) return -1;
    if (strcmp(cmd->type, "SI") == 0) return execute_simple_command_in_terminal(cmd, taskid);
    if (strcmp(cmd->type, "SQ") == 0) return execute_sequence_command_in_terminal(cmd, taskid);
    return -1;
}

/**
 * Vérifie si une tâche doit être exécutée maintenant.
 * @param t La tâche à vérifier.
 * @param now L'heure actuelle.
 * @return 1 si c'est l'heure, 0 sinon.
 */
int is_time_to_run(struct task *t, struct tm *now) {
    uint64_t min_bit = (uint64_t)1 << now->tm_min;
    uint32_t hour_bit = (uint32_t)1 << now->tm_hour;
    uint8_t  day_bit  = (uint8_t)1  << now->tm_wday;

    if ((t->timing.minutes & min_bit) == 0) return 0;
    if ((t->timing.hours & hour_bit) == 0) return 0;
    if ((t->timing.daysofweek & day_bit) == 0) return 0;
    return 1;
}

/**
 * Enregistre le timestamp actuel et le code de sortie dans le journal de la tâche.
 * Ajoute une entrée binaire au fichier 'times-exitcodes'.
 * @param taskid Identifiant de la tâche.
 * @param exit_code Code de retour (enregistré comme 0xFFFF si -1).
 */
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

void init_communication() {
    char pipes_dir[MAX_PATH_LEN];
    char req_pipe[MAX_PATH_LEN];
    char rep_pipe[MAX_PATH_LEN];

    // Construction des chemins
    snprintf(pipes_dir, sizeof(pipes_dir), "%s/pipes", g_root_dir);
    snprintf(req_pipe, sizeof(req_pipe), "%s/erraid-request-pipe", pipes_dir);
    snprintf(rep_pipe, sizeof(rep_pipe), "%s/erraid-reply-pipe", pipes_dir);

    // Création du répertoire pipes s'il n'existe pas
    if (mkdir(pipes_dir, 0755) == -1 && errno != EEXIST) {// 0755 = rwxr-xr-x
        write_error("Erreur fatale: Impossible de créer le répertoire pipes");
        exit(EXIT_FAILURE);
    }

    // Création du tube de requête
    if (mkfifo(req_pipe, 0644) == -1 && errno != EEXIST) {
        write_error("Erreur fatale: Impossible de créer erraid-request-pipe");
        exit(EXIT_FAILURE);
    }

    // Création du tube de réponse
    if (mkfifo(rep_pipe, 0644) == -1 && errno != EEXIST) {
        write_error("Erreur fatale: Impossible de créer erraid-reply-pipe");
        exit(EXIT_FAILURE);
    }

    // Ouverture du tube de requête en LECTURE
    g_request_fd = open(req_pipe, O_RDWR);
    
    if (g_request_fd == -1) {
        write_error("Erreur fatale: Impossible d'ouvrir erraid-request-pipe");
        exit(EXIT_FAILURE);
    }

    write_line("Canaux de communication (pipes) initialisés.");
}


void handle_client_request(int req_fd, struct task *tasks, size_t task_count) {

    uint16_t opcode_be;
    if (read(req_fd, &opcode_be, 2) <= 0) return;
    uint16_t opcode = be16toh(opcode_be);

    char rep_path[MAX_PATH_LEN];
    snprintf(rep_path, MAX_PATH_LEN, "%s/pipes/erraid-reply-pipe", g_root_dir);
    int rep_fd = open(rep_path, O_WRONLY);
    if (rep_fd == -1) {
        // On vide les arguments potentiels si client envoie plus que l'opcode
        if (opcode != OPCODE_LIST && opcode != OPCODE_TERMINATE) {
            uint64_t trash; read(req_fd, &trash, 8);
        }
        return; 
    }

    uint16_t ans_ok = htobe16(ANSTYPE_OK);
    uint16_t ans_err = htobe16(ANSTYPE_ERR);
    uint16_t err_nf = htobe16(ERRCODE_NOT_FOUND);
    uint16_t err_nr = htobe16(ERRCODE_NO_RUN);


    // --- LIST (-l) ---
    if (opcode == OPCODE_LIST) {
        write(rep_fd, &ans_ok, 2);
        uint32_t nb = htobe32((uint32_t)task_count);
        write(rep_fd, &nb, 4);

        for (size_t i = 0; i < task_count; i++) {
            uint64_t id = htobe64(tasks[i].taskid);
            uint64_t min = htobe64(tasks[i].timing.minutes);
            uint32_t hour = htobe32(tasks[i].timing.hours);
            
            write(rep_fd, &id, 8);
            write(rep_fd, &min, 8);
            write(rep_fd, &hour, 4);
            write(rep_fd, &tasks[i].timing.daysofweek, 1);

            char cmd_str[2048] = "";
            serialize_command_to_buffer(tasks[i].command, cmd_str, sizeof(cmd_str));
            uint32_t len = htobe32(strlen(cmd_str));
            write(rep_fd, &len, 4);
            write(rep_fd, cmd_str, strlen(cmd_str));
        }
    } 

    //a continuer 





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
    snprintf(buffer, sizeof(buffer), " Démon erraid démarré sur : %s ", g_root_dir);
    write_line(buffer);

    init_communication();

    //  Chargement
    size_t task_count = 0;
    struct task *all_tasks = load_all_tasks(&task_count);

    if (all_tasks == NULL && task_count == 0) {
        write_error("Aucune tâche chargée ou erreur fatale.");

    }else {
        snprintf(buffer, sizeof(buffer), "%zu tâches chargées.", task_count);
        write_line(buffer);
    }

    
    // Synchronisation initiale sur la minute
    time_t start_now = time(NULL);
    int seconds_to_wait = 60 - (start_now % 60);
    if (seconds_to_wait == 0) seconds_to_wait = 60; // Sécurités
    write_line("Synchronisation horloge...");
    sleep(seconds_to_wait);


    // Initialisation pour éviter la double exécution
    int last_executed_min = -1;

    while (1) {
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);

        // Exécution des tâches (seulement si nouvelle minute)
        if (tm_now->tm_min != last_executed_min) {
            last_executed_min = tm_now->tm_min; // On marque la minute comme faite

            for (size_t i = 0; i < task_count; i++) {
                if (is_time_to_run(&all_tasks[i], tm_now)) {
                    struct task *t = &all_tasks[i];

                    snprintf(buffer, sizeof(buffer), "Exécution de la tâche ID %lu...", t->taskid);
                    write_line(buffer);

                    // Chemins logs
                    char p_out[MAX_PATH_LEN], p_err[MAX_PATH_LEN];
                    snprintf(p_out, MAX_PATH_LEN, "%s/tasks/%lu/stdout", g_root_dir, t->taskid);
                    snprintf(p_err, MAX_PATH_LEN, "%s/tasks/%lu/stderr", g_root_dir, t->taskid);
                    
                    // Truncate logs
                    close(open(p_out, O_WRONLY | O_CREAT | O_TRUNC, 0644));
                    close(open(p_err, O_WRONLY | O_CREAT | O_TRUNC, 0644));

                    int exit_code = execute_command_in_terminal(t->command, t->taskid);
                    log_execution(t->taskid, exit_code);
                }

            }
        }

        // Calcul de l'attente
        // On vise le début de la minute suivante
        now = time(NULL);
        tm_now = localtime(&now);
        int seconds_to_wait = 60 - tm_now->tm_sec;
        
        // Si on est à 59s, on attend 1s. Si on est à 0s, on attend 60s.
        // Sécurité : ne jamais attendre 0 ou négatif
        if (seconds_to_wait <= 0) seconds_to_wait = 1;

        // Attente active (Select)
        struct timeval timeout;
        timeout.tv_sec = seconds_to_wait;
        timeout.tv_usec = 0;
        
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(g_request_fd, &readfds);

        int ret = select(g_request_fd + 1, &readfds, NULL, NULL, &timeout);

        if (ret > 0 && FD_ISSET(g_request_fd, &readfds)) {
            // Traitement requête
            handle_client_request(g_request_fd, all_tasks, task_count);
        }
    }

    // Nettoyage
    for (size_t i = 0; i < task_count; i++) {
        free_command(all_tasks[i].command);
    }
    free(all_tasks);
    if (g_request_fd != -1) close(g_request_fd);

    return 0;
}