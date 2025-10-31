all: erraid tadmor

erraid: erraid.c
	gcc -o erraid erraid.c

tadmor: tadmor.c
	gcc -o tadmor tadmor.c

distclean:
	rm -f erraid tadmor