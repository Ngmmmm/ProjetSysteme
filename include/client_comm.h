#ifndef CLIENT_COMM_H
#define CLIENT_COMM_H

#include <stdint.h>

// Envoie la requête (Opcode + TaskID optionnel)
int send_request(const char *pipes_dir, uint16_t opcode, uint64_t taskid);

// Envoie une requête de création de tâche simple (-c)
int send_create_request(const char *pipes_dir, uint64_t minutes, uint32_t hours, uint8_t day, int argc, char **argv);

// Lit la réponse et appelle le bon handler selon l'opcode d'origine
int read_response(const char *pipes_dir, uint16_t original_opcode);

// Envoie une requête de combinaison : id_src sera déplacé dans id_dest
int send_combine_request(const char *pipes_dir, uint64_t id_dest, uint64_t id_src);

#endif