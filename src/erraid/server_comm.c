#include "../include/erraid.h"
#include "../include/utils.h"
#include "../include/globals.h"
#include "../include/server_comm.h"
#include "../include/task_loader.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <endian.h>


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


void handle_client_request(int req_fd, struct task **tasks, size_t *task_count) {

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
        uint32_t nb = htobe32((uint32_t)*task_count);
        write(rep_fd, &nb, 4);

        for (size_t i = 0; i < *task_count; i++) {
            // ID
            uint64_t id = htobe64((*tasks)[i].taskid);
            write(rep_fd, &id, 8);

            // Timing
            uint64_t min = htobe64((*tasks)[i].timing.minutes);
            uint32_t hour = htobe32((*tasks)[i].timing.hours);
            
            write(rep_fd, &min, 8);
            write(rep_fd, &hour, 4);
            write(rep_fd, &(*tasks)[i].timing.daysofweek, 1);
            write_command_to_fd(rep_fd, (*tasks)[i].command);
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
    for (size_t i = 0; i < *task_count; i++) {
        if ((*tasks)[i].taskid == target_id) {
            target_task = &(*tasks)[i];
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
    //int res = -1;

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