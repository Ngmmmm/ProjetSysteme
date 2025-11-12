#include "../include/erraid.h"
#include "../include/utils.h" 
#include <stdlib.h>     // Pour malloc, free, exit, atoi
#include <string.h>     // Pour strlen, memcpy, strcpy, memset
#include <unistd.h>     // Pour read, close
#include <fcntl.h>      // Pour open, O_RDONLY
#include <dirent.h>     // Pour opendir, readdir, closedir
#include <ctype.h>      // Pour isdigit
#include <errno.h>      // Pour errno

// --- Constantes pour les types de commande ---
// 0x5349 est 'S' 'I' en ASCII
#define TYPE_SI 0x5349 
// 0x5351 est 'S' 'Q' en ASCII
#define TYPE_SQ 0x5351

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

int read_u8(int fd, uint8_t *val) {
    ssize_t bytes_read = read(fd, val, sizeof(*val));
    if (bytes_read == -1) return -1;
    if (bytes_read != sizeof(*val)) {
        errno = EBADMSG;
        return -1;
    }
    return 0;
}
