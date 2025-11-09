
# --- REPERTOIRES ---
SRC_DIR = src
BIN_DIR = bin

# --- SOURCES ---
ERRAID_SRCS = $(SRC_DIR)/erraid.c $(SRC_DIR)/utils.c
TADMOR_SRCS = $(SRC_DIR)/tadmor.c $(SRC_DIR)/utils.c


# --- REGLES ---

# Cible par défaut: compile les deux exécutables
all: $(BIN_DIR)/erraid $(BIN_DIR)/tadmor

# Règle pour créer le répertoire 'bin' s'il n'existe pas
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Règle pour erraid:
# Dépend des sources et du répertoire 'bin'
$(BIN_DIR)/erraid: $(ERRAID_SRCS) | $(BIN_DIR)
	gcc -o $@ $(ERRAID_SRCS)

# Règle pour tadmor:
# Dépend des sources et du répertoire 'bin'
$(BIN_DIR)/tadmor: $(TADMOR_SRCS) | $(BIN_DIR)
	gcc -o $@ $(TADMOR_SRCS)

# Nettoyage complet: efface le répertoire 'bin'
distclean:
	rm -rf $(BIN_DIR)

# Test: exécute erraid (après l'avoir compilé)
test: $(BIN_DIR)/erraid
	@echo "==> Lancement du démon erraid"
	@$(BIN_DIR)/erraid