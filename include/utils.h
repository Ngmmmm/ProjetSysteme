#define UTILS_H

#include <stdint.h> // Pour uint16_t, uint32_t, uint64_t
#include <stddef.h>

/**
 * Écrit une chaîne suivie d'un retour à la ligne sur la sortie standard (stdout).
 */
void write_line(const char *line);

/**
 * Écrit un message d'erreur sur la sortie d'erreur standard (stderr).
 * Récupère le message d'erreur système via strerror(errno).
 */
void write_error(const char *prefix);

// Nouvelles fonctions de lecture (big-endian)
int read_u16_be(int fd, uint16_t *val);
int read_u32_be(int fd, uint32_t *val);
int read_u64_be(int fd, uint64_t *val);
int read_u8(int fd, uint8_t *val);
void print_command(struct command *cmd, int indent);

