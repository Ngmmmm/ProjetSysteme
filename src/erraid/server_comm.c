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
    char tasks_dir[MAX_PATH_LEN];
    char req_pipe[MAX_PATH_LEN];
    char rep_pipe[MAX_PATH_LEN];

    // Construction des chemins
    snprintf(pipes_dir, sizeof(pipes_dir), "%s/pipes", g_root_dir);
    snprintf(tasks_dir, sizeof(tasks_dir), "%s/tasks", g_root_dir);
    snprintf(req_pipe, sizeof(req_pipe), "%s/erraid-request-pipe", pipes_dir);
    snprintf(rep_pipe, sizeof(rep_pipe), "%s/erraid-reply-pipe", pipes_dir);


    if (mkdir(g_root_dir, 0755) == -1 && errno != EEXIST) {
        // Si on ne peut pas loguer sur stderr (car démon), on quitte juste
        exit(EXIT_FAILURE);
    }
    
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

    // Création du répertoire tasks
    if (mkdir(tasks_dir, 0755) == -1 && errno != EEXIST) {
        write_error("Erreur fatale: Impossible de créer le répertoire tasks");
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

    // Commande simple SI
    if (strcmp(cmd->type, "SI") == 0) {
        write(fd, "SI", 2);
        // Écriture du nombre d’arguments (big-endian)
        uint32_t argc_be = htobe32((uint32_t)cmd->argc);
        write(fd, &argc_be, sizeof(argc_be));

        // Écriture de chaque argument : longueur + contenu
        for (int i = 0; i < cmd->argc; i++) {
            char *arg = cmd->argv[i];
            uint32_t len = strlen(arg);
            uint32_t len_be = htobe32(len);
            write(fd, &len_be, sizeof(len_be));
            write(fd, arg, len);
        }

    // Séquence de commandes SQ
    } else if (strcmp(cmd->type, "SQ") == 0) {
        write(fd, "SQ", 2);
        // Écriture du nombre de sous-commandes (big-endian)
        uint32_t sub_be = htobe32((uint32_t)cmd->subcount);
        write(fd, &sub_be, sizeof(sub_be));

        // Sérialisation récursive de chaque sous-commande
        for (size_t i = 0; i < cmd->subcount; i++) {
            write_command_to_fd(fd, &cmd->subcommands[i]);
        }
    }
}


void handle_client_request(int req_fd, struct task **tasks_ptr, size_t *count_ptr) {

    struct task *tasks = *tasks_ptr; 
    size_t task_count = *count_ptr;

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

    // CREATE (-c) 
    if (opcode == OPCODE_CREATE) {
        //Lecture du Timing
        struct timing t;
        uint64_t m_be; uint32_t h_be; 
        read(req_fd, &m_be, 8);
        read(req_fd, &h_be, 4);
        read(req_fd, &t.daysofweek, 1);
        t.minutes = be64toh(m_be);
        t.hours = be32toh(h_be);

        //Lecture de la Commande
        struct command *new_cmd = malloc(sizeof(struct command));
        memset(new_cmd, 0, sizeof(struct command));
        strcpy(new_cmd->type, "SI");

        uint32_t argc_be;
        read(req_fd, &argc_be, 4);
        new_cmd->argc = be32toh(argc_be);
        
        new_cmd->argv = malloc((new_cmd->argc + 1) * sizeof(char*));
        for(int i=0; i<new_cmd->argc; i++) {
            new_cmd->argv[i] = read_string_from_fd(req_fd); 
        }
        new_cmd->argv[new_cmd->argc] = NULL;

        //Calcul du nouvel ID
        uint64_t max_id = 0;
        for(size_t i=0; i<task_count; i++) {
            if(tasks[i].taskid > max_id) max_id = tasks[i].taskid;
        }
        uint64_t new_id = max_id + 1;

        //Construction de la tâche
        struct task new_task;
        new_task.taskid = new_id;
        new_task.timing = t;
        new_task.command = new_cmd;

        //Sauvegarde et Mise à jour mémoire
        if (save_task_to_disk(&new_task) == 0) {
            size_t new_count = task_count + 1;
            struct task *new_array = realloc(tasks, new_count * sizeof(struct task));
            
            if (new_array) {
                new_array[task_count] = new_task;
                *tasks_ptr = new_array;
                *count_ptr = new_count;
                
                write(rep_fd, &ans_ok, 2);
                uint64_t id_be = htobe64(new_id);
                write(rep_fd, &id_be, 8);
            } else {
                write(rep_fd, &ans_err, 2);
                free_command(new_cmd); 
            }
        } else {
             write(rep_fd, &ans_err, 2);
             free_command(new_cmd);
        }
        close(rep_fd);
        return;
    }

    // REMOVE (-r)
    if (opcode == OPCODE_REMOVE) {
        uint64_t id_be;
        if (read(req_fd, &id_be, 8) != 8) { close(rep_fd); return; }
        uint64_t target_id = be64toh(id_be);

        int found_idx = -1;
        for(size_t i=0; i<task_count; i++) {
            if(tasks[i].taskid == target_id) {
                found_idx = i;
                break;
            }
        }

        if (found_idx != -1) {
            delete_task_from_disk(target_id);
            free_command(tasks[found_idx].command);

            if (found_idx < task_count - 1) {
                memmove(&tasks[found_idx], &tasks[found_idx+1], 
                        sizeof(struct task) * (task_count - 1 - found_idx));
            }

            *count_ptr = task_count - 1;
            write(rep_fd, &ans_ok, 2);
        } else {
            write(rep_fd, &ans_err, 2); 
            write(rep_fd, &err_nf, 2);
        }
        close(rep_fd);
        return;
    }


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

    // COMBINE (-s, -p, -i) : Fusion de tâches
    if (opcode == OPCODE_COMBINE) {
        uint16_t type_be;
        uint64_t id1_be, id2_be;

        // Lecture des paramètres envoyés par le client (Type + 2 IDs sources)
        if (read(req_fd, &type_be, 2) != 2) { close(rep_fd); return; }
        if (read(req_fd, &id1_be, 8) != 8) { close(rep_fd); return; }
        if (read(req_fd, &id2_be, 8) != 8) { close(rep_fd); return; }
        
        uint64_t id1 = be64toh(id1_be);
        uint64_t id2 = be64toh(id2_be);

        //On s'assure que les IDs existent
        int found1 = 0, found2 = 0;
        for (size_t i = 0; i < task_count; i++) {
            if (tasks[i].taskid == id1) found1 = 1;
            if (tasks[i].taskid == id2) found2 = 1;
        }

        if (!found1 || !found2) {
             write(rep_fd, &ans_err, 2);
             write(rep_fd, &err_nf, 2); // Code Not Found
             close(rep_fd);
             return;
        }

        // Création structure : Nouvel ID et dossiers
        uint64_t max_id = 0;
        for(size_t i=0; i < task_count; i++) {
            if(tasks[i].taskid > max_id) max_id = tasks[i].taskid;
        }
        uint64_t new_id = max_id + 1;

        char new_task_dir[MAX_PATH_LEN];
        char new_cmd_dir[MAX_PATH_LEN];
        // Construction des chemins absolus
        snprintf(new_task_dir, MAX_PATH_LEN, "%s/tasks/%lu", g_root_dir, new_id);
        snprintf(new_cmd_dir, MAX_PATH_LEN, "%s/cmd", new_task_dir);
        
        // Création des répertoires
        if (mkdir(new_task_dir, 0755) == -1 || mkdir(new_cmd_dir, 0755) == -1) {
            write(rep_fd, &ans_err, 2);
            close(rep_fd);
            return;
        }

        // Création du fichier 'type' pour la nouvelle commande (SQ, PL ou IF)
        char path[MAX_PATH_LEN];
        snprintf(path, MAX_PATH_LEN, "%s/type", new_cmd_dir);
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd != -1) {
            write(fd, &type_be, 2); // Écrit le type
            close(fd);
        }

        // Création d'un fichier 'timing' par défaut (tout à 1 = exécution tout le temps)
        // Nécessaire pour que load_all_tasks puisse charger la tâche.
        snprintf(path, MAX_PATH_LEN, "%s/timing", new_task_dir);
        fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd != -1) {
            uint64_t min = 0xFFFFFFFFFFFFFFFF;
            uint32_t hour = 0xFFFFFFFF;
            uint8_t day = 0xFF;
            write(fd, &min, 8);
            write(fd, &hour, 4);
            write(fd, &day, 1);
            close(fd);
        }

        // Déplacement : Rename tasks/Y/cmd -> tasks/X/cmd/index
        char src_path[MAX_PATH_LEN];
        char dst_path[MAX_PATH_LEN];

        // Déplacement de la tâche source 1 vers l'index 0
        snprintf(src_path, MAX_PATH_LEN, "%s/tasks/%lu/cmd", g_root_dir, id1);
        snprintf(dst_path, MAX_PATH_LEN, "%s/0", new_cmd_dir);
        rename(src_path, dst_path);

        // Déplacement de la tâche source 2 vers l'index 1
        snprintf(src_path, MAX_PATH_LEN, "%s/tasks/%lu/cmd", g_root_dir, id2);
        snprintf(dst_path, MAX_PATH_LEN, "%s/1", new_cmd_dir);
        rename(src_path, dst_path);

        // Suppression des anciennes tâches
        // Comme le dossier 'cmd' a été déplacé, delete_task_from_disk supprimera
        // le dossier racine (tasks/ID) et les fichiers restants (timing, stdout...).
        delete_task_from_disk(id1);
        delete_task_from_disk(id2);

        // Libération de la mémoire actuelle
        for (size_t i = 0; i < task_count; i++) {
            free_command(tasks[i].command);
        }
        free(tasks);

        // met à jour le pointeur du main
        *tasks_ptr = load_all_tasks(count_ptr);

        // Réponse OK avec le nouvel ID
        write(rep_fd, &ans_ok, 2);
        uint64_t new_id_be = htobe64(new_id);
        write(rep_fd, &new_id_be, 8);
        
        close(rep_fd);
        return;
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