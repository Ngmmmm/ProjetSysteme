SRC_DIR = src
BIN_DIR = bin
TEST_DIR = /tmp/$(USER)/erraid

all: $(BIN_DIR)/erraid #$(BIN_DIR)/tadmor

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/erraid: $(SRC_DIR)/erraid.c | $(BIN_DIR)
	gcc -Wall -Wextra -o $@ $<

$(BIN_DIR)/tadmor: $(SRC_DIR)/tadmor.c | $(BIN_DIR)
	gcc -Wall -Wextra -o $@ $<

# Nettoyage complet
distclean:
	rm -rf $(BIN_DIR)

# --- SECTION TEST ---
# Crée un environnement temporaire et exécute erraid
test: all
#	@echo "==> Création de l'environnement de test dans $(TEST_DIR)"
#	@mkdir -p $(TEST_DIR)/tasks/001/cmd
#	@echo "Tâche factice créée : $(TEST_DIR)/tasks/001"
#	@echo "==> Lancement du démon erraid"
	@$(BIN_DIR)/erraid $(TEST_DIR)
#	@echo "==> Test terminé"

