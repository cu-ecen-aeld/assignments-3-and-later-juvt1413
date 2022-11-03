CC=$(CROSS_COMPILE)gcc

:writer

writer: writer.o
	$(CC) -o writer writer.o
writer.o: writer.c
	$(CC) -c writer.c
		
all: writer
	
clean: 
	rm -f writer *.o
