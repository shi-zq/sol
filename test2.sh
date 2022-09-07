#!/bin/bash

./server ./config2.txt &

SERVER_PID=$!

./client -p -t 200 -f ./socket -w ./test1,0
./client -p -t 200 -f ./socket -w ./test2,0
./client -p -t 200 -f ./socket -w ./test3,0 
./client -p -t 200 -f ./socket -w ./test4,0 

kill -s SIGHUP $SERVER_PID

wait $SERVER_PID
