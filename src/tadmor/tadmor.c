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
    ACTION_STDERR,          // -e
    ACTION_CREATE,          // -c
    ACTION_REMOVE,          // -d
    ACTION_COMBINE          // -s, -p et -i
} ActionType;

/**
 * Affiche l'aide si l'utilisateur se trompe.
 */
void print_usage(const char *prog_name) {
    write_error("Usage: "); write_error(prog_name); write_error(" [options]\n");
    write_error("Options:\n");
    write_error("  -l          Lister les tâches.\n");
    write_error("  -c          Créer une nouvelle tâche (suivi de la commande).\n");
    write_error("  -r <id>     Supprimer la tâche <id>.\n");
    write_error("  -x <id>     Lister les codes de retour de la tâche <id>.\n");
    write_error("  -o <id>     Afficher stdout de la tâche <id>.\n");
    write_error("  -e <id>     Afficher stderr de la tâche <id>.\n");
    write_error("Options de temps (pour -c) :\n");
    write_error("  -m <mins>   Masque minutes (Bitmask 64 bits).\n");
    write_error("  -H <heures> Masque heures (Bitmask 32 bits).\n");
    write_error("  -d <jours>  Masque jours (Bitmask 8 bits).\n");
    write_error("  -P <dir>    Répertoire des tubes nommés.\n");
    write_error("  -s <dst> <src> Combiner Séquentiellement (dst ; src).\n");
    write_error("  -p <dst> <src> Combiner en Pipeline (dst | src).\n");
    write_error("  -i <dst> <src> Combiner en Conditionnel (if dst then src).\n");
}


int main(int argc, char *argv[]) {
    int opt;
    char pipes_directory[MAX_PATH_LEN];
    
    // Variables pour stocker la requête
    ActionType action = ACTION_NONE;
    uint64_t target_task_id = 0;
    uint16_t combine_type = FILE_TYPE_SQ;

    uint64_t tm_min = 0xFFFFFFFFFFFFFFFF;
    uint32_t tm_hour = 0xFFFFFFFF;
    uint8_t tm_day = 0xFF;

    // Déterminer le répertoire par défaut des pipes (/tmp/$USER/erraid/pipes)
    const char *user = getenv("USER");
    if (!user) user = "unknown";
    snprintf(pipes_directory, sizeof(pipes_directory), "/tmp/%s/erraid/pipes", user);

    // Analyse des arguments avec getopt
    // "l" ne prend pas d'argument
    // "x:", "o:", "e:", "p:" prennent un argument (indiqué par le :)
    //ajout des nouveaux arguments r c m h d k
    while ((opt = getopt(argc, argv, "lx:o:e:P:r:cm:H:d:p:s:i:")) != -1) {
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

            case 'r': // Remove
                action = ACTION_REMOVE; 
                target_task_id = strtoull(optarg, NULL, 10); 
                break;
            
            case 'c': // Create
                action = ACTION_CREATE; 
                break;

            case 'm': tm_min = strtoull(optarg, NULL, 10); break;
            case 'H': tm_hour = (uint32_t)strtoul(optarg, NULL, 10); break;
            case 'd': tm_day = (uint8_t)atoi(optarg); break;

            case 's': // Séquence
                action = ACTION_COMBINE;
                combine_type = FILE_TYPE_SQ;
                target_task_id = strtoull(optarg, NULL, 10);
                break;

            case 'P': // Pipeline
                action = ACTION_COMBINE;
                combine_type = FILE_TYPE_PL;
                target_task_id = strtoull(optarg, NULL, 10);
                break;

            case 'i': // Conditionnel 
                action = ACTION_COMBINE;
                combine_type = FILE_TYPE_IF;
                target_task_id = strtoull(optarg, NULL, 10); 
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


    if (action == ACTION_CREATE) {
        int cmd_argc = argc - optind;
        char **cmd_argv = &argv[optind];

        if (cmd_argc < 1) {
            write_error("Erreur: Commande manquante pour création (-c).\n");
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }

        if (send_create_request(pipes_directory, tm_min, tm_hour, tm_day, cmd_argc, cmd_argv) == 0) {
            return read_response(pipes_directory, OPCODE_CREATE);
        }
        return EXIT_FAILURE;
    }
    if (action == ACTION_REMOVE) {
        if (send_request(pipes_directory, OPCODE_REMOVE, target_task_id) == 0) {
            return read_response(pipes_directory, OPCODE_REMOVE);
        }
        return EXIT_FAILURE;
    }
    
    if (action == ACTION_COMBINE) {
        if (optind >= argc) {
            write_error("Erreur: ID Source manquant.\n");
            return EXIT_FAILURE;
        }
        uint64_t source_task_id = strtoull(argv[optind], NULL, 10);

        if (send_combine_request(pipes_directory, combine_type, target_task_id, source_task_id) == 0) {
            return read_response(pipes_directory, OPCODE_COMBINE);
        }
        return EXIT_FAILURE;
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

    if (action != ACTION_CREATE && action != ACTION_REMOVE && action != ACTION_COMBINE) {
        if (send_request(pipes_directory, opcode, target_task_id) == 0) {
            int res = read_response(pipes_directory, opcode);
            if (res != 0) return EXIT_FAILURE;
        } else {
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS; 
}