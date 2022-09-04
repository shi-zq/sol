#!/bin/bash

valgrind --leak-check=full ./server ./config.txt &

SERVER_PID=$!

sleep 5s

./client -f ./socket -w ./test,3

./client -f ./socket -R 3 -d ./save

kill -s SIGHUP $SERVER_PID

wait $SERVER_PID