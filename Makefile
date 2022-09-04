CC 			= gcc
CFLAGS		= -g -Wall 
TARGETS		= server client
.PHONY : clean test1 test2

all : $(TARGETS)

server : server.c
	$(CC) $(CFLAGS) server.c -o server 

client : client.c 
	$(CC) $(CFLAGS) client.c -o client 

clean: 
	rm $(TARGETS) socket log.txt

test1: $(TARGETS)
	chmod +x ./test1.sh
	./test1.sh
	chmod +x ./statistica.sh
	./statistica.sh

test2: $(TARGET)
	chmod +x ./test2.sh
	./test2.sh
	chmod +x ./statistica.sh
	./statistica.sh