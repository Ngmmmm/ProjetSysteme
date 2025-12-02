SRC_DIR = src
BIN_DIR = .

ERRAID_SRCS = $(SRC_DIR)/erraid.c $(SRC_DIR)/utils.c
TADMOR_SRCS = $(SRC_DIR)/tadmor.c $(SRC_DIR)/utils.c
TEST_DIR = /tmp/$(USER)/erraid

# Cibles principales
all: $(BIN_DIR)/erraid $(BIN_DIR)/tadmor

# Règle pour créer le répertoire 'bin' s'il n'existe pas
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Règle pour erraid:
$(BIN_DIR)/erraid: $(ERRAID_SRCS) | $(BIN_DIR)
	gcc -Wno-format-truncation -o $@ $(ERRAID_SRCS)

# Règle pour tadmor:
$(BIN_DIR)/tadmor: $(TADMOR_SRCS) | $(BIN_DIR)
	gcc -Wno-format-truncation -o $@ $(TADMOR_SRCS)

	@echo "==> Nettoyage et création de l'environnement de test dans $(TEST_DIR)"
	@rm -rf $(TEST_DIR)
	@mkdir -p $(TEST_DIR)
	
# Nettoyage
distclean:
	rm -f $(BIN_DIR)/erraid $(BIN_DIR)/tadmor

# --- Zone de Test ---


test: distclean all
	@echo "==> Nettoyage et création de l'environnement de test dans $(TEST_DIR)"
	@rm -rf $(TEST_DIR)
	@mkdir -p $(TEST_DIR)
	
	@echo "==> Copie des exemples d’arborescences..."
	@cp -r consignes/exemples-arborescences/exemple-arborescence-1/tmp-username-erraid/* $(TEST_DIR) 
	@cp -r consignes/exemples-arborescences/exemple-arborescence-2/tmp-username-erraid/* $(TEST_DIR) 
	@cp -r consignes/exemples-arborescences/exemple-arborescence-3/tmp-username-erraid/* $(TEST_DIR) 
	@cp -r consignes/exemples-arborescences/exemple-arborescence-4/tmp-username-erraid/* $(TEST_DIR) 
	
	

