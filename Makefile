SRC_DIR = src
BIN_DIR = .


ERRAID_DIR = $(SRC_DIR)/erraid
TADMOR_DIR = $(SRC_DIR)/tadmor
COMMON_DIR = $(SRC_DIR)/common


ERRAID_SRCS = $(ERRAID_DIR)/erraid.c \
              $(ERRAID_DIR)/globals.c \
              $(ERRAID_DIR)/task_loader.c \
              $(ERRAID_DIR)/task_executor.c \
              $(ERRAID_DIR)/server_comm.c \
              $(COMMON_DIR)/utils.c
			  
TADMOR_SRCS = $(TADMOR_DIR)/tadmor.c \
              $(TADMOR_DIR)/client_comm.c \
              $(TADMOR_DIR)/response_handlers.c \
              $(COMMON_DIR)/utils.c
TEST_DIR = /tmp/$(USER)/erraid

# Cibles principales
all: $(BIN_DIR)/erraid $(BIN_DIR)/tadmor

# Règle pour créer le répertoire 'bin' s'il n'existe pas
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Règle pour erraid:
$(BIN_DIR)/erraid: $(ERRAID_SRCS) | $(BIN_DIR)
	gcc -Wno-format-truncation -I include -o $@ $(ERRAID_SRCS)
	@echo "==> Nettoyage et création de l'environnement de test dans $(TEST_DIR)"


# Règle pour tadmor:
$(BIN_DIR)/tadmor: $(TADMOR_SRCS) | $(BIN_DIR)
	gcc -Wno-format-truncation -I include -o $@ $(TADMOR_SRCS)


	
# Nettoyage
distclean:
	rm -f $(BIN_DIR)/erraid $(BIN_DIR)/tadmor

# --- Zone de Test ---


test: distclean all
	@echo "==> Nettoyage et création de l'environnement de test dans $(TEST_DIR)"
	@rm -rf $(TEST_DIR)
	@mkdir -p $(TEST_DIR)
	
	@echo "==> Copie des exemples d'arborescences..."
	@cp -r consignes/exemples-arborescences/exemple-arborescence-1/tmp-username-erraid/* $(TEST_DIR) 
	@cp -r consignes/exemples-arborescences/exemple-arborescence-2/tmp-username-erraid/* $(TEST_DIR) 
	@cp -r consignes/exemples-arborescences/exemple-arborescence-3/tmp-username-erraid/* $(TEST_DIR) 
	@cp -r consignes/exemples-arborescences/exemple-arborescence-4/tmp-username-erraid/* $(TEST_DIR) 
	
	

