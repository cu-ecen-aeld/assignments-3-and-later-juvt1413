CC?=gcc
CFLAGS?=-g -Wall -pthread
LDFLAGS?=-pthread

all: aesdsocket

aesdsocket: aesdsocket.o
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.o $(LDFLAGS)

aesdsocket.o: aesdsocket.c
	$(CC) $(CFLAGS) -c aesdsocket.c

clean:
	rm -f aesdsocket *.o
