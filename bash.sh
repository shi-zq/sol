#!/bin/bash

echo "inizio"

if [ -f "./log.txt" ]; then
    echo "exists"
else
    echo "file not exists"
    exit 0
fi

declare -i sum=0;
number_of_total_reads=$(grep -c "openFilenode" "./log.txt")
echo "read totale" "$number_of_total_reads"