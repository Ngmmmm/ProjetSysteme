#include "../include/erraid.h"
#include "../include/utils.h"
#include "../include/client_comm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>


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