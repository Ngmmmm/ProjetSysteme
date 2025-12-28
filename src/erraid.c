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
#include <signal.h>


void sigchld_handler(int signum) {
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}



// Variable globale pour la racine 
char g_root_dir[MAX_PATH_LEN];
int g_request_fd;



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

/**
 * Fonction qui permet d'ecrire la liste des taches serialisées
 */
void write_command_to_fd(int fd, struct command *cmd) {
    if (!cmd) return;

    if (strcmp(cmd->type, "SI") == 0) {
        write(fd, "SI", 2);
        uint32_t argc_be = htobe32((uint32_t)cmd->argc);
        write(fd, &argc_be, sizeof(argc_be));

        for (int i = 0; i < cmd->argc; i++) {
            char *arg = cmd->argv[i];
            uint32_t len = strlen(arg);
            uint32_t len_be = htobe32(len);
            write(fd, &len_be, sizeof(len_be));
            write(fd, arg, len);
        }

    } else if (strcmp(cmd->type, "SQ") == 0) {
        write(fd, "SQ", 2);
        uint32_t sub_be = htobe32((uint32_t)cmd->subcount);
        write(fd, &sub_be, sizeof(sub_be));

        for (size_t i = 0; i < cmd->subcount; i++) {
            write_command_to_fd(fd, &cmd->subcommands[i]);
        }
    }
}

int send_file_content(int rep_fd, const char *filepath, int with_header_len) {
    int file_fd = open(filepath, O_RDONLY);
    if (file_fd == -1) return -1;

    // Récupération de la taille
    struct stat st;
    if (fstat(file_fd, &st) == -1) { close(file_fd); return -1; }
    uint32_t size = (uint32_t)st.st_size;

    // Si le protocole demande d'envoyer la taille avant (pour stdout/stderr)
    if (with_header_len) {
        uint32_t size_be = htobe32(size);
        write(rep_fd, &size_be, 4);
    }

    // Copie du contenu
    char buf[1024];
    ssize_t n;
    while ((n = read(file_fd, buf, sizeof(buf))) > 0) {
        write(rep_fd, buf, n);
    }
    
    close(file_fd);
    return 0;
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
        
        // Nombre de tâches
        uint32_t nb = htobe32((uint32_t)task_count);
        write(rep_fd, &nb, 4);

        for (size_t i = 0; i < task_count; i++) {
            // ID
            uint64_t id = htobe64(tasks[i].taskid);
            write(rep_fd, &id, 8);

            // Timing
            uint64_t min = htobe64(tasks[i].timing.minutes);
            uint32_t hour = htobe32(tasks[i].timing.hours);
            
            write(rep_fd, &min, 8);
            write(rep_fd, &hour, 4);
            write(rep_fd, &tasks[i].timing.daysofweek, 1);
            write_command_to_fd(rep_fd, tasks[i].command);
        }
        
        close(rep_fd);
        return;
    } 

    if (opcode == OPCODE_TERMINATE) {
        write(rep_fd, &ans_ok, 2);
        close(rep_fd);
        exit(0);
    }

    // COMMANDES CIBLÉES (-x, -o, -e)
    uint64_t taskid_be;
    if (read(req_fd, &taskid_be, 8) != 8) {
        close(rep_fd);
        return;
    }
    uint64_t target_id = be64toh(taskid_be);

    //Recherche de la tâche (Pour erreur ERNF)
    struct task *target_task = NULL;
    for (size_t i = 0; i < task_count; i++) {
        if (tasks[i].taskid == target_id) {
            target_task = &tasks[i];
            break;
        }
    }

    if (!target_task) {
        // Tâche non trouvée -> ERNF
        write(rep_fd, &ans_err, 2);
        write(rep_fd, &err_nf, 2);
        close(rep_fd);
        return;
    }

    //Traitement selon l'opcode
    char file_path[MAX_PATH_LEN];
    int res = -1;

    if (opcode == OPCODE_TIMES_EXITCODES) { // -x
        snprintf(file_path, MAX_PATH_LEN, "%s/tasks/%lu/times-exitcodes", g_root_dir, target_id);
        
        int fd_log = open(file_path, O_RDONLY);
        if (fd_log == -1) {
            // Pas de fichier log -> ERNR
            write(rep_fd, &ans_err, 2);
            write(rep_fd, &err_nr, 2);
        } else {
            write(rep_fd, &ans_ok, 2);
            
            // Calcul du nombre de runs
            struct stat st;
            fstat(fd_log, &st);
            uint32_t runs = (uint32_t)(st.st_size / 10);
            uint32_t runs_be = htobe32(runs);
            
            write(rep_fd, &runs_be, 4);
            
            // Copie contenu
            char buf[1024];
            ssize_t n;
            while ((n = read(fd_log, buf, sizeof(buf))) > 0) write(rep_fd, buf, n);
            close(fd_log);
        }
    } 
    else if (opcode == OPCODE_STDOUT || opcode == OPCODE_STDERR) { // -o ou -e
        const char *filename = (opcode == OPCODE_STDOUT) ? "stdout" : "stderr";
        snprintf(file_path, MAX_PATH_LEN, "%s/tasks/%lu/%s", g_root_dir, target_id, filename);

        // On essaie d'ouvrir pour voir si le fichier existe
        if (access(file_path, F_OK) == -1) {
             // Fichier inexistant -> ERNR
            write(rep_fd, &ans_err, 2);
            write(rep_fd, &err_nr, 2);
        } else {
            write(rep_fd, &ans_ok, 2);
            // Envoi Contenu (avec header taille = 1)
            send_file_content(rep_fd, file_path, 1); 
        }
    } 
    else {
        // Opcode inconnu
        write(rep_fd, &ans_err, 2);
        uint16_t zero = 0;
        write(rep_fd, &zero, 2);
    }

    close(rep_fd);
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


    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

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
   time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    
    int last_min = tm_now->tm_min;

    if (tm_now->tm_sec == 0) {
        last_min = -1;
    }

    while (1) {
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);

        // Exécution des tâches (seulement si nouvelle minute)
        if (tm_now->tm_min != last_min) {
            last_min = tm_now->tm_min; // On marque la minute comme faite

            for (size_t i = 0; i < task_count; i++) {
                if (is_time_to_run(&all_tasks[i], tm_now)) {
                    struct task *t = &all_tasks[i];

                    snprintf(buffer, sizeof(buffer), "Exécution de la tâche ID %lu...", t->taskid);
                    write_line(buffer);

                    // Exécution asynchrone
                    pid_t pid = fork();
                    if (pid == 0) {
                        // Processus Fils "Task Runner"

                        struct sigaction sa_dfl;
                        sa_dfl.sa_handler = SIG_DFL;
                        sigemptyset(&sa_dfl.sa_mask);
                        sa_dfl.sa_flags = 0;
                        sigaction(SIGCHLD, &sa_dfl, NULL);

                        // Troncature des fichiers de logs ici (dans le fils uniquement)
                        char p_out[MAX_PATH_LEN], p_err[MAX_PATH_LEN];
                        snprintf(p_out, MAX_PATH_LEN, "%s/tasks/%lu/stdout", g_root_dir, t->taskid);
                        snprintf(p_err, MAX_PATH_LEN, "%s/tasks/%lu/stderr", g_root_dir, t->taskid);
                        
                        int fd = open(p_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fd != -1) close(fd);
                        fd = open(p_err, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fd != -1) close(fd);

                        // Exécution et Log
                        int exit_code = execute_command_in_terminal(t->command, t->taskid);
                        log_execution(t->taskid, exit_code);
                        
                        exit(0); // Le fils termine ici
                    }
                }

            }
        }

        // Calcul de l'attente
        // On vise le début de la minute suivante
        now = time(NULL);
        tm_now = localtime(&now);
        int seconds_to_wait = 60 - tm_now->tm_sec;


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