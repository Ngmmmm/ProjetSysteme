#include "../include/erraid.h"
#include "../include/utils.h"
#include "../include/globals.h"      // Globales
#include "../include/task_loader.h"  // Chargement
#include "../include/task_executor.h"// Exécution
#include "../include/server_comm.h"  // Communication

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <sys/select.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>

void stop_handler(int sig) {
    (void)sig;
    g_running = 0;
}

void daemonize() {
    pid_t pid;

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) exit(EXIT_FAILURE);

    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0);
    chdir("/");

    int x;
    for (x = sysconf(_SC_OPEN_MAX); x >= 0; x--) {
        close(x);
    }
    
    int fd0 = open("/dev/null", O_RDWR); 
    int fd1 = dup(0);                    
    int fd2 = dup(0);                   
    (void)fd0; (void)fd1; (void)fd2;
}



int main(int argc, char *argv[]) {
    // Initialisation du chemin racine par défaut
    const char *user = getenv("USER");
    if (!user) user = "unknown";
    snprintf(g_root_dir, sizeof(g_root_dir), "/tmp/%s/erraid", user);

    // Gestion de l'option -r (pour les tests)
    int foreground = 0;
    int opt;
    while ((opt = getopt(argc, argv, "Fr:")) != -1) {
        switch (opt) {
            case 'F':
                foreground = 1;
                break;
            case 'r':
                strncpy(g_root_dir, optarg, sizeof(g_root_dir) - 1);
                g_root_dir[sizeof(g_root_dir) - 1] = '\0';
                break;
        }
    }
    if (!foreground) {
        daemonize();
    }


    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    struct sigaction sa_stop;
    sa_stop.sa_handler = stop_handler;
    sigemptyset(&sa_stop.sa_mask);
    sa_stop.sa_flags = 0;
    sigaction(SIGINT, &sa_stop, NULL);  
    sigaction(SIGTERM, &sa_stop, NULL);

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

    while (g_running) {
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
                        
                        chdir(g_root_dir);

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
                        
                        exit(0);
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
            handle_client_request(g_request_fd, &all_tasks, &task_count);
        }
    }

    // Nettoyage
    for (size_t i = 0; i < task_count; i++) {
        free_command(all_tasks[i].command);
    }
    free(all_tasks);
    if (g_request_fd != -1) close(g_request_fd);

    char pipe_path[MAX_PATH_LEN];
    snprintf(pipe_path, MAX_PATH_LEN, "%s/pipes/erraid-request-pipe", g_root_dir);
    unlink(pipe_path);
    snprintf(pipe_path, MAX_PATH_LEN, "%s/pipes/erraid-reply-pipe", g_root_dir);
    unlink(pipe_path);

    if (foreground) write_line("Arrêt propre du démon.");

    return 0;
}