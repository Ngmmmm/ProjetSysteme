#ifndef SERVER_COMM_H
#define SERVER_COMM_H

#include "../include/erraid.h"
#include <stddef.h>

/**
 * Initialise les pipes de communication client ↔ serveur.
 * Crée les FIFO et ouvre le pipe de requêtes.
 */
void init_communication();

/**
 * Traite une requête client lue sur le pipe.
 * Peut modifier la liste des tâches (ajout, suppression, rechargement).
 */
void handle_client_request(int req_fd, struct task **tasks, size_t *task_count);

#endif