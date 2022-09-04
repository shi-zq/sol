#!/bin/bash

valgrind --leak-check=full ./server ./config1.txt &

SERVER_PID=$!

./client -p -t 200 -f ./socket -w ./test1,0

./client -p -t 200 -f ./socket -w ./test1,0 -l ./test1/primo.txt -u ./test1/primo.txt

./client -p -t 200 -f ./socket -w ./test1,0 -R 0 -d ./save

kill -s SIGHUP $SERVER_PID

wait $SERVER_PID

echo -e "========TEST 1 COMPLETED==========="