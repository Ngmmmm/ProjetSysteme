#include "../include/erraid.h"
#include "../include/utils.h" 
#include <stdlib.h>  
#include <dirent.h>   
#include <string.h>  
#include <sys/stat.h> 
#include <unistd.h>    
#include <errno.h>     
#include <stdio.h>    
#include <fcntl.h> 
#include <ctype.h>



/**
 * Vérifie si le chemin donné correspond à un répertoire.
 */
int is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) == -1) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}


/**
 * Libère récursivement le *contenu* d'une struct command
 * (mais pas la struct elle-même).
 */
void free_command_contents(struct command *cmd) {
    if (cmd == NULL) return;

    if (strcmp(cmd->type, "SI") == 0) {
        // Libère le contenu de 'SI'
        for (int i = 0; i < cmd->argc; i++) {
            free(cmd->argv[i]);
        }
        free(cmd->argv);

    
    } else if (strcmp(cmd->type, "SQ") == 0) {
        // Libère le contenu de 'SQ'
        for (size_t i = 0; i < cmd->subcount; i++) {

            free_command_contents(&cmd->subcommands[i]);
        }

        free(cmd->subcommands);

    }

}


/**
 * Libère un pointeur de commande (contenu + conteneur).
 * Ne doit être appelée que sur un pointeur reçu de parse_command.
 */
void free_command(struct command *cmd) {
    if (cmd == NULL) return;
    

    free_command_contents(cmd);
    

    free(cmd);
}


char* read_string_from_fd(int fd) {
    uint32_t str_len;

    if (read_u32_be(fd, &str_len) == -1) {
        write_error("impossible de lire la longueur");
        return NULL;
    }

    
    if (str_len == 0) {
        char *empty_str = malloc(1);
        if (empty_str) empty_str[0] = '\0';
        return empty_str;
    }

    
    char *buffer = malloc(str_len + 1);
    if (!buffer) {
        write_error("malloc a échoué");
        return NULL;
    }

    
    ssize_t bytes_read = read(fd, buffer, str_len);
    if (bytes_read == -1) {
        write_error("read a échoué");
        free(buffer);
        return NULL;
    }
    if (bytes_read != str_len) {
        write_line("fin de fichier inattendue en lisant la chaîne");
        free(buffer);
        return NULL;
    }

    
    buffer[str_len] = '\0';
    return buffer;
}


struct command *parse_command(const char *cmd_path) {
    char file_path[MAX_PATH_LEN];
    int fd = -1;

   
    struct command *cmd = malloc(sizeof(struct command));
    if (!cmd) {
        write_error("malloc a échoué pour cmd");
        return NULL;
    }

    memset(cmd, 0, sizeof(struct command));


    snprintf(file_path, MAX_PATH_LEN, "%s/type", cmd_path);
    fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        write_error("impossible d'ouvrir le fichier 'type'");
        free(cmd);
        return NULL;
    }

    uint16_t type_from_file;
    if (read_u16_be(fd, &type_from_file) == -1) {
        write_error("impossible de lire le 'type'");
        close(fd); free(cmd); 
        return NULL;
    }
    close(fd);

    
    if (type_from_file == FILE_TYPE_SI) {
        strcpy(cmd->type, "SI");
    } else if (type_from_file == FILE_TYPE_SQ) {
        strcpy(cmd->type, "SQ");
    } else {
        char msg_buf[100];
        snprintf(msg_buf, 100, "type de commande inconnu: 0x%X", type_from_file);
        write_line(msg_buf);
        free(cmd); 
        return NULL;
    }


    
    if (strcmp(cmd->type, "SI") == 0) {
        snprintf(file_path, MAX_PATH_LEN, "%s/argv", cmd_path);
        fd = open(file_path, O_RDONLY);
        if (fd == -1) {
            write_error("parse_command: impossible d'ouvrir 'argv'");
            free(cmd); 
            return NULL;
        }

        uint32_t argc_from_file;
        if (read_u32_be(fd, &argc_from_file) == -1) {
            write_error("impossible de lire ARGC");
            close(fd); free(cmd); 
            return NULL;
        }
        cmd->argc = (int)argc_from_file;
        
        if (cmd->argc < 1) {
             write_line("ARGC < 1 dans un fichier argv");
             close(fd); free(cmd); 
             return NULL;
        }

        
        cmd->argv = malloc(sizeof(char *) * (cmd->argc + 1));
        if (!cmd->argv) {
            write_error("malloc a échoué pour argv");
            close(fd); free(cmd); 
            return NULL;
        }

        
        for (int i = 0; i < cmd->argc; i++) {
            cmd->argv[i] = read_string_from_fd(fd);
            if (cmd->argv[i] == NULL) {
                for (int j = 0; j < i; j++) free(cmd->argv[j]);
                free(cmd->argv);
                close(fd); free(cmd);
                return NULL;
            }
        }
        
        
        cmd->argv[cmd->argc] = NULL;
        close(fd);


    } else if (strcmp(cmd->type, "SQ") == 0) {

        DIR *d = opendir(cmd_path);
        if (!d) {
            write_error("impossible d'ouvrir le répertoire de séquence");
            free(cmd);
            return NULL;
        }

        struct dirent *entry;
        size_t count = 0;
        
        while ((entry = readdir(d)) != NULL) {
            //AJOUTE 1 A COUNT POUR CHAQUE SOUS REPERTOIRE
            if (isdigit(entry->d_name[0])) {
                count++;
            }
        }
        
        cmd->subcount = count;
        
        if (cmd->subcount == 0) {
            //SI VIDE ALORS RIEN
            cmd->subcommands = NULL; 
            closedir(d);
        } else {

            cmd->subcommands = malloc(cmd->subcount * sizeof(struct command));
            if (!cmd->subcommands) {
                write_error("malloc a échoué pour subcommands");
                closedir(d);
                free(cmd);
                return NULL;
            }

            memset(cmd->subcommands, 0, cmd->subcount * sizeof(struct command));
            
            rewinddir(d);
            //ON PARCOURS LES REPERTOIRES
            while ((entry = readdir(d)) != NULL) {
                if (isdigit(entry->d_name[0])) { // ON VERIFIE QUE LE NOM DU REPERTOIRE EST UN NOMBRE (et pas un .. par ex)
                    int index = atoi(entry->d_name);// TRANSFORME LA CHAINE DE CARACTERE EN NOMBRE -> "2" -> int 2
                    
                    if (index < 0 || (size_t)index >= cmd->subcount) { // ON VERIFIE QUIL EST VALIDE (> 0 et pas hors limite)
                        char msg_buf[50];
                        snprintf(msg_buf, 50, "Index de sous-commande invalide");
                        write_line(msg_buf);
                        continue;
                    }
                    
                    snprintf(file_path, MAX_PATH_LEN, "%s/%s", cmd_path, entry->d_name); //CONSTRUIT LE CHEMIN COMPLET VERS LE SOUS REPERTOIRE ex tasks/4/cmd/0
                    
                    struct command *temp_cmd = parse_command(file_path);// APPEL RECURSIF POUR CE MEME REPERTOIRE
                    
                    if (temp_cmd == NULL) {
                        write_error("Échec de l'appel recursif");
                        
                        free(cmd->subcommands);
                        free(cmd);
                        closedir(d);
                        return NULL;
                    }
                
                    cmd->subcommands[index] = *temp_cmd; //COPIE LE RESULTAT DANS LA BONNE CASE DU TABLEAU

                    free(temp_cmd);
                }
            }
            closedir(d);
        }

    }

    return cmd;
}


/**
 * Lit et parse le fichier 'timing' pour une tâche donnée.
 * @param task_path Le chemin vers le dossier de la tâche (ex: "/tmp/user/erraid/tasks/10").
 * @param task_timing Un pointeur vers une struct timing qui sera remplie.
 * @return 0 en cas de succès, -1 en cas d'erreur.
 */
int parse_timing(const char *task_path, struct timing *task_timing) {
    char timing_path[MAX_PATH_LEN];
    snprintf(timing_path, sizeof(timing_path), "%s/timing", task_path);

    // Ouvre le fichier en lecture seule
    int fd = open(timing_path, O_RDONLY);
    if (fd == -1) {
        write_error("parse_timing: Impossible d'ouvrir le fichier timing");
        return -1;
    }

    // Lit les 3 champs décrits dans serialisation.md
    int error = 0;

    // Minutes (uint64_t)
    if (read_u64_be(fd, &task_timing->minutes) == -1) {
        write_error("parse_timing: Erreur lecture timing (minutes)");
        error = 1;
    }

    // Heures (uint32_t)
    if (!error && read_u32_be(fd, &task_timing->hours) == -1) {
        write_error("parse_timing: Erreur lecture timing (hours)");
        error = 1;
    }

    // Jours (uint8_t) 1 octet
    if (!error && read(fd, &task_timing->daysofweek, 1) != 1) {
        write_error("parse_timing: Erreur lecture timing (days)");
        error = 1;
    }

    close(fd);

    return error ? -1 : 0;
}


/**
 * Fonction de comparaison pour qsort, pour trier les tâches par taskid.
 */
int compare_tasks(const void *a, const void *b) {
    // Convertit les pointeurs 'void' en pointeurs 'struct task'
    struct task *taskA = (struct task *)a;
    struct task *taskB = (struct task *)b;

    // Compare les taskid
    if (taskA->taskid < taskB->taskid) return -1;
    if (taskA->taskid > taskB->taskid) return 1;
    return 0;
}

/**
 * Charge toutes les tâches depuis le disque en utilisant un tableau dynamique,
 * les trie par ID, et renvoie un tableau alloué sur le tas.
 * @param task_count Un pointeur de sortie pour stocker le nombre de tâches trouvées.
 * @return Un pointeur vers le tableau de 'struct task',
 * ou NULL en cas d'erreur.
 */
struct task *load_all_tasks(size_t *task_count) {
    char base_dir[MAX_PATH_LEN];
    int len;

    const char *user = getenv("USER");
    if (!user) user = "unknown";

    len = snprintf(base_dir, sizeof(base_dir), "/tmp/%s/erraid/tasks", user);
    if (len >= sizeof(base_dir)) {
        write_line("Erreur: Le chemin du répertoire de base est trop long.");
        return NULL;
    }

    DIR *dirp = opendir(base_dir);
    if (!dirp) {
        write_error("Impossible d'ouvrir le répertoire des tâches");
        return NULL;
    }

    // Initialisation du tableau dynamique 
    size_t count = 0;     
    size_t capacity = 10;  // Taille initiale 
    struct task *task_array = malloc(sizeof(struct task) * capacity);
    
    if (!task_array) {
        write_error("malloc initial failed");
        closedir(dirp);
        return NULL;
    }

    // Parcourir le répertoire (un seul passage) 
    struct dirent *entry;
    while ((entry = readdir(dirp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char task_path[MAX_PATH_LEN];
        snprintf(task_path, sizeof(task_path), "%s/%s", base_dir, entry->d_name);
        
        if (is_directory(task_path)) {
            // Agrandir le tableau si nécessaire
            if (count == capacity) {
                capacity *= 2; // Double la taille
                struct task *new_array = realloc(task_array, sizeof(struct task) * capacity);
                if (!new_array) {
                    write_error("realloc failed");
                    free(task_array); 
                    closedir(dirp);
                    return NULL;
                }
                task_array = new_array;
            }

            // Remplir la tâche à l'index 'count' 
            struct task *current_task = &task_array[count];
            
            // Remplir taskid
            current_task->taskid = strtoull(entry->d_name, NULL, 10);

            // Remplir timing
            if (parse_timing(task_path, &current_task->timing) != 0) {
                continue; // Ignore cette tâche
            }

            // Remplir la commande
            char cmd_path[MAX_PATH_LEN];
            snprintf(cmd_path, sizeof(cmd_path), "%s/cmd", task_path);
            
            current_task->command = parse_command(cmd_path);
            if (current_task->command == NULL) {
                continue; // Ignore cette tâche
            }

            
            count++;
        }
    }
    closedir(dirp);

    if (count == 0) {
        free(task_array);
        *task_count = 0;
        return NULL;
    }

    // Trier le tableau 
    qsort(task_array, count, sizeof(struct task), compare_tasks);

    *task_count = count;
    return task_array;
}





/**
 *  Point d'entrée principal
 */
int main(void) {
    write_line("=== Démon erraid Test de chargement (Compteur) ===");

    size_t task_count = 0;
    struct task *all_tasks = load_all_tasks(&task_count);

    if (all_tasks == NULL) {
        write_line("Aucune tâche n'a été chargée.");
        return 0;
    }

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%zu tâches chargées et triées :", task_count);
    write_line(buffer);





    // Boucle pour afficher les tâches (maintenant triées) TEST
    for (size_t i = 0; i < task_count; i++) {
        snprintf(buffer, sizeof(buffer), "--- Tâche ID: %lu ---", all_tasks[i].taskid);
        write_line(buffer);
        
        print_command(all_tasks[i].command, 1);
        
        // Nettoyage
        free_command(all_tasks[i].command);
    }

    // Libère le tableau principal
    free(all_tasks);

    return 0;
}