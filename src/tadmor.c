#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <endian.h>
#include "../include/erraid.h"
#include "../include/utils.h"

// Définition de la longueur maximale des chemins (comme dans erraid)
#define MAX_PATH_LEN 1024

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




    // Envoi de la requête
    char msg[256];
    snprintf(msg, sizeof(msg), "Envoi requête vers %s...", pipes_directory);
    write_line(msg);

    if (send_request(pipes_directory, opcode, target_task_id) == 0) {
        write_line("Requête envoyée avec succès !");
        // TODO : Attendre et lire la réponse
        write_line("(Attente de la réponse non implémentée pour l'instant)");
    }


    return 0;
}