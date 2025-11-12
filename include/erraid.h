#define ERRAID_H

#include <stdlib.h>    // getenv
#include <unistd.h>    // write, STDOUT_FILENO, STDERR_FILENO
#include <stdint.h>



#define MAX_PATH_LEN 1024
#define FILE_TYPE_SI 0x5349 // 'SI' 
#define FILE_TYPE_SQ 0x5351 // 'SQ' 

/**
 * Vérifie si le chemin donné correspond à un répertoire.
 */
int is_directory(const char *path);

/**
 * Parcourt le répertoire /tmp/$USER/erraid/tasks/
 * et affiche les identifiants des tâches (répertoires).
 */
void find_all_tasks(void);


struct timing {
    uint64_t minutes;
    uint32_t hours;
    uint8_t daysofweek;
};

struct command {
    char type[3]; // "SI" ou "SQ"

    // --- Pour le type "SI" ---
    int argc;     
    char **argv;  

    // --- Pour le type "SQ" ---
    struct command *subcommands; 
    size_t subcount;             
};

struct task {
    uint64_t taskid;
    struct timing timing;
    struct command command;
};
