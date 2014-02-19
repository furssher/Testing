OBJS = host.o
LIBS = -lusb-1.0
CC   = gcc

stb_host : $(OBJS)
	$(CC) $(LIBS) -o stb_host $(OBJS)

host.o : host.c
	$(CC) -c $(LIBS) host.c 

.PHONY : clean
clean : 
	rm -f *.o stb_host
