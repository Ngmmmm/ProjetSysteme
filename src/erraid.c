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


void free_command(struct command *cmd) {
    if (cmd == NULL) return;

    if (strcmp(cmd->type, "SI") == 0) {
        for (int i = 0; i < cmd->argc; i++) {
            free(cmd->argv[i]);
        }
        free(cmd->argv);

    } else if (strcmp(cmd->type, "SQ") == 0) {
        for (size_t i = 0; i < cmd->subcount; i++) {
            free_command(&cmd->subcommands[i]);
        }
        free(cmd->subcommands);
    }
    
    
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
 * Parcourt le répertoire /tmp/$USER/erraid/tasks/
 * et affiche les identifiants des tâches (répertoires).
 */
void find_all_tasks(void) {
    char base_dir[MAX_PATH_LEN];
    char message_buffer[MAX_PATH_LEN * 2];
    int len;

    const char *user = getenv("USER");
    if (!user) user = "unknown";

    // Construit le chemin : /tmp/$USER/erraid/tasks
    len = snprintf(base_dir, sizeof(base_dir), "/tmp/%s/erraid/tasks", user);
    if (len >= sizeof(base_dir)) {
        write_line("Erreur: Le chemin du répertoire de base est trop long.");
        return;
    }

    DIR *dirp = opendir(base_dir);
    if (!dirp) {
        write_error("Impossible d'ouvrir le répertoire des tâches");
        return;
    }

    // Affiche le répertoire des tâches
    len = snprintf(message_buffer, sizeof(message_buffer), "Répertoire des tâches : %s", base_dir);
    write_line(message_buffer);

    struct dirent *entry;

    while ((entry = readdir(dirp)) != NULL) {
        // Ignore "." et ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char task_path[MAX_PATH_LEN];
        len = snprintf(task_path, sizeof(task_path), "%s/%s", base_dir, entry->d_name);
        
        if (len >= sizeof(task_path)) {
            snprintf(message_buffer, sizeof(message_buffer), "Erreur: Chemin trop long pour %s", entry->d_name);
            write_line(message_buffer);
            continue;
        }

        if (is_directory(task_path)) {
            snprintf(message_buffer, sizeof(message_buffer), "Tâche trouvée : %s", entry->d_name);
            write_line(message_buffer);

            // === Lecture du fichier timing ===
            char timing_path[MAX_PATH_LEN];
            snprintf(timing_path, sizeof(timing_path), "%s/timing", task_path);

            int fd = open(timing_path, O_RDONLY);
            if (fd == -1) {
                write_error("Impossible d'ouvrir le fichier timing");
                continue;
            }

            struct timing task_timing; 

            if (read_u64_be(fd, &task_timing.minutes) == -1) {
                 write_error("Erreur lecture timing (minutes)");
                 close(fd);
                 continue;
            }
            if (read_u32_be(fd, &task_timing.hours) == -1) {
                 write_error("Erreur lecture timing (hours)");
                 close(fd);
                 continue;
            }
            if (read_u8(fd, &task_timing.daysofweek) == -1) { 
                 write_error("Erreur lecture timing (days)");
                 close(fd);
                 continue;
            }
            close(fd);

            //snprintf(message_buffer, sizeof(message_buffer), "Timing : %lu", timing_value);
            //write_line(message_buffer);
        }
    }

    closedir(dirp);
}





int main(void) {
    write_line("=== Démon erraid - Étape 1 (parcours des tâches) ===");
    find_all_tasks();
    return 0;
}