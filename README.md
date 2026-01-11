# Projet Système : Erraid & Tadmor

**Erraid** est un démon de planification de tâches (similaire à `cron`) et **Tadmor** est son client en ligne de commande permettant de contrôler et consulter le démon.

---

## Auteurs

- **POTIER Nathan**
- **GREGOIRE-MELLIER Noa**
- **RAMANATHAN Jonas**

---

## Compilation

Le projet utilise un **Makefile** à la racine.

### Compiler le projet  
(Génère les exécutables `erraid` et `tadmor` à la racine)

```bash
make
```


### Lancer la suite de tests automatiques

```bash
./test.sh
```
### Nettoyer les fichiers générés

```bash
make distclean
```

---

##  Utilisation

### 1. Démarrage du démon (Erraid)

Le démon doit être lancé en premier.
Il initialise l’arborescence de stockage (par défaut dans `/tmp/$USER/erraid`) et crée les tubes de communication.

```bash
./erraid
```

#### Options

* `-R <répertoire>` : Spécifier un répertoire racine personnalisé pour stocker les tâches et les logs.

* `-F` : Forcer l'exécution en avant-plan (foreground), utile pour le débogage (ne détache pas le processus).
---

### 2. Utilisation du client (Tadmor)

Une fois le démon lancé, utilisez **tadmor** pour interagir avec lui.

---

## Consultation

### Lister les tâches

```bash
./tadmor -l
```

### Voir la sortie standard de la dernière exécution

```bash
./tadmor -o <ID_TACHE>
```

### Voir la sortie d’erreur standard

```bash
./tadmor -e <ID_TACHE>
```

### Voir l’historique des exécutions

(dates et codes de retour)

```bash
./tadmor -x <ID_TACHE>
```

---

## Gestion des tâches

### Créer une tâche simple

```bash
./tadmor -c -m <minutes> -H <heures> -d <jours> <commande> [args...]
```

#### Exemple

```bash
./tadmor -c -m 30 -H 8 echo "Réveil"
```

Exécute `echo Réveil` tous les jours à **8h30**.

>  **Note :** Les masques temporels sont des entiers (bitmasks).
---

### Supprimer une tâche

```bash
./tadmor -r <ID_TACHE>
```

---

## Combinaison de tâches

### Séquence

(dst ; src)

```bash
./tadmor -s <ID_DEST> <ID_SRC>
```

### Pipeline

(dst | src)

```bash
./tadmor -p <ID_DEST> <ID_SRC>
```


### Conditionnel

(if dst then src)

```bash
./tadmor -i <ID_DEST> <ID_SRC>
```

---

## Fonctionnalités Implémentées

* Architecture **Client–Serveur robuste** via tubes nommés
* **Persistance** : sauvegarde des tâches sur disque et rechargement au redémarrage
* **Planification complète** : minutes, heures et jours de la semaine
* **Commandes complexes** : tâches simples, séquences, pipelines et conditionnelles
* **Logs** : capture de `stdout`, `stderr` et des codes de retour
* **Multiplexage** : utilisation de `select()` pour gérer simultanément l’horloge et les requêtes client sans blocage actif

---

## Structure du Projet

```
├──src/        Code source (.c)
│  ├──erraid/     Code spécifique au démon (chargement, exécution, serveur)
│  ├──tadmor/     Code spécifique au client
│  └──common/     Fonctions utilitaires partagées
├──include/    Fichiers d’en-tête (.h)
├──consignes/  Sujet et documentation du protocole
```
