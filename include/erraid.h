#define ERRAID_H

#include <stdlib.h>    // getenv
#include <unistd.h>    // write, STDOUT_FILENO, STDERR_FILENO
#include <stdint.h>

#define MAX_PATH_LEN 1024

// Signatures binaires pour les types de commandes (utilisées dans parse_command)
#define FILE_TYPE_SI 0x5349 // 'SI' 
#define FILE_TYPE_SQ 0x5351 // 'SQ' 

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
    struct command command;
};

/**
 * Vérifie si le chemin donné correspond à un répertoire.
 */
int is_directory(const char *path);

/**
 * Libère récursivement une structure command et ses sous-éléments.
 */
void free_command(struct command *cmd);

/**
 * Lit une chaîne depuis un descripteur de fichier binaire.
 * Le format attendu est : longueur (uint32_t BE) + octets de la chaîne.
 */
char *read_string_from_fd(int fd);

/**
 * Analyse récursivement une commande à partir d’un chemin de répertoire.
 */
struct command *parse_command(const char *cmd_path);

/**
 * Parcourt le répertoire /tmp/$USER/erraid/tasks et affiche les tâches trouvées.
 */
void find_all_tasks(void);
