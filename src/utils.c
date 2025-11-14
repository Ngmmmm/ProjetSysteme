#include "../include/utils.h" 
#include "../include/erraid.h" 
#include <unistd.h>    // Pour read(), write(), STDOUT_FILENO, ...
#include <endian.h>    // Pour be16toh, be32toh, be64toh
#include <errno.h>     // Pour errno
#include <string.h>    // Pour strlen(), strerror()
#include <stdio.h>     // Pour snprintf()

void write_line(const char *line) {
    write(STDOUT_FILENO, line, strlen(line));
    write(STDOUT_FILENO, "\n", 1);
}

void write_error(const char *prefix) {
    const char *err_msg = strerror(errno);
    write(STDERR_FILENO, prefix, strlen(prefix));
    write(STDERR_FILENO, ": ", 2);
    write(STDERR_FILENO, err_msg, strlen(err_msg));
    write(STDERR_FILENO, "\n", 1);
}


int read_u16_be(int fd, uint16_t *val) {
    uint16_t net_val; 
    ssize_t bytes_read = read(fd, &net_val, sizeof(net_val));
    if (bytes_read == -1) return -1;
    if (bytes_read != sizeof(net_val)) {
        errno = EBADMSG; 
        return -1;
    }
    *val = be16toh(net_val);
    return 0;
}

int read_u32_be(int fd, uint32_t *val) {
    uint32_t net_val; 
    ssize_t bytes_read = read(fd, &net_val, sizeof(net_val));
    if (bytes_read == -1) return -1;
    if (bytes_read != sizeof(net_val)) {
        errno = EBADMSG;
        return -1;
    }
    *val = be32toh(net_val);
    return 0;
}

int read_u64_be(int fd, uint64_t *val) {
    uint64_t net_val; 
    ssize_t bytes_read = read(fd, &net_val, sizeof(net_val));
    if (bytes_read == -1) return -1;
    if (bytes_read != sizeof(net_val)) {
        errno = EBADMSG;
        return -1;
    }
    *val = be64toh(net_val);
    return 0;
}


/**
 * Affiche récursivement le contenu d'une structure command.
 * @param cmd Pointeur vers la commande à afficher.
 * @param indent Niveau d'indentation pour un affichage lisible.
 */
void print_command(struct command *cmd, int indent) {
    // Crée une chaîne d'indentation (ex: "    ")
    char indent_str[32] = {0};
    for(int i = 0; i < indent * 2 && i < 31; i++) {
        indent_str[i] = ' ';
    }
    
    char buffer[MAX_PATH_LEN];


    if (strcmp(cmd->type, "SI") == 0) {
        snprintf(buffer, sizeof(buffer), "%s-> [SI] argc = %d", indent_str, cmd->argc);
        write_line(buffer);
        // Affiche chaque argument
        for (int i = 0; i < cmd->argc; i++) {
            snprintf(buffer, sizeof(buffer), "%s  argv[%d]: %s", indent_str, i, cmd->argv[i]);
            write_line(buffer);
        }
    

    } else if (strcmp(cmd->type, "SQ") == 0) {
        snprintf(buffer, sizeof(buffer), "%s-> [SQ] subcount = %zu", indent_str, cmd->subcount);
        write_line(buffer);
        // Appel récursif pour les sous-commandes
        for (size_t i = 0; i < cmd->subcount; i++) {
            
            print_command(&cmd->subcommands[i], indent + 1);
        }


    } else {
        snprintf(buffer, sizeof(buffer), "%s-> [??] Type inconnu (%s)", indent_str, cmd->type);
        write_line(buffer);
    }
}