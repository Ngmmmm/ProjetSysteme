#ifndef CLIENT_COMM_H
#define CLIENT_COMM_H

#include <stdint.h>

// Envoie la requête (Opcode + TaskID optionnel)
int send_request(const char *pipes_dir, uint16_t opcode, uint64_t taskid);

// Envoie une requête de création de tâche simple (-c)
int send_create_request(const char *pipes_dir, struct timing t, int argc, char **argv);

// Lit la réponse et appelle le bon handler selon l'opcode d'origine
int read_response(const char *pipes_dir, uint16_t original_opcode);

#endif