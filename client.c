#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <errno.h>
#include <sys/types.h> 
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>

#define RETRY_TIME 5
#define UNIX_PATH_MAX 108
#define REQUEST 1024
#define DATA 1024

typedef struct commandNode {
    char operation;
    char* path;
    struct commandNode* next;
} commandNode;

commandNode* headCommand = NULL;
int clientSocket;
char* nameSocket;


void add(char operation, char* path) {
    commandNode* new = malloc(sizeof(commandNode));
    if(new == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    new->operation = operation;
    new->next = NULL;
    new->path = malloc((strlen(path) + 1) * sizeof(char));
    sprintf(new->path, "%s", path);
    if(headCommand == NULL) {
        headCommand = new;
    }
    else {
        commandNode* tmp = headCommand;
        while(tmp->next != NULL) {
            tmp = tmp->next;
        }
        tmp->next = new;
    }
}

int compareTime(struct timespec endTime){
    struct timespec currTime;
    clock_gettime(CLOCK_REALTIME, &currTime);
    if(currTime.tv_sec < endTime.tv_sec) {
        return 1;
    }
    if(currTime.tv_sec  == endTime.tv_sec) {
        if(currTime.tv_nsec < endTime.tv_nsec) {
            return 1;
        }
        else {
            return -1;
        }
    }
    return -1;
}

int openConnection(const char* socketName, int msec, const struct timespec endTime) {
    struct sockaddr_un sa;
    strncpy(sa.sun_path, socketName, UNIX_PATH_MAX);
    sa.sun_family = AF_UNIX;
    if((clientSocket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        return -1;
    }
    int connesso = 0;
    while (compareTime(endTime) == 1 && connesso == 0){
        if(connect(clientSocket, (struct sockaddr *)&sa,sizeof(sa)) != -1) {
            connesso = 1;
        }
        sleep(msec/1000);
    }
    if(connesso == 1) {
        nameSocket = malloc((strlen(socketName) + 1) * sizeof(char));
        if(nameSocket == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        strncpy(nameSocket, socketName, UNIX_PATH_MAX);
        return 0;
    }
    else {
        return -1;
    }
}

int closeConnection(const char* sockname) {
    char request[REQUEST];
    char answer[REQUEST];
    if(strcmp(nameSocket, sockname) == 0) {
        sprintf(request, "c");
        if(write(clientSocket, request, REQUEST) == -1) {
            return -1;
        }
        if(read(clientSocket, answer, REQUEST) == -1) {
            return -1;
        }
        if(atoi(answer) != 0) {
            return -1;
        }
        close(clientSocket);
        return 0;
    }
    else{
        return -1;
    }
}

int main(int argc, char *argv[]) {
    char opt = 0;
    int connected = 0;
    while((opt = (char)getopt(argc, argv, "hf:W:w:D:d:r:R:l:u:c:t:p")) != -1) {
        switch (opt) {
            case('h'):
                printf("help \n");
                break;
            case('f'):
                struct timespec abstime;
                clock_gettime(CLOCK_REALTIME, &abstime);
                abstime.tv_sec = abstime.tv_sec + RETRY_TIME; 
    			if(openConnection(optarg, 1000, abstime) == 0) {
                    connected = 1;
		    	}
			    else {
                    perror("openConnection");
                    exit(EXIT_FAILURE);
			    }
			    break;
        }
    }
    if(connected == 1) {
        while(headCommand != NULL) {
            headCommand = headCommand->next;
        }
        if(closeConnection(nameSocket) == -1) {
            printf("errore nel closeConnection \n");
            exit(EXIT_FAILURE);
        }
    }


}