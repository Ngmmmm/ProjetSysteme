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




void sigchld_handler(int signum) {
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
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
