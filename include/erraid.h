#define ERRAID_H

#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>





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

/**
 * Vérifie si le chemin donné correspond à un répertoire.
 */
int is_directory(const char *path);

/**
 * Libère toute la mémoire associée à une commande (structure + contenu).
 */
void free_command(struct command *cmd);

/**
 * Libère récursivement les ressources allouées d'une struct command (contenu interne).
 */
void free_command_contents(struct command *cmd);

/**
 * Lit une chaîne (taille u32be + contenu) depuis un descripteur de fichier.
 */
char *read_string_from_fd(int fd);

/**
 * Construit une structure command à partir d'un répertoire.
 */
struct command *parse_command(const char *cmd_path);

/**
 * Lit et parse le fichier 'timing' pour une tâche donnée.
 */
int parse_timing(const char *task_path, struct timing *task_timing);

/**
 * Fonction de comparaison pour qsort (tri par taskid).
 */
int compare_tasks(const void *a, const void *b);

/**
 * Charge toutes les tâches en mémoire.
 */
struct task *load_all_tasks(size_t *task_count);

/**
 * Exécute une commande simple (SI).
 */
int execute_simple_command_in_terminal(struct command *cmd, uint64_t taskid);

/**
 * Exécute une séquence de commandes (SQ).
 */
int execute_sequence_command_in_terminal(struct command *cmd, uint64_t taskid);

/**
 * Point d'entrée générique pour exécuter une commande (SI ou SQ).
 */
int execute_command_in_terminal(struct command *cmd, uint64_t taskid);

/**
 * Vérifie si une tâche doit être exécutée à l'heure actuelle.
 */
int is_time_to_run(struct task *t, struct tm *now);

/**
 * Enregistre le code de sortie et l'heure dans le journal.
 */
void log_execution(uint64_t taskid, int exit_code);

/**
 * Initialise les tubes nommés (pipes) pour la communication.
 */
void init_communication();

/**
 * Traite une requête reçue depuis le pipe de requête.
 */
void handle_client_request(int req_fd, struct task *tasks, size_t task_count);

