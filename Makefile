CC 			= gcc
CFLAGS		= -g -Wall -pedantic
TARGETS		= server client
.PHONY : clean test1 test2 test3

all : $(TARGETS)

server : server.c
	$(CC) $(CFLAGS) server.c -o server -lpthread

client : client.c 
	$(CC) $(CFLAGS) client.c -o client 

clean: 
	rm -f $(TARGETS) socket log.txt
	rm -f ./save/*

test1:
	mkdir -p ./save
	rm -f socket
	chmod +x ./test1.sh
	./test1.sh
	chmod +x ./statistica.sh
	./statistica.sh

test2:
	mkdir -p ./save
	rm -f socket
	chmod +x ./test2.sh
	./test2.sh
	chmod +x ./statistica.sh
	./statistica.sh

test3:
	mkdir -p ./save
	rm -f socket
	chmod +x ./test3.sh
	./test3.sh
	chmod +x ./statistica.sh
	./statistica.sh