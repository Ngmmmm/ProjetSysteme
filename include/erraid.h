#ifndef ERRAID_H 
#define ERRAID_H

#include <stdint.h>
#include <time.h>

#define MAX_PATH_LEN 1024


// Opcodes pour les requêtes (Client -> Démon)
#define OPCODE_LIST             0x4C53 // 'LS'
#define OPCODE_CREATE           0x4352 // 'CR'
#define OPCODE_REMOVE           0x524D // 'RM'
#define OPCODE_TERMINATE        0x544D // 'TM'
#define OPCODE_TIMES_EXITCODES  0x5458 // 'TX'
#define OPCODE_STDOUT           0x534F // 'SO'
#define OPCODE_STDERR           0x5345 // 'SE'
#define OPCODE_COMBINE          0x4342 // 'CB'



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
