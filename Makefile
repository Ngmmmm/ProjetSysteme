SRC_DIR = src

all: erraid tadmor

erraid: $(SRC_DIR)/erraid.c
	gcc -o erraid $(SRC_DIR)/erraid.c

tadmor: $(SRC_DIR)/tadmor.c
	gcc -o tadmor $(SRC_DIR)/tadmor.c

distclean:
	rm -f erraid tadmor
