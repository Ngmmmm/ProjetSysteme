#define UTILS_H

#include <stdint.h> // Pour uint16_t, uint32_t, uint64_t
#include <stddef.h>



// Opcodes pour les requêtes (Client -> Démon)
#define OPCODE_LIST            0x4c53 // 'LS'
#define OPCODE_TIMES_EXITCODES 0x5458 // 'TX'
#define OPCODE_STDOUT          0x534f // 'SO'
#define OPCODE_STDERR          0x5345 // 'SE'
#define OPCODE_TERMINATE       0x4b49 // 'TM'

// Codes de réponse (Démon -> Client) 
#define ANSTYPE_OK             0x4f4b // 'OK'
#define ANSTYPE_ERR            0x4552 // 'ER'

// Codes d'erreur
#define ERRCODE_NOT_FOUND      0x4e46 // 'NF'
#define ERRCODE_NO_RUN         0x4e52 // 'NR'



#define MAX_PATH_LEN 1024

// Signatures binaires pour les types de commandes (utilisées dans parse_command)
#define FILE_TYPE_SI 0x5349 // 'SI' 
#define FILE_TYPE_SQ 0x5351 // 'SQ' 

#define FILE_TYPE_PL 0x504C // 'PL' 
#define FILE_TYPE_IF 0x4946 // 'IF'


/**
 * Écrit une chaîne suivie d'un retour à la ligne sur la sortie standard (stdout).
 */
void write_line(const char *line);

void print_str(const char *str);

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

/**
 * Affiche récursivement le contenu d'une structure command.
*/
void print_command(struct command *cmd, int indent);

/**
 * Sérialise une commande en chaîne de caractères (pour l'envoi au client).
 */
void serialize_command_to_buffer(struct command *cmd, char *buffer, size_t buf_size);

char* read_string_from_pipe(int fd);
// --- Affichage Formaté ---
void print_mask_list(uint64_t mask, int max_bits); 
void print_timing_complex(uint64_t min, uint32_t hour, uint8_t day);

