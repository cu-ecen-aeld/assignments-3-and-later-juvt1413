CC=$(CROSS_COMPILE)gcc

:aesdsocket

aesdsocket: aesdsocket.o
	$(CC) -o aesdsocket aesdsocket.o
aesdsocket.o: aesdsocket.c
	$(CC) -c aesdsocket.c 
		
all: aesdsocket
	
clean: 
	rm -f aesdsocket *.o
