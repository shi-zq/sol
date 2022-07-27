CC 			= gcc
CFLAGS		= -g -Wall 
TARGETS		= server client
.PHONY : clean test1

all : $(TARGETS)

server : server.c
	$(CC) $(CFLAGS) server.c -o server 

client : client.c 
	$(CC) $(CFLAGS) client.c -o client 

clean: 
	rm $(TARGETS) socket log.txt

test1:
