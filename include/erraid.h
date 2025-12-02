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

int is_directory(const char *path);

void free_command(struct command *cmd);

void free_command_contents(struct command *cmd);

char *read_string_from_fd(int fd);

struct command *parse_command(const char *cmd_path);

int parse_timing(const char *task_path, struct timing *task_timing);

int compare_tasks(const void *a, const void *b);

struct task *load_all_tasks(size_t *task_count);

int execute_simple_command_in_terminal(struct command *cmd, uint64_t taskid);

int execute_sequence_command_in_terminal(struct command *cmd, uint64_t taskid);

int execute_command_in_terminal(struct command *cmd, uint64_t taskid);

int is_time_to_run(struct task *t, struct tm *now);

void log_execution(uint64_t taskid, int exit_code);

void init_communication();

void serialize_command_to_buffer(struct command *cmd, char *buffer, size_t buf_size);