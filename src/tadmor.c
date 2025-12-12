#include "../include/erraid.h"
#include "../include/utils.h"


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <endian.h>
#include <time.h>




// Codes des opérations possibles (Opcode)
typedef enum {
    ACTION_NONE = 0,
    ACTION_LIST,            // -l
    ACTION_EXIT_CODE,       // -x
    ACTION_STDOUT,          // -o
    ACTION_STDERR           // -e
} ActionType;

/**
 * Affiche l'aide si l'utilisateur se trompe.
 */
void print_usage(const char *prog_name) {
    write_error("Usage: "); write_error(prog_name); write_error(" [options]\n");
    write_error("Options:\n");
    write_error("  -l          Lister les tâches.\n");
    write_error("  -x <id>     Lister les codes de retour de la tâche <id>.\n");
    write_error("  -o <id>     Afficher stdout de la tâche <id>.\n");
    write_error("  -e <id>     Afficher stderr de la tâche <id>.\n");
    write_error("  -p <dir>    Répertoire des tubes nommés.\n");
}


void deserialize_and_print_cmd(int fd, int is_root);



/**
 * Envoie une requête simple au démon via le tube nommé.
 * Gère l'ouverture, l'écriture et la fermeture du tube.
 */
int send_request(const char *pipes_dir, uint16_t opcode, uint64_t taskid) {
    char req_pipe_path[MAX_PATH_LEN];
    snprintf(req_pipe_path, sizeof(req_pipe_path), "%s/erraid-request-pipe", pipes_dir);

    //Ouverture du tube en écriture
    int fd = open(req_pipe_path, O_WRONLY);
    if (fd == -1) {
        write_error("Erreur: Impossible d'ouvrir le tube de requête ");
        return -1;
    }

    //  Préparation du buffer
    // La taille max est OPCODE (2) + TASKID (8) = 10 octets
    uint8_t buffer[10];
    int msg_len = 0;

    // Écriture de l'OPCODE (toujours présent)
    uint16_t op_be = htobe16(opcode);  // Convertit l'opcode en big-endian
    memcpy(buffer, &op_be, sizeof(uint16_t));
    msg_len += sizeof(uint16_t);

    // Écriture du TASKID (si nécessaire)
    // Pour LIST, on n'envoie que l'opcode. Pour les autres, on ajoute l'ID.
    if (opcode != OPCODE_LIST && opcode != OPCODE_TERMINATE) {
        uint64_t id_be = htobe64(taskid);
        memcpy(buffer + msg_len, &id_be, sizeof(uint64_t));
        msg_len += sizeof(uint64_t);
    }

    // Envoi
    if (write(fd, buffer, msg_len) == -1) {
        write_error("Erreur: Échec de l'envoi de la requête");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

ssize_t read_full(int fd, void *buf, size_t count) {
    size_t total_read = 0;
    while (total_read < count) {
        ssize_t n = read(fd, (char *)buf + total_read, count - total_read);
        if (n == -1) return -1;
        if (n == 0) return total_read; 
        total_read += n;
    }
    return total_read;
}

void handle_list_reponse(int fd) {
    uint32_t nb_tasks_be; 
    if (read(fd, &nb_tasks_be, 4) != 4) return;
    uint32_t nb_tasks = be32toh(nb_tasks_be);

    if (nb_tasks == 0) return;

    char buf[64];

    for (uint32_t i = 0; i < nb_tasks; i++) {
        uint64_t id_be, min_be;
        uint32_t hour_be;
        uint8_t day;

        read(fd, &id_be, 8); 
        read(fd, &min_be, 8);
        read(fd, &hour_be, 4);
        read(fd, &day, 1);

        snprintf(buf, sizeof(buf), "%lu: ", be64toh(id_be));
        print_str(buf);

        print_timing_complex(be64toh(min_be), be32toh(hour_be), day);
        print_str(" ");

        deserialize_and_print_cmd(fd, 1);
        print_str("\n");
    }
}

void handle_runs_reponse(int fd) {
    uint16_t nb_runs_be;
    if (read_full(fd, &nb_runs_be, sizeof(uint16_t)) != sizeof(uint16_t)) return;
    uint16_t nb_runs = be16toh(nb_runs_be);

    char buf[512];
    snprintf(buf, sizeof(buf), "Historique d'exécution (%d runs)", nb_runs);
    write_line(buf);

    write_line("Date                      | Code Retour");

    for (int i = 0; i < nb_runs; i++) {
        int64_t time_val_be;
        uint16_t exit_code_be;

        read_full(fd, &time_val_be, sizeof(int64_t));
        read_full(fd, &exit_code_be, sizeof(uint16_t));

        time_t timestamp = (time_t)be64toh(time_val_be);
        uint16_t code = be16toh(exit_code_be);

        char *date_str = ctime(&timestamp);
        if (date_str) date_str[strlen(date_str) - 1] = '\0';

        snprintf(buf, sizeof(buf), "%-25s | %d", date_str ? date_str : "Inconnu", code);
        write_line(buf);
    }
}

void handle_output_reponse(int fd) {
    uint32_t len_be;
    if (read_full(fd, &len_be, sizeof(uint32_t)) != sizeof(uint32_t)) return;
    uint32_t len = be32toh(len_be);

    char buf[512];
    snprintf(buf, sizeof(buf), "Sortie Standard/Erreur (%u octets)", len);
    write_line(buf);
    
    char content_buf[1024];
    uint32_t remaining = len;
    while (remaining > 0) {
        int to_read = (remaining > sizeof(content_buf)) ? sizeof(content_buf) : remaining;
        int n = read(fd, content_buf, to_read);
        if (n <= 0) break;
        write(STDOUT_FILENO, content_buf, n);
        remaining -= n;
    }
    
}

void deserialize_and_print_cmd(int fd, int is_root) {
    char type[3]; 
    if (read(fd, type, 2) != 2) return;
    type[2] = '\0';

    if (strcmp(type, "SI") == 0) {
        uint32_t argc_be;
        read(fd, &argc_be, 4);
        uint32_t argc = be32toh(argc_be);

        for (uint32_t i = 0; i < argc; i++) {
            char *arg = read_string_from_pipe(fd);
            if (arg) {
                print_str(arg); 
                if (i < argc - 1) print_str(" ");
                free(arg);
            }
        }
    } 
    else if (strcmp(type, "SQ") == 0) {
        uint32_t sub_be;
        read(fd, &sub_be, 4);
        uint32_t subcount = be32toh(sub_be);

        //On n'affiche ( que si c pas la racine
        if (!is_root) print_str("( ");
        
        for (uint32_t i = 0; i < subcount; i++) {
            //Les enfants sont jamais la racine donc 0
            deserialize_and_print_cmd(fd, 0); 
            
            if (i < subcount - 1) print_str(" ; ");
        }
        if (!is_root) print_str(" )");
    }
}



int read_response(const char *pipes_dir, uint16_t original_opcode) {
    char reply_pipe_path[MAX_PATH_LEN];
    snprintf(reply_pipe_path, sizeof(reply_pipe_path), "%s/erraid-reply-pipe", pipes_dir);

    int fd = open(reply_pipe_path, O_RDONLY);
    if (fd == -1) {
        return -1;
    }

    uint16_t anstype_be;
    if (read(fd, &anstype_be, 2) != 2) {
        close(fd);
        return -1;
    }
    uint16_t anstype = be16toh(anstype_be);

    int return_code = 0;

    if (anstype == ANSTYPE_ERR) {
        uint16_t errcode_be;
        read(fd, &errcode_be, 2);
        
        char buf[64];
        snprintf(buf, sizeof(buf), "Erreur reçue du démon (Code: %04x)", be16toh(errcode_be));
        write_line(buf);
        return_code = -1; 
    } 
    else if (anstype == ANSTYPE_OK) {
        switch (original_opcode) {
            case OPCODE_LIST: handle_list_reponse(fd); break;
            case OPCODE_TIMES_EXITCODES: handle_runs_reponse(fd); break;
            case OPCODE_STDOUT:
            case OPCODE_STDERR: handle_output_reponse(fd); break;
            default: write_line("Réponse OK mais opcode inconnu.");
        }
    } else {
        write_line("Type de réponse inconnu.");
        return_code = -1;
    }

    close(fd);
    return return_code;
}



int main(int argc, char *argv[]) {
    int opt;
    char pipes_directory[MAX_PATH_LEN];
    
    // Variables pour stocker la requête
    ActionType action = ACTION_NONE;
    uint64_t target_task_id = 0;

    // Déterminer le répertoire par défaut des pipes (/tmp/$USER/erraid/pipes)
    const char *user = getenv("USER");
    if (!user) user = "unknown";
    snprintf(pipes_directory, sizeof(pipes_directory), "/tmp/%s/erraid/pipes", user);

    // Analyse des arguments avec getopt
    // "l" ne prend pas d'argument
    // "x:", "o:", "e:", "p:" prennent un argument (indiqué par le :)
    while ((opt = getopt(argc, argv, "lx:o:e:p:")) != -1) {
        switch (opt) {
            case 'l':
                action = ACTION_LIST;
                break;

            case 'x':
                action = ACTION_EXIT_CODE;
                target_task_id = strtoull(optarg, NULL, 10);
                break;

            case 'o':
                action = ACTION_STDOUT;
                target_task_id = strtoull(optarg, NULL, 10);
                break;

            case 'e':
                action = ACTION_STDERR;
                target_task_id = strtoull(optarg, NULL, 10);
                break;

            case 'p':
                // L'utilisateur force un autre chemin pour les pipes
                strncpy(pipes_directory, optarg, MAX_PATH_LEN - 1);
                pipes_directory[MAX_PATH_LEN - 1] = '\0'; // Sécurité
                break;

            default:
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Vérification
    if (action == ACTION_NONE) {
        write_error( "Erreur: Aucune action spécifiée.\n");
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    // Détermination de l'Opcode protocolaire
    uint16_t opcode = 0;
    switch (action) {
        case ACTION_LIST:
            opcode = OPCODE_LIST;
            break;
        case ACTION_EXIT_CODE:
            opcode = OPCODE_TIMES_EXITCODES;
            break;
        case ACTION_STDOUT:
            opcode = OPCODE_STDOUT;
            break;
        case ACTION_STDERR:
            opcode = OPCODE_STDERR;
            break;
        default:
            write_line("Action inconnue.\n");
            break;
    }

    if (send_request(pipes_directory, opcode, target_task_id) == 0) {

        int res = read_response(pipes_directory, opcode);
        
        if (res != 0) {
            return EXIT_FAILURE; 
        }
    } else {
        return EXIT_FAILURE; 
    }

    return EXIT_SUCCESS; 
}