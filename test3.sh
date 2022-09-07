#!/bin/bash

./server ./config3.txt &

SERVER_PID=$!

(sleep 30; kill -s SIGINT $SERVER_PID)&

chmod +x ./client

sleep 2
#controlla il suo exit status di ps -p
while ps -p $SERVER_PID > /dev/null
    do
    ./client -t 0 -f ./socket -w ./test5,0 &
    ./client -t 0 -f ./socket -r ./test5/a.txt -r ./test5/b.txt -r ./test5/c.txt -r ./test5/d.txt -r ./test5/e.txt &
    sleep 0.1
    done

wait $SERVER_PID