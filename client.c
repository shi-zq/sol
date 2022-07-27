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

commandNode* headCommand = NULL; //FIFO
int clientSocket = -1;
char* nameSocket = NULL;

long isNumber(const char* s){
    char* e = NULL;
    long val = strtol(s, &e, 0);
    if (e != NULL && *e == (char)0) return val;
    return -1;
}

int setErrno(int result) {
    switch(result) {
        case(-1): //errno settato da chiamate di funzione
            return -1;
        case(-2):
            errno = EEXIST;
            return -1;
        case(-3):
            errno = EACCES;
            return -1;
        case(-4):
            errno = ENOSPC;
            return -1;
        case(-5):
            errno = ENOENT;
            return -1;
        case(-6):
            errno = EFBIG;
            return -1;
        case(-7):
            errno = ENAMETOOLONG;
            return -1;
        case(-8):
            errno = ENOTCONN;
            return -1;
        default:
            return result;
    }
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
    snprintf(new->path, strlen(path) + 1, "%s", path);
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
    if(currTime.tv_sec == endTime.tv_sec) {
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
        return setErrno(-1);
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
        snprintf(nameSocket, strlen(socketName) + 1, "%s", socketName);
        return setErrno(0);
    }
    else {
        return setErrno(-1);
    }
}

int closeConnection(const char* sockname) {
    char request[REQUEST];
    char answer[REQUEST];
    memset(request, 0, REQUEST);
    memset(answer, 0, REQUEST);
    if(strcmp(nameSocket, sockname) == 0) {
        snprintf(request, REQUEST, "C");
        if(write(clientSocket, request, REQUEST) == -1) {
            return setErrno(-1);
        }
        if(read(clientSocket, answer, REQUEST) == -1) {
            return setErrno(-1);
        }
        int result = atoi(answer);
        if(result == 0) {
            free(nameSocket);
            close(clientSocket);
        }
        return setErrno(result);
    }
    else {
        return setErrno(-9);
    }
}

int openFile(const char* pathname, int flags) {
    char request[REQUEST];
    char answer[REQUEST];
    memset(request, 0, REQUEST);
    memset(answer, 0, REQUEST);
    char* absPath = NULL;
    absPath = realpath(pathname, absPath);
    if(absPath == NULL) {
        return setErrno(-1);
    }
    snprintf(request, REQUEST, "o;%s;%d", absPath, flags);
    free(absPath);
    if(write(clientSocket, request, REQUEST) == -1) {
        return setErrno(-1);
    }
    if(read(clientSocket, answer, REQUEST) == -1) {
        return setErrno(-1);
    }
    int result = atoi(answer);
    return setErrno(result);
}

int writeFile(const char* pathname, const char* dirname) {
    FILE *fp;
    char* absPath = NULL;
    absPath = realpath(pathname, absPath);
    char data[DATA];
    memset(data, 0, DATA);
    data[0] = '\0';
    char buffer[DATA];
    memset(buffer, 0, DATA);
    if(absPath == NULL) {
        return setErrno(-1);
    }
    if ((fp = fopen(pathname, "r")) == NULL) {
        return setErrno(-1);
    }
    while (fgets(buffer, DATA, fp) != NULL) {
        if(strlen(buffer) + strlen(data) + 1 < DATA) {
            strcat(data, buffer);
        }
        else {
            return setErrno(-7);
        }
    }
    fclose(fp);
    char request[REQUEST + DATA];
    memset(request, 0, REQUEST + DATA);
    char answer[REQUEST];
    memset(answer, 0, REQUEST);
    snprintf(request, REQUEST + DATA, "w;%s;%s", absPath, data);
    free(absPath);
    if(write(clientSocket, request, REQUEST + DATA) == -1) {
        return setErrno(-1);
    }
    if(read(clientSocket, answer, REQUEST) == -1) {
        return setErrno(-1);
    }
    int result = atoi(answer);
    char outFile[REQUEST + DATA];
    char fileName[REQUEST];
    if(result >= 0) {
        int saved;
        if(dirname == NULL) {
            saved = 0;
        }
        else {
            saved = 1;
        }
        while(result > 0) {
            memset(outFile, 0, REQUEST + DATA);
            memset(fileName, 0, DATA);
            if(read(clientSocket, outFile, REQUEST + DATA) == -1) { //leggere il nome del file
                return setErrno(-1);
            }
            if(saved == 1) {
                char* save = NULL;
                char* token = NULL;
                token = strtok_r(outFile, ";", &save);
                snprintf(fileName, DATA, "%s/%s", dirname, basename(token));
                FILE* file = fopen(fileName, "w");
                if(file == NULL) {
                    return setErrno(-1);
                }
                token = strtok_r(NULL, ";", &save);
                fprintf(file, "%s", token);
                fclose(file);
            }
            result--;
        }
        return setErrno(result); //dovrebbe essere 0
    }
    else {
        return setErrno(result);
    }
}

int closeFile(const char* pathname) {
    char* absPath = NULL;
    absPath = realpath(pathname, absPath);
    char request[REQUEST];
    memset(request, 0, REQUEST);
    char answer[REQUEST];
    memset(answer, 0, REQUEST);
    if(absPath == NULL) {
        return setErrno(-1);
    }
    snprintf(request, REQUEST, "c;%s", absPath);
    free(absPath);
    if(write(clientSocket, request, REQUEST) == -1) {
        return setErrno(-1);
    }
    if(read(clientSocket, answer, REQUEST) == -1) {
        return setErrno(-1);
    }
    int result = atoi(answer);
    return setErrno(result);
}

int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname) {
    char* absPath = NULL;
    absPath = realpath(pathname, absPath);
    if(absPath == NULL) {
        return setErrno(-1);
    }
    char request[REQUEST + DATA];
    memset(request, 0, REQUEST + DATA);
    char answer[REQUEST];
    memset(answer, 0, REQUEST);
    snprintf(request, REQUEST, "a;%s;%s", absPath, (char*)buf);
    free(absPath);
    if(write(clientSocket, request, REQUEST + DATA) == -1) {
        return setErrno(-1);
    }
    if(read(clientSocket, answer, REQUEST) == -1) {
        return setErrno(-1);
    }
    int result = atoi(answer);
    char outFile[REQUEST + DATA];
    char fileName[DATA];
    if(result >= 0) {
        int saved;
        if(dirname == NULL) {
            saved = 0;
        }
        else {
            saved = 1;
        }
        while(result > 0) {
            memset(outFile, 0, REQUEST + DATA);
            memset(fileName, 0, DATA);
            if(read(clientSocket, outFile, REQUEST + DATA) == -1) { //leggere il nome del file
                return setErrno(-1);
            }
            if(saved == 1) {
                char* save = NULL;
                char* token = NULL;
                token = strtok_r(outFile, ";", &save);
                snprintf(fileName, DATA, "%s/%s", dirname, basename(token));
                FILE* file = fopen(fileName, "w");
                if(file == NULL) {
                    return setErrno(-1);
                }
                token = strtok_r(NULL, ";", &save);
                fprintf(file, "%s", token);
                fclose(file);
            }
            result--;
        }
        return setErrno(result); //dovrebbe essere 0
    }
    else {
        return setErrno(result);
    }
}

int readFile(const char* pathname, void** buf, size_t* size) {
    char* absPath = NULL;
    absPath = realpath(pathname, absPath);
    char request[REQUEST];
    memset(request, 0, REQUEST);
    char answer[REQUEST];
    memset(answer, 0, REQUEST);
    snprintf(request, REQUEST, "r;%s", absPath);
    free(absPath);
    if(write(clientSocket, request, REQUEST) == -1) {
        return setErrno(-1);
    }
    if(read(clientSocket, answer, REQUEST) == -1) {
        return setErrno(-1);
    }
    int result = atoi(answer);
    if(result == 0) {
        memset(answer, 0, DATA); //riutillizare answer per ricevere dati
        if(read(clientSocket, answer, REQUEST) == -1) {
            return setErrno(-1);
        }
        char* data = malloc((strlen(answer) + 1) * sizeof(char));
        snprintf(data, strlen(data) + 1, "%s", answer);
        *size = strlen(data);
        *buf = (void*)data;
    }
    return setErrno(result);
}

int readNFiles(int n, const char* dirname) {
    char request[REQUEST];
    memset(request, 0, REQUEST);
    char answer[REQUEST];
    memset(answer, 0, REQUEST);
    snprintf(request, REQUEST, "%d", n);
    if(write(clientSocket, request, REQUEST) == -1) {
        return setErrno(-1);
    }
    if(read(clientSocket, answer, REQUEST) == -1) {
        return setErrno(-1);
    }
    int result = atoi(answer);
    char outFile[REQUEST + DATA];
    char fileName[DATA];
    if(result >= 0) {
        int saved;
        if(dirname == NULL) {
            saved = 0;
        }
        else {
            saved = 1;
        }
        while(result > 0) {
            memset(outFile, 0, REQUEST + DATA);
            memset(fileName, 0, DATA);
            if(read(clientSocket, outFile, REQUEST + DATA) == -1) { //leggere il nome del file
                return setErrno(-1);
            }
            if(saved == 1) {
                char* save = NULL;
                char* token = NULL;
                token = strtok_r(outFile, ";", &save);
                snprintf(fileName, DATA, "%s/%s", dirname, basename(token));
                FILE* file = fopen(fileName, "w");
                if(file == NULL) {
                    return -1;
                }
                token = strtok_r(NULL, ";", &save);
                fprintf(file, "%s", token);
                fclose(file);
            }
            result--;
        }
        return setErrno(result); //dovrebbe essere 0
    }
    else {
        return setErrno(result);
    }

}

int lockFile(const char* pathname) {
    char* absPath = NULL;
    absPath = realpath(pathname, absPath);
    char request[REQUEST];
    memset(request, 0, REQUEST);
    char answer[REQUEST];
    memset(answer, 0, REQUEST);
    if(absPath == NULL) {
        return setErrno(-1);
    }
    snprintf(request, REQUEST, "l;%s", absPath);
    if(write(clientSocket, request, REQUEST) == -1) {
        return setErrno(-1);
    }
    if(read(clientSocket, answer, REQUEST) == -1) {
        return setErrno(-1);
    }
    int result = atoi(answer);
    return setErrno(result);
}
int unlockFile(const char* pathname) {
    char* absPath = NULL;
    absPath = realpath(pathname, absPath);
    char request[REQUEST];
    memset(request, 0, REQUEST);
    char answer[REQUEST];
    memset(answer, 0, REQUEST);
    if(absPath == NULL) {
        return setErrno(-1);
    }
    snprintf(request, strlen(absPath) + 3, "u;%s", absPath);
    if(write(clientSocket, request, REQUEST) == -1) {
        return setErrno(-1);
    }
    if(read(clientSocket, answer, REQUEST) == -1) {
        return setErrno(-1);
    }
    int result = atoi(answer);
    return setErrno(result);
}

int removeFile(const char* pathname) {
    char* absPath = NULL;
    char request[REQUEST];
    memset(request, 0, REQUEST);
    char answer[REQUEST];
    memset(answer, 0, REQUEST);
    absPath = realpath(pathname, absPath);
    if(absPath == NULL) {
        return setErrno(-1);
    }
    snprintf(request, REQUEST, "C;%s", absPath);
    if(write(clientSocket, request, REQUEST) == -1) {
        return setErrno(-1);
    }
    if(read(clientSocket, answer, REQUEST) == -1) {
        return setErrno(-1);
    }
    int result = atoi(answer);
    return setErrno(result);
}

void writeNFiles(char* dirName, int* n) { // se n \`e negatovo scrive tutti i file
    DIR* dir = opendir(dirName);
    if(dir == NULL) {
        perror("writeNFiles.opendir");
        exit(EXIT_FAILURE);
    }
    struct dirent* entry;
    if(*n == 0) {
        return; //non ho piu file da scrivere
    }
    while((entry = readdir(dir)) != NULL && *n != 0) {
        char path[PATH_MAX];
        snprintf(path, PATH_MAX, "%s/%s", dirName, entry->d_name);
        struct stat info;
        if(stat(path,&info) ==- 1) {
            perror("writeNFiles.stat");
            exit(EXIT_FAILURE);
        }
        if(S_ISDIR(info.st_mode)) {
            if (strcmp(entry->d_name,".") == 0 || strcmp(entry->d_name,"..") == 0) {
                //non faccio niente
            }
            else {
                writeNFiles(path , n); //chiamo la funzione su questo
            }
        }
        else {
            add('w', path);
            (*n)--;
        }
    }
    if ((closedir(dir)) == -1) {
        perror("writeNFile.closedir");
        exit(EXIT_FAILURE);
    }
    return;
}

int main(int argc, char *argv[]) {
    char opt = 0;
    int connected = 0;
    int t = 0;
    int p = 1;
    char* save;
    char* token;
    char* token2;
    char* dirD = NULL;
    char* dird = NULL;
    int n;
    DIR* dir = NULL;
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
                token = strtok_r(optarg, ",", &save);
                while(token != NULL) {
                    add('w', token);
                    token = strtok_r(NULL, ",", &save);
                }
                break;
            case('w'):
                token = strtok_r(optarg, ",", &save);
                token2 = strtok_r(NULL, ",", &save);
                n = isNumber(token2);
                if(n <= -1) {
                    printf(" main.errore nel n -w \n");
                    exit(EXIT_FAILURE);
                }
                if(n == 0) {
                    n = -1;
                }
                writeNFiles(token, &n);
                break;
            case('D'):
                dir = opendir(optarg);
                if(dir == NULL) {
                    perror("main.openDir");
                    exit(EXIT_FAILURE);
                }
                dirD = realpath(optarg, dirD);
                if(dirD == NULL) {
                    perror("main.malloc");
                    exit(EXIT_FAILURE);
                }
                closedir(dir);
                break;
            case('r'):
                token = strtok_r(optarg, ";", &save);
                while(token != NULL) {
                    add('r', token);
                    token = strtok_r(NULL, ",", &save);
                }
                break;
            case('R'):
                add('R', optarg);
                break;    
            case('d'):
                dir = opendir(optarg);
                if(dir == NULL) {
                    perror("main.opendir");
                    exit(EXIT_FAILURE);
                }
                dird = realpath(optarg, dird);
                if(dird == NULL) {
                    perror("main.malloc");
                    exit(EXIT_FAILURE);
                }
                closedir(dir);
                break;
            case('l'):
                add('l', optarg);
                break;
            case('u'):
                add('u', optarg);
                break;
            case('c'):
                add('d', optarg);
            default:
                printf("default \n");
                exit(EXIT_FAILURE);
                break;
        }
    }
    printf("inizio inviare commandi \n");
    if(connected == 1) {
        while(headCommand != NULL) {
            if(t != 0) {
                sleep(t/1000);
            }
            int result;
            switch(headCommand->operation) {
                case('w'):
                    result = openFile(headCommand->path, O_CREATE | O_LOCK);
                    if(p == 1) {
                        printf("openFile(%s, %d) : %d \n", headCommand->path, O_CREATE | O_LOCK, result);
                    }
                    if(result >= 0) {
                        if(result == 0) {
                            result = writeFile(headCommand->path, dirD);
                            if(p == 1) {
                                printf("writeFile(%s, %s) : %d \n", headCommand->path, dirD, result);
                            }
                            if(result == 0) {
                                closeFile(headCommand->path);
                                if(p == 1) {
                                    printf("closeFile(%s) : %d \n", headCommand->path, result);
                                }
                                if(result != 0) {
                                    perror("main.closeFile");
                                }
                            }
                            else {
                                perror("main.writeFile");
                            }
                        }
                        else {
                            FILE* append;
                            if((append = fopen(headCommand->path, "r")) == NULL) {
                                perror("main.fopen");
                                exit(EXIT_FAILURE);
                            }
                            char data[DATA];
                            memset(data, 0, DATA);
                            char buffer[DATA];
                            memset(buffer, 0, DATA);
                            while (fgets(buffer, DATA, append)) {
                                if(strlen(buffer) + strlen(data) + 1 < DATA) {
                                    strcat(data, buffer);
                                }
                                else {
                                    setErrno(-1);
                                    perror("main.strlen");
                                }
                            }
                            fclose(append);
                            result = appendToFile(headCommand->path, data, strlen(data), dirD);
                            if(p == 1) {
                                printf("appendToFile(%s, %s, %ld, %s) : %d \n", headCommand->path, data, strlen(data), dirD, result);
                            }
                            if(result == 0) {
                                closeFile(headCommand->path);
                                if(p == 1) {
                                    printf("closeFile(%s) : %d \n", headCommand->path, result);
                                }
                                if(result != 0) {
                                    perror("main.closeFile");
                                }
                            }
                            else {
                                perror("main.appendToFile");
                            }
                        }
                    }
                    break;
                case('r'):
                    char* buf = NULL;
                    size_t size;
                    result = readFile(headCommand->path, (void*)&buf, &size);
                    if(p == 1) {
                        printf("readFile(%s, %s, %ld) : %d \n", headCommand->path, buf, size, result);
                    }
                    if(dird != NULL) {
                        char* rsave = NULL;
                        char* rtoken = NULL;
                        char outFile[REQUEST + DATA];
                        char fileName[DATA];
                        memset(outFile, 0, REQUEST + DATA);
                        memset(fileName, 0, DATA);
                        rtoken = strtok_r(outFile, ";", &rsave);
                        snprintf(fileName, DATA, "%s/%s", dird, basename(token));
                        FILE* file = fopen(fileName, "w");
                        if(file == NULL) {
                            perror("main.fopen");
                            exit(EXIT_FAILURE);
                        }
                        rtoken = strtok_r(NULL, ";", &rsave);
                        fprintf(file, "%s", rtoken);
                        fclose(file);
                    }
                    free(buf);
                    break;
                case('R'):
                    n = isNumber(headCommand->path);
                    if(n <= -1) {
                        printf(" main.errore nel n -R \n");
                        exit(EXIT_FAILURE);
                    }
                    if(n == 0) {
                        n = -1;
                    }
                    readNFiles(n, dird);
                    break;
                case('l'):
                    result = lockFile(headCommand->path);
                    if(result < 0) {
                        perror("main.lockFile");
                    }
                    if(p == 1) {
                        printf("lockFile(%s) : %d \n", headCommand->path, result);
                    }
                    break; 
                case('u'):
                    result = unlockFile(headCommand->path);
                    if(result < 0) {
                        perror("main.unlockFile");
                    }
                    if(p == 1) {
                        printf("unlockFile(%s) : %d \n", headCommand->path, result);
                    }
                    break;
                case('d'):
                    result = removeFile(headCommand->path);
                    if(result < 0) {
                        perror("main.removeFile");
                    }
                    if(p == 1) {
                        printf("removeFile(%s) : %d \n", headCommand->path, result);
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
    free(dirD);
    free(dird);
    printf("fine \n");
    return 0;

}