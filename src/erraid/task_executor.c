#include "../include/erraid.h"
#include "../include/utils.h"
#include "../include/globals.h"
#include "../include/task_executor.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <endian.h>
#include <sys/stat.h>




void sigchld_handler(int signum) {
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

// --- Prototypes des fonctions helpers statiques ---
static int execute_pipeline(struct command *cmd, uint64_t taskid);
static int execute_conditional(struct command *cmd, uint64_t taskid);

/**
 * Exécute une commande pipeline (PL).
 * Connecte la sortie de chaque commande à l'entrée de la suivante.
 */
static int execute_pipeline(struct command *cmd, uint64_t taskid) {
    if (!cmd || cmd->subcount == 0) return 0;
    
    int prev_pipe_read = -1; // Le descripteur de lecture du pipe précédent
    int pipe_fds[2];
    pid_t last_pid = -1;
    
    for (size_t i = 0; i < cmd->subcount; i++) {
        // On crée un pipe si ce n'est pas la dernière commande
        if (i < cmd->subcount - 1) {
            if (pipe(pipe_fds) == -1) {
                perror("pipe");
                return -1;
            }
        }
        
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            return -1;
        }

        if (pid == 0) {
            // --- FILS ---
            
            // Redirection de l'entrée (Stdin) depuis le pipe précédent
            if (prev_pipe_read != -1) {
                if (dup2(prev_pipe_read, STDIN_FILENO) == -1) {
                    perror("dup2 stdin");
                    exit(1);
                }
                close(prev_pipe_read);
            }
            
            // Redirection de la sortie (Stdout) vers le pipe actuel
            // (Sauf pour la dernière commande, qui écrira vers la sortie par défaut : le fichier log)
            if (i < cmd->subcount - 1) {
                close(pipe_fds[0]);
                if (dup2(pipe_fds[1], STDOUT_FILENO) == -1) {
                    perror("dup2 stdout");
                    exit(1);
                }
                close(pipe_fds[1]);
                setenv("ERRAID_NO_LOG", "1", 1);
            }
            
            // Exécution de la sous-commande
            int ret = execute_command_in_terminal(&cmd->subcommands[i], taskid);
            exit(ret); 

        } 
        
        // --- PÈRE ---
        last_pid = pid;
        
        // Fermeture du pipe précédent dont on n'a plus besoin
        if (prev_pipe_read != -1) {
            close(prev_pipe_read);
        }
        
        // Préparation pour le tour suivant
        if (i < cmd->subcount - 1) {
            close(pipe_fds[1]); // Le père n'écrit pas dans le pipe
            prev_pipe_read = pipe_fds[0]; // On garde le bout lecture pour le prochain fils
        }
    }
    
    // Attente de la fin de tous les processus du pipeline
    int status;
    int last_exit_code = -1;
    
    // On attend explicitement le dernier pour récupérer son code de retour (c'est celui du pipeline)
    if (last_pid != -1) {
        waitpid(last_pid, &status, 0);
        if (WIFEXITED(status)) last_exit_code = WEXITSTATUS(status);
        else last_exit_code = 128 + WTERMSIG(status);
    }
    
    // On nettoie les éventuels autres fils zombies
    while (wait(NULL) > 0);
    
    return last_exit_code;
}

/**
 * Exécute une commande conditionnelle (IF).
 * Sub 0 : Condition
 * Sub 1 : Then (si Condition == 0)
 * Sub 2 : Else (si Condition != 0) (Optionnel)
 */
static int execute_conditional(struct command *cmd, uint64_t taskid) {
    if (cmd->subcount < 2) return -1;
    
    // Exécution de la condition
    int cond_ret = execute_command_in_terminal(&cmd->subcommands[0], taskid);
    
    if (cond_ret == 0) {
        // Branche THEN
        return execute_command_in_terminal(&cmd->subcommands[1], taskid);
    } else {
        // Branche ELSE 
        if (cmd->subcount > 2) {
            return execute_command_in_terminal(&cmd->subcommands[2], taskid);
        }
        // Sinon, succès par défaut (convention shell)
        return 0;
    }
}


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
        // --- Fils (exécution finale) ---
        char stdout_path[MAX_PATH_LEN], stderr_path[MAX_PATH_LEN];
        
        snprintf(stdout_path, MAX_PATH_LEN, "%s/tasks/%lu/stdout", g_root_dir, taskid);
        snprintf(stderr_path, MAX_PATH_LEN, "%s/tasks/%lu/stderr", g_root_dir, taskid);

        // Gestion STDERR : Toujours vers le fichier log de la tâche
        int fd_err = open(stderr_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd_err != -1) { 
            dup2(fd_err, STDERR_FILENO); 
            close(fd_err); 
        }

        // Gestion STDOUT : Subtilité pour les pipelines
        const char *no_log = getenv("ERRAID_NO_LOG");
        int skip_log = (no_log && strcmp(no_log, "1") == 0);

        if (!skip_log) {
            // Pas de pipe interne signalé -> On logue dans le fichier
            int fd_out = open(stdout_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd_out != -1) { 
                dup2(fd_out, STDOUT_FILENO); 
                close(fd_out); 
            }
        }

        execvp(cmd->argv[0], cmd->argv);
        
        // Si execvp échoue
        fprintf(stderr, "Erreur execvp '%s': %s\n", cmd->argv[0], strerror(errno));
        exit(127);
    } else {
        // --- Père ---
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
    if (strcmp(cmd->type, "PL") == 0) return execute_pipeline(cmd, taskid);
    if (strcmp(cmd->type, "IF") == 0) return execute_conditional(cmd, taskid);
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
