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
            default: write_line("Réponse OK mais opcode inconnu.");
        }
    } else {
        write_line("Type de réponse inconnu.");
        return_code = -1;
    }

    close(fd);
    return return_code;
}
