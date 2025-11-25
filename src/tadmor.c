#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <stdint.h>
#include <errno.h>

// Définition de la longueur maximale des chemins (comme dans erraid)
#define MAX_PATH_LEN 1024

// Codes des opérations possibles (Opcode)
typedef enum {
    OP_NONE = 0,
    OP_LIST,            // -l
    OP_EXIT_CODE,       // -x
    OP_STDOUT_PATH,     // -o
    OP_STDERR_PATH      // -e
} OpCode;

/**
 * Affiche l'aide si l'utilisateur se trompe.
 */
void print_usage(const char *prog_name) {
    write_error( "Usage: %s [options]\n", prog_name);
    write_error( "Options:\n");
    write_error( "  -l          Lister les tâches surveillées.\n");
    write_error( "  -x <id>     Obtenir le dernier code de retour de la tâche <id>.\n");
    write_error( "  -o <id>     Obtenir le chemin du fichier stdout de la tâche <id>.\n");
    write_error( "  -e <id>     Obtenir le chemin du fichier stderr de la tâche <id>.\n");
    write_error( "  -p <dir>    Spécifier le répertoire des tubes nommés.\n");
}

int main(int argc, char *argv[]) {
    int opt;
    char pipes_directory[MAX_PATH_LEN];
    
    // Variables pour stocker la requête
    OpCode action = OP_NONE;
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
                action = OP_LIST;
                break;

            case 'x':
                action = OP_EXIT_CODE;
                target_task_id = strtoull(optarg, NULL, 10);
                break;

            case 'o':
                action = OP_STDOUT_PATH;
                target_task_id = strtoull(optarg, NULL, 10);
                break;

            case 'e':
                action = OP_STDERR_PATH;
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
    if (action == OP_NONE) {
        write_error( "Erreur: Aucune action spécifiée.\n");
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    
    // Affichage répertoires des pipes cible.
    write_line("DEBUG TADMOR (Client) \n");
    write_line("Répertoire des pipes cible : %s\n", pipes_directory);
    
    switch (action) {
        case OP_LIST:
            write_line("Action demandée : LISTER les tâches (-l)\n");
            break;
        case OP_EXIT_CODE:
            write_line("Action demandée : CODE DE RETOUR pour la tâche ID %lu (-x)\n", target_task_id);
            break;
        case OP_STDOUT_PATH:
            write_line("Action demandée : CHEMIN STDOUT pour la tâche ID %lu (-o)\n", target_task_id);
            break;
        case OP_STDERR_PATH:
            write_line("Action demandée : CHEMIN STDERR pour la tâche ID %lu (-e)\n", target_task_id);
            break;
        default:
            write_line("Action inconnue.\n");
    }

    return 0;
}