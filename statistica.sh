#!/bin/bash
LOG=./log.txt

#parte write
writes=$(grep -c "op=writeFile" "$LOG")
appends=$(grep -c "op=appendToFile" "$LOG")
echo "WRITE TOTALI: $writes"
echo "APPEND TOTALI: $appends"
declare -i tot=0
wsum=$(grep "op=writeFile" "$LOG" | grep "answer=0" | cut -d ';' -f4 | cut -d '=' -f2 )
asum=$(grep "op=appendToFile" "$LOG" | grep "answer=0" | cut -d ';' -f4 | cut -d '=' -f2 )
wtimes=$(grep "op=writeFile" "$LOG" | grep -c "answer=0")
atimes=$(grep "op=appendToFile" "$LOG" | grep -c "answer=0")
for num in $wsum
    do 
        tot=$(($tot + $num))
    done
for num in $asum
    do 
        tot=$(($tot + $num))
    done
times=$(($wtimes+$atimes))
if [ "$times" -eq "0" ]; then
echo "write byte totale: $tot"
echo "media byte write: 0";
else
echo "write byte totale: $tot"
media=$(echo "scale=2; ${tot} / ${times}" | bc -l)
echo "media byte write: " $media
fi

#parte read
reads=$(grep -c "op=read" "$LOG")
echo "READ totali: $reads"
declare -i tot=0
declare -i times=0
sum=$(grep "op=read" "$LOG" | grep "answer=0" | cut -d ';' -f3 | cut -d '=' -f2 )
times=$(grep "op=read" "$LOG" | grep -c "answer=0")
for num in $sum
    do 
        tot=$(($tot + $num))
    done

if [ "$times" -eq "0" ]; then
echo "read byte totale: $tot"
echo "media byte read: 0";
else
echo "read byte totale: $tot"
media=$(echo "scale=2; ${tot} / ${times}" | bc -l)
echo "media byte read: " $media
fi

#parte lock
locks=$(grep -c "op=lockFile" "$LOG")
echo "LOCK totali: $locks"

#parte unlock
unlocks=$(grep -c "op=unlockFile" "$LOG")
echo "UNLOCK totali: $unlocks"

#parte openlock
openlocks=$(grep -c "op=openFile" "$LOG")
echo "OPEN-LOCK totali: $openlocks"

#parte close
closes=$(grep -c "op=closeFile" "$LOG")
echo "CLOSE totali: $closes"
#parte out
outs=$(grep -c "op=out" "$LOG")
echo "OUT totali: $outs"

#MB raggiunti file raggiunti connessioni raggiunti
declare -i bytes_balance=0;
declare -i max_bytes=0;
declare -i files_balance=0;
declare -i max_files=0;
declare -i connections=0;
declare -i max_connections=0;
while read -r line
do
    op=$(echo "$line" | cut -d ';' -f2 | cut -d '=' -f2)
    case "$op" in 
        "writeFile")
            add_bytes=$(echo "$line" | cut -d ';' -f4 | cut -d '=' -f2)
            bytes_balance=$((bytes_balance+add_bytes))
            files_balance=$((files_balance+1))
            if [ "$bytes_balance" -gt "$max_bytes" ]; then
                max_bytes=$bytes_balance
            fi
            if [ "$files_balance" -gt "$max_files" ]; then
                max_files=$files_balance
            fi 
            ;;
        "appendToFile")
            add_bytes=$(echo "$line" | cut -d ';' -f4 | cut -d '=' -f2)
            bytes_balance=$((bytes_balance+add_bytes))
            if [ "$bytes_balance" -gt "$max_bytes" ]; then
                max_bytes=$bytes_balance
            fi
            ;;
        "out")
            add_bytes=$(echo "$line" | cut -d ';' -f4 | cut -d '=' -f2)
            bytes_balance=$((bytes_balance-add_bytes))
            files_balance=$((files_balance-1))
            ;;
        "openConnection")
            connections=$((connections+1))
            if [[ "$connections" -gt "$max_connections" ]]; then
                max_connections=$connections
            fi
            ;;
        "closeConnection")
            connections=$((connections-1))
            ;;
        
    esac
done < "$LOG"
echo "MAX BYTE HIT: $max_bytes"
echo "MAX FILE HIT:$max_files"
echo "MAX CONNECTION HIT: $max_connections"

#lavoro di ogni thread worker
thread_ids=$(grep -v "thread=main" "$LOG" | sort | cut -d ';' -f1 | uniq -c)
echo "$thread_ids"
