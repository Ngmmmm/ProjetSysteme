SRC_DIR = src
BIN_DIR = bin

all: $(BIN_DIR)/erraid $(BIN_DIR)/tadmor

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/erraid: $(SRC_DIR)/erraid.c | $(BIN_DIR)
	gcc -Wall -Wextra -o $@ $<

$(BIN_DIR)/tadmor: $(SRC_DIR)/tadmor.c | $(BIN_DIR)
	gcc -Wall -Wextra -o $@ $<

distclean:
	rm -rf $(BIN_DIR)
.