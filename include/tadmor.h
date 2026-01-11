#define TADMOR_H

#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

/**
 * Affiche l'aide si l'utilisateur se trompe.
 *
 */
void print_usage(const char *prog_name);

/**
 * Envoie une requête simple au démon via le tube nommé.
 * Gère l'ouverture, l'écriture et la fermeture du tube.
 *
 */
int send_request(const char *pipes_dir, uint16_t opcode, uint64_t taskid);

/**
 * Lit exactement 'count' octets depuis le descripteur de fichier.
 * Boucle tant que tout n'est pas lu.
 */
ssize_t read_full(int fd, void *buf, size_t count);

/**
 * Traite et affiche la réponse pour une commande LIST.
 */
void handle_list_reponse(int fd);

/**
 * Traite et affiche la réponse pour une demande d'historique d'exécution.
 */
void handle_runs_reponse(int fd);

/**
 * Traite et affiche la réponse pour une demande de sortie standard/erreur.
 */
void handle_output_reponse(int fd);

/**
 * Lit une réponse complète depuis le tube nommé de réponse.
 * Gère le dispatching de la demande grace au switch vers les fonctions d'affichage spécifiques.
 */
int read_response(const char *pipes_dir, uint16_t original_opcode);