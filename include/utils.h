#define UTILS_H

#include <stdint.h> // Pour uint16_t, uint32_t, uint64_t


// Fonctions d'écriture 
void write_line(const char *line);
void write_error(const char *prefix);

// Nouvelles fonctions de lecture (big-endian)
int read_u16_be(int fd, uint16_t *val);
int read_u32_be(int fd, uint32_t *val);
int read_u64_be(int fd, uint64_t *val);

