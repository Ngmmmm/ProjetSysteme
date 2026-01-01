#ifndef ERRAID_H 
#define ERRAID_H

#include <stdint.h>
#include <time.h>

#define MAX_PATH_LEN 1024

#define OPCODE_CREATE    0x0006
#define OPCODE_REMOVE    0x0007






/**
 * Structure de planification lue depuis le fichier 'timing'.
 * Contient les bits pour les minutes, heures et jours de la semaine.
 */
struct timing {
    uint64_t minutes;
    uint32_t hours;
    uint8_t daysofweek;
};

/**
 * Structure représentant une commande (simple ou séquence) avec le nombre 
 * et la value des args.
 */
struct command {
    char type[3]; // "SI" ou "SQ"

    // --- Pour le type "SI" ---
    int argc;     
    char **argv;  

    // --- Pour le type "SQ" ---
    struct command *subcommands; 
    size_t subcount;             
};

/**
 * Structure principale représentant une tâche complète chargée en mémoire.
 */
struct task {
    uint64_t taskid;
    struct timing timing;
    struct command *command;
};

#endif
