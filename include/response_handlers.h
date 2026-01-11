#ifndef RESPONSE_HANDLERS_H
#define RESPONSE_HANDLERS_H


/**
 * Désérialise une commande depuis un descripteur et l’affiche
 */
void deserialize_and_print_cmd(int fd, int is_root);

/**
 * LIST
 * Affiche la liste des tâches planifiées
 */
void handle_list_reponse(int fd);

/**
 * RUNS
 * Affiche l’historique des exécutions
 */
void handle_runs_reponse(int fd);

/**
 * OUTPUT
 * Écrit la sortie brute sur stdout
 */
void handle_output_reponse(int fd);

#endif
