#include "../include/erraid.h"
#include "../include/utils.h" 
#include <unistd.h>    
#include <endian.h>   
#include <string.h>    
#include <stdio.h>     
#include <stdlib.h>    
#include <fcntl.h>     
#include <dirent.h>     
#include <ctype.h>      
#include <errno.h>      

// --- Constantes pour les types de commande ---
// 0x5349 est 'S' 'I' en ASCII
#define TYPE_SI 0x5349 
// 0x5351 est 'S' 'Q' en ASCII
#define TYPE_SQ 0x5351


void write_line(const char *line) {
    write(STDOUT_FILENO, line, strlen(line));
    write(STDOUT_FILENO, "\n", 1);
}

void print_str(const char *str) {
    write(STDOUT_FILENO, str, strlen(str));
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

void serialize_command_to_buffer(struct command *cmd, char *buffer, size_t size) {
    if (strlen(buffer) >= size - 1) return; 
    
    if (strcmp(cmd->type, "SI") == 0) {
        for (int i = 0; i < cmd->argc; i++) {
            strncat(buffer, cmd->argv[i], size - strlen(buffer) - 1);
            if (i < cmd->argc - 1) strncat(buffer, " ", size - strlen(buffer) - 1);
        }
    } else {
        strncat(buffer, "(", size - strlen(buffer) - 1);
        for (size_t i = 0; i < cmd->subcount; i++) {
            serialize_command_to_buffer(&cmd->subcommands[i], buffer, size);
            if (i < cmd->subcount - 1) strncat(buffer, "; ", size - strlen(buffer) - 1);
        }
        strncat(buffer, ")", size - strlen(buffer) - 1);
    }
}

int read_u8(int fd, uint8_t *val) {
    ssize_t bytes_read = read(fd, val, sizeof(*val));
    if (bytes_read == -1) return -1;
    if (bytes_read != sizeof(*val)) {
        errno = EBADMSG;
        return -1;
    }
    return 0;
}

char* read_string_from_pipe(int fd) {
    uint32_t len_be;
    ssize_t n = read(fd, &len_be, 4);
    if (n != 4) return NULL;
    
    uint32_t len = be32toh(len_be);

    char *buf = malloc(len + 1);
    if (!buf) return NULL;
    
    if (len > 0) {
        ssize_t r = read(fd, buf, len);
        if (r != (ssize_t)len) {
            free(buf);
            return NULL;
        }
    }
    buf[len] = '\0';
    return buf;
}

void print_mask_list(uint64_t mask, int max_bits) {
    uint64_t full_mask = 0;
    for(int i=0; i<max_bits; i++) full_mask |= ((uint64_t)1 << i);
    
    if ((mask & full_mask) == full_mask) {
        print_str("*");
        return;
    }

    int first = 1;
    int start = -1;
    char buf[32];

    for (int i = 0; i <= max_bits; i++) {
        int bit = (i < max_bits) ? ((mask >> i) & 1) : 0;

        if (bit) {
            if (start == -1) start = i;
        } else {
            if (start != -1) {
                if (!first) print_str(",");
                
                if (start == i - 1) {
                    snprintf(buf, sizeof(buf), "%d", start);
                } else {
                    snprintf(buf, sizeof(buf), "%d-%d", start, i - 1);
                }
                print_str(buf);
                
                first = 0;
                start = -1;
            }
        }
    }
    if (first) print_str("-");
}

void print_timing_complex(uint64_t min, uint32_t hour, uint8_t day) {
    print_mask_list(min, 60);
    print_str(" ");
    print_mask_list((uint64_t)hour, 24);
    print_str(" ");
    print_mask_list((uint64_t)day, 7);
}

