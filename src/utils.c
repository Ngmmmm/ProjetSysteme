#include "utils.h" 
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