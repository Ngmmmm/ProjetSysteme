#include "../include/erraid.h"
#include "../include/utils.h"
#include "../include/client_comm.h"
#include "../include/response_handlers.h" // Nécessaire pour appeler les handlers !

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <endian.h>

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

int send_create_request(const char *pipes_dir, uint64_t minutes, uint32_t hours, uint8_t day,int argc, char **argv) {
    char req_pipe_path[MAX_PATH_LEN];
    snprintf(req_pipe_path, sizeof(req_pipe_path), "%s/erraid-request-pipe", pipes_dir);

    int fd = open(req_pipe_path, O_WRONLY);
    if (fd == -1) {
        write_error("Erreur: Impossible d'ouvrir le tube de requête");
        return -1;
    }

    // OPCODE ('CR')
    uint16_t opcode_be = htobe16(OPCODE_CREATE);
    write(fd, &opcode_be, sizeof(uint16_t));

    // TIMING (Minutes, Heures, Jours)
    uint64_t min_be = htobe64(minutes);
    uint32_t hour_be = htobe32(hours);
    write(fd, &min_be, sizeof(uint64_t));
    write(fd, &hour_be, sizeof(uint32_t));
    write(fd, &day, sizeof(uint8_t));

    // COMMAND (ARGC + Strings)
    uint32_t argc_be = htobe32((uint32_t)argc);
    write(fd, &argc_be, sizeof(uint32_t));

    for (int i = 0; i < argc; i++) {
        uint32_t len = strlen(argv[i]);
        uint32_t len_be = htobe32(len);
        write(fd, &len_be, sizeof(uint32_t));
        write(fd, argv[i], len);
    }

    close(fd);
    return 0;
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
    
        return_code = -1; 
    } 
    else if (anstype == ANSTYPE_OK) {
        switch (original_opcode) {
            case OPCODE_LIST: handle_list_reponse(fd); break;
            case OPCODE_TIMES_EXITCODES: handle_runs_reponse(fd); break;
            case OPCODE_STDOUT:
            case OPCODE_STDERR: handle_output_reponse(fd); break;
            case OPCODE_TERMINATE: write_line("Succès : Le démon s'est arrêté proprement."); break;

            case OPCODE_CREATE: 
                {
                    uint64_t new_id_be;
                    if (read(fd, &new_id_be, 8) == 8) {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%lu\n", be64toh(new_id_be));
                        print_str(buf);
                    }
                }
                break;
            case OPCODE_REMOVE: break;
            case OPCODE_COMBINE: {
                    uint64_t new_id_be;
                    // On lit 8 octets (l'ID retourné par le serveur)
                    if (read(fd, &new_id_be, 8) == 8) {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%lu\n", be64toh(new_id_be));
                        print_str(buf); 
                    }
                }
                break;
            default: write_line("Réponse OK mais opcode inconnu.");
        }
    } else {
        write_line("Type de réponse inconnu.");
        return_code = -1;
    }

    close(fd);
    return return_code;
}



int send_combine_request(const char *pipes_dir, uint16_t type, uint64_t id_dest, uint64_t id_src) {
    char req_pipe_path[MAX_PATH_LEN];
    snprintf(req_pipe_path, sizeof(req_pipe_path), "%s/erraid-request-pipe", pipes_dir);

    int fd = open(req_pipe_path, O_WRONLY);
    if (fd == -1) {
        write_error("Erreur: Impossible d'ouvrir le tube de requête (Combine)\n");
        return -1;
    }

    //Opcode
    uint16_t op_be = htobe16(OPCODE_COMBINE);
    write(fd, &op_be, 2);

    //TYPE
    uint16_t type_be = htobe16(type);
    write(fd, &type_be, 2);

    //ID Destination
    uint64_t dest_be = htobe64(id_dest);
    write(fd, &dest_be, 8);

    //ID Source
    uint64_t src_be = htobe64(id_src);
    write(fd, &src_be, 8);

    close(fd);
    return 0;
}