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
#define O_CREATE 1
#define O_LOCK 2

typedef struct commandNode {
    char operation;
    char* path;
    struct commandNode* next;
} commandNode;

commandNode* headCommand = NULL;
int clientSocket;
char* nameSocket = NULL;
char* dirD = NULL;

int setErrno(int result) {
    printf("result = %d \n", result);
    return result;
}

void add(char operation, char* path) {
    commandNode* new = malloc(sizeof(commandNode));
    if(new == NULL) {
        perror("add.malloc");
        exit(EXIT_FAILURE);
    }
    new->operation = operation;
    new->next = NULL;
    new->path = malloc((strlen(path) + 1) * sizeof(char));
    if(new->path == NULL) {
        perror("add.malloc");
        exit(EXIT_FAILURE);
    }
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
    int result = -1;
    struct sockaddr_un sa;
    strncpy(sa.sun_path, socketName, UNIX_PATH_MAX);
    sa.sun_family = AF_UNIX;
    if((clientSocket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        return setErrno(result);
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
            perror("openConnecton.malloc");
            exit(EXIT_FAILURE);
        }
        sprintf(nameSocket, "%s", socketName);
        return setErrno(0);
    }
    else {
        return setErrno(result);
    }
}

int closeConnection(const char* sockname) {
    char request[REQUEST];
    char answer[REQUEST];
    memset(request, 0, REQUEST);
    memset(answer, 0, REQUEST);
    int result = -1;
    if(strcmp(nameSocket, sockname) == 0) {
        sprintf(request, "C");
        if(write(clientSocket, request, REQUEST) == -1) {
            return setErrno(result);
        }
        if(read(clientSocket, answer, REQUEST) == -1) {
            return setErrno(result);
        }
        result = atoi(answer);
        if(result != 0) {
            return setErrno(result);
        }
        free(nameSocket);
        close(clientSocket);
        return setErrno(result);
    }
    else{
        return setErrno(result);
    }
}

int openFile(const char* pathname, int flags) {
    char* absPath = NULL;
    int result = -1;
    absPath = realpath(pathname, absPath);
    if(absPath == NULL) {
        return -1;
    }
    if(strlen(absPath) > 1024) {
        result = -11;
        return setErrno(result);
    }
    else {
        char request[REQUEST];
        memset(request, 0, REQUEST);
        char answer[REQUEST];
        memset(answer, 0, REQUEST);
        sprintf(request, "o;%s;%d", absPath, flags);
        free(absPath);
        if(write(clientSocket, request, REQUEST) == -1) {
            return setErrno(result);
        }
        if(read(clientSocket, answer, REQUEST) == -1) {
            return setErrno(result);
        }
        result = atoi(answer);
        return setErrno(result);
    }
}

int writeFile(const char* pathname, const char* dirname) {
    int result = -1;
    FILE *fp;
    char* absPath = NULL;
    absPath = realpath(pathname, absPath);
    if ((fp = fopen(pathname, "r")) == NULL) {
        return setErrno(result);
    }
    char data[DATA];
    memset(data, 0, DATA);
    data[0] = '\0';
    char buffer[DATA];
    memset(buffer, 0, DATA);
    while (fgets(buffer, DATA, fp) != NULL) {
        if(strlen(buffer) + strlen(data) + 1 < DATA) {
            strcat(data, buffer);
        }
        else {
            result = -12;
            return setErrno(result);
        }
    }
    fclose(fp);
    char request[REQUEST + DATA];
    memset(request, 0, REQUEST + DATA);
    sprintf(request, "w;%s;%s", absPath, data);
    free(absPath);
    if(write(clientSocket, request, REQUEST + DATA) == -1) {
        return setErrno(-1);
    }
    char answer[REQUEST];
    memset(answer, 0, REQUEST);
    if(read(clientSocket, answer, REQUEST) == -1) {
        return setErrno(-1);
    }
    result = atoi(answer);
    char outFile[REQUEST + DATA];
    memset(outFile, 0, REQUEST + DATA);
    char filePath[DATA];
    memset(filePath, 0, DATA);
    char* fileName = NULL;
    if(result >= 0) {
        int save;
        if(dirname == NULL) {
            save = 0;
        }
        else {
            save = 1;
        }
        while(result > 0) {
            memset(outFile, 0, REQUEST + DATA);
            memset(filePath, 0, DATA);
            if(read(clientSocket, filePath, REQUEST) == -1) { //leggere il nome del file
                perror("writeFile.read");
                exit(EXIT_FAILURE);
                return setErrno(-1);
            }
            if(read(clientSocket, outFile, DATA) == -1) { //leggere il nome del file
                perror("writeFile.read");
                exit(EXIT_FAILURE);
                return setErrno(-1);
            }
            if(save == 1) {
                fileName = NULL;
                sprintf(filePath, "%s/%s", dirname, basename(fileName));
                FILE* file = fopen(filePath, "w");
                if(file == NULL) {
                    perror("writeFile.fopen");
                    exit(EXIT_FAILURE);
                }
                fprintf(file, "%s", outFile);
            }
        }
        return setErrno(result); //dovrebbe essere 0
    }
    else {
        return setErrno(result);
    }
}

int closeFile(const char* pathname) {
    char* absPath = NULL;
    int result = -1;
    absPath = realpath(pathname, absPath);
    if(strlen(absPath) > 1024) {
        result = -11;
        return setErrno(result);
    }
    else {
        char request[REQUEST];
        memset(request, 0, REQUEST);
        char answer[REQUEST];
        memset(answer, 0, REQUEST);
        sprintf(request, "c;%s", absPath);
        free(absPath);
        if(write(clientSocket, request, REQUEST) == -1) {
            return setErrno(result);
        }
        if(read(clientSocket, answer, REQUEST) == -1) {
            return setErrno(result);
        }
        result = atoi(answer);
        return setErrno(result);
    }
}

int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname) {

}

int main(int argc, char *argv[]) {
    char opt = 0;
    int connected = 0;
    int t = 0;
    int p = 1;
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
                printf("connesso \n");
			    break;
            case('t'):
                t = atoi(optarg);
                break;
            case('p'):
                p = 0;
                break;
            case('W'):
                char* tmp = NULL;
                char* token = strtok_r(optarg, ",", &tmp);
                while(token != NULL) {
                    add('w', token);
                    token = strtok_r(NULL, ",", &tmp);
                }
                break;
            case('D'):
                DIR* dir = opendir(optarg);
                if(dir == NULL) {
                    perror("main.openDir");
                    exit(EXIT_FAILURE);
                }
                else {
                    dirD = malloc((strlen(optarg) + 1) * sizeof(char));
                    if(dirD == NULL) {
                        perror("main.malloc");
                        exit(EXIT_FAILURE);
                    }
                    sprintf(dirD, "%s", optarg);
                }
                break;
            default:
                printf("default \n");
                break;
        }
    }
    printf("inizio inviare commandi\n");
    if(connected == 1) {
        while(headCommand != NULL) {
            int result;
            printf("%c, %s \n", headCommand->operation, headCommand->path);
            switch(headCommand->operation) {
                case('w'):
                    result = openFile(headCommand->path, O_CREATE | O_LOCK);
                    if(p == 1) {
                        printf("openFile(%s, %d) : %d \n", headCommand->path, O_CREATE | O_LOCK, result);
                    }
                    if(result == 0) {
                        result = writeFile(headCommand->path, dirD);
                        if(p == 1) {
                            printf("writeFile(%s, %s) : %d \n", headCommand->path, dirD, result);
                        }
                        if(result == 0) {
                            result = closeFile(headCommand->path);
                            if(p == 1) {
                                printf("closeFile(%s) : %d \n", headCommand->path, result);
                            }
                            if(result == 0) {
                            }
                            else {
                                perror("main.closeFile");
                            }
                        }
                        else {
                            perror("main.writeFile");
                        }
                    }
                    else {
                        //perror("main.openFile");
                    }
                    if(result == 1) {
                        FILE* append = fopen(headCommand->path, "r");
                        if((append = fopen(headCommand->path, "r"))== NULL) {
                            perror("main.fopen");
                            exit(EXIT_FAILURE);
                        }
                        char data[DATA];
                        char buffer[DATA];
                        while (fgets(buffer, DATA, append)) {
                            if(strlen(buffer) + strlen(data) + 1 < DATA) {
                                strcat(data, buffer);
                            }
                            else {
                                setErrno(-11);
                                perror("main.append");
                            }
                        }   
                        fclose(append);
                        result = appendToFile(headCommand->path, data, strlen(data), dirD);
                        if(p == 1) {
                            printf("appendToFile(%s, %s, %ld, %s) : %d \n", headCommand->path, data, strlen(data), dirD, result);
                        }
                        if(result == 0) {
                            result = closeFile(headCommand->path);
                            if(p == 1) {
                                printf("closeFile(%s) : %d \n", headCommand->path, result);
                            }
                            if(result == 0) {
                            }
                            else {
                                perror("main.closeFile");
                            }
                        }
                        else {
                            perror("main.appendToFile");
                        }
                        
                    }
                    break;
            }
            commandNode* tmp = headCommand;
            headCommand = headCommand->next;
            free(tmp->path);
            free(tmp);
        }
        if(closeConnection(nameSocket) == -1) {
            printf("errore nel closeConnection \n");
            exit(EXIT_FAILURE);
        }
    }
    else {
        printf("help \n");
    }


}