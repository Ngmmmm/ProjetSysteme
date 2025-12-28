#ifndef CLIENT_COMM_H
#define CLIENT_COMM_H

#include <stdint.h>

// Envoie la requête (Opcode + TaskID optionnel)
int send_request(const char *pipes_dir, uint16_t opcode, uint64_t taskid);

// Lit la réponse et appelle le bon handler selon l'opcode d'origine
int read_response(const char *pipes_dir, uint16_t original_opcode);

#endif