
# --- REPERTOIRES ---
SRC_DIR = src
BIN_DIR = bin

# --- SOURCES ---
ERRAID_SRCS = $(SRC_DIR)/erraid.c $(SRC_DIR)/utils.c
TADMOR_SRCS = $(SRC_DIR)/tadmor.c $(SRC_DIR)/utils.c
TEST_DIR = /tmp/$(USER)/erraid


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
test: distclean $(BIN_DIR)/erraid
	@echo "==> Création de l'environnement de test dans $(TEST_DIR)"
	@mkdir -p $(TEST_DIR)
	@echo "==> Copie des exemples d’arborescences..."
	@cp -r consignes/exemples-arborescences/exemple-arborescence-1/tmp-username-erraid/* $(TEST_DIR) 
	@cp -r consignes/exemples-arborescences/exemple-arborescence-2/tmp-username-erraid/* $(TEST_DIR) 
	@cp -r consignes/exemples-arborescences/exemple-arborescence-3/tmp-username-erraid/* $(TEST_DIR) 
	@cp -r consignes/exemples-arborescences/exemple-arborescence-4/tmp-username-erraid/* $(TEST_DIR) 
	@echo "==> Exécution du démon erraid sur $(TEST_DIR)"
	@$(BIN_DIR)/erraid $(TEST_DIR)
	@echo "==> Test terminé."

	@echo "============================================="
	@echo "==> VÉRIFICATION DES LOGS stdout/stderr <=="
	@echo "============================================="
	@find $(TEST_DIR)/tasks \( -name "stdout" -o -name "stderr" \) -exec echo "--- Contenu de {} ---" \; -exec cat {} \;
	@echo "============================================="