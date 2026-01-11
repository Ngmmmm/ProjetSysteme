 # Architecture du Projet Erraid & Tadmor

Ce projet implémente un système de planification de tâches sous Linux, composé d’un démon serveur (**erraid**) et d’un client en ligne de commande (**tadmor**).

---

## 1. Vue d’Ensemble

L’architecture suit un modèle **Client–Serveur local** communiquant via des **tubes nommés (Named Pipes / FIFO)**.

### Erraid (Serveur)

Démon tournant en arrière-plan (sauf si l'option `-F` est utilisée), responsable de :

- Chargement de l’arborescence des tâches au démarrage
- Planification temporelle (scheduler) avec précision à la minute
- Écoute des requêtes client via multiplexage (`select`)
- Exécution des tâches et journalisation des sorties

### Tadmor (Client)

Interface en ligne de commande permettant à l’utilisateur de :

- Envoyer des requêtes formatées au démon
- Consulter les tâches, logs et historiques
- Afficher les réponses du démon de manière lisible

---

## 2. Communication Inter-Processus (IPC)

La communication repose sur **deux fichiers FIFO**, situés dans le répertoire `pipes/` de l’arborescence de travail  
(par défaut : `/tmp/$USER/erraid/pipes`).

### Tubes utilisés

- **`erraid-request-pipe`** *(Client → Serveur)*  
  Utilisé par `tadmor` pour envoyer les commandes (Opcode + Arguments)

- **`erraid-reply-pipe`** *(Serveur → Client)*  
  Utilisé par `erraid` pour renvoyer les données (liste des tâches, logs, erreurs, etc.)

### Protocole

- Protocole **binaire**
- Encodage **Big-Endian** pour tous les entiers (16, 32, 64 bits)
- Garantit la portabilité entre architectures

---

## 3. Structures de Données

Les structures principales sont définies dans `include/erraid.h` et modélisent les tâches en mémoire.

### `struct task`

- Identifiant unique de la tâche
- Structure de planification temporelle
- Pointeur vers la commande racine

### `struct timing`

- Stocke la planification sous forme de **masques de bits**
- Champs :
  - minutes
  - heures
  - jours

### `struct command`

Structure récursive représentant l’arbre d’exécution d’une tâche.

#### Types de commandes

- **SI (Simple)**  
  - Contient `argc` et `argv`
  - Exécution directe via `execvp`

- **SQ (Séquence)**  
  - Tableau de sous-commandes (`subcommands`)
  - Exécution séquentielle

- **PL (Pipeline)**  
  - Tableau de sous-commandes
  - Exécution parallèle avec redirection des flux (`pipe()`) : la sortie de la commande N devient l'entrée de la commande N+1.

- **IF (Conditionnelle)**  
  - Contient une condition (commande test), une branche `then` et une branche `else` (optionnelle).
  - Exécute la branche `then` si la condition réussit (exit code 0), sinon la branche `else`.


---

## 4. Fonctionnement du Démon (Erraid)

### Initialisation

- Création de l’arborescence de travail
- Création des tubes nommés (`mkfifo`)
- Chargement complet des tâches depuis le disque (`task_loader`)
- Synchronisation initiale :
  - Attente du début précis de la minute suivante
  - Alignement sur l’horloge système

---

### Boucle Principale (Scheduler)

Le cœur du démon repose sur une boucle infinie utilisant `select()`.

#### Gestion du Temps

- Le timeout de `select` est calculé dynamiquement
- Réveil à la seconde `00` de chaque minute

#### Gestion des Requêtes

- Surveillance du descripteur du tube de requête
- Traitement immédiat dès qu’une requête client arrive

---

### Logique d’Exécution

- Une variable `last_min` empêche l’exécution multiple d’une tâche sur la même minute
- Avant chaque exécution :
  - Troncature des fichiers de logs (`stdout`, `stderr`) avec `O_TRUNC`
- Exécution via :
  - `fork()`
  - `execvp()`

#### Cas des Séquences

- Exécution strictement séquentielle :
  - La commande `N+1` attend la fin de `N`
- Les fichiers de logs sont ouverts en **`O_APPEND`**
- Garantit la conservation complète des sorties intermédiaires

---

## 5. Fonctionnement du Client (Tadmor)

### Parsing

- Analyse des options (`-l`, `-o`, `-x`, etc.) avec `getopt`

### Sérialisation

- Construction d’un paquet binaire :
  - Opcode
  - Identifiant de tâche (si nécessaire)
  - Arguments éventuels

### Envoi

- Ouverture du tube de requête
- Écriture du paquet
- Blocage automatique si le démon n’est pas actif

### Réception

- Ouverture du tube de réponse
- Lecture du header (`OK` / `ERR`)
- Désérialisation des données :
  - Nombre de tâches
  - Chaînes de caractères
  - Historiques
- Affichage sur la sortie standard

---

## 6. Choix d’Implémentation

### Multiplexage I/O

- Utilisation de `select()` pour :
  - Réagir aux requêtes client
  - Attendre efficacement le prochain tick temporel

### Robustesse Temporelle

- Recalage systématique sur le début de la minute suivante
- Évite toute dérive temporelle à long terme

### Gestion des Logs

- Utilisation de `O_APPEND` dans les processus fils
- Garantit une redirection correcte pour les commandes composées
- Compatible avec les séquences et futures extensions (pipelines)

---
