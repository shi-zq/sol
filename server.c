#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h> 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#define UNIX_PATH_MAX 108
#define REQUEST 1024
#define DATA 1024
#define O_CREATE 1
#define O_LOCK 2

typedef struct jobNode {
    char* requestData;
    int clientSocketNumber;
    struct jobNode* next;
} jobNode;

typedef struct fileNode {
    char* abspath;
    char* data;
    int clientSocketNumber;
    struct fileNode* next;
} fileNode;

int nThread = 2; // numero di thread
int maxFileNumber = 3; // massimo numero di file
int maxStorageSize = 3*1024; // massimo capacita

int currentNumber = 0; //numero di file corrente
int currentSize = 0; //numero di capacita usato

int maxFileNumberHit = 0;
int maxStorageSizeHit = 0;
int outFile = 0;

FILE* flog = NULL;

jobNode* headJob = NULL; //FIFO
fileNode* headFile = NULL; //aggiungere in fondo

volatile sig_atomic_t end = 0;
int selfPipe[2];

pthread_mutex_t fileLog = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t jobList = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t jobListNotEmpty = PTHREAD_COND_INITIALIZER;
pthread_mutex_t fileList = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t fileListFree = PTHREAD_COND_INITIALIZER;

long isNumber(const char* s) {
    char* e = NULL;
    long val = strtol(s, &e, 0);
    if (e != NULL && *e == (char)0) return val;
    return -1;
}

void parser(char* path) {
    FILE* fp = fopen(path, "r");
    if(fp == NULL) {
        perror("parser.fopen");
        exit(EXIT_FAILURE);
    }
    char buf[100];
    char* token;
    char* token2;
    char* save;
    int tmp;
    while (fgets(buf, 100, fp) != NULL) {
        token = strtok_r(buf, ";", &save);
        if(strlen(token) != 1) {
            printf("errore nel config file");
            exit(EXIT_FAILURE);
        }
        switch(token[0]) {
            case('n'):
                token2 = strtok_r(NULL, ";", &save);
                tmp = isNumber(token2);
                if(tmp < 0) {
                    printf("n non valido, n=2 per default \n");
                }
                else {
                    //printf("n=%d \n", tmp);
                    nThread = tmp;
                }
                break;
            case('k'):
                token2 = strtok_r(NULL, ";", &save);
                tmp = isNumber(token2);
                if(tmp < 0) {
                    printf("k non valido, k=3 per default \n");
                }
                else {
                    //printf("k=%d \n", tmp);
                    maxFileNumber = tmp;
                }
                break;
            case('s'):
                token2 = strtok_r(NULL, ";", &save);
                tmp = isNumber(token2);
                if(tmp < 0) {
                    printf("s non valido, s=3*1024 per default \n");
                }
                else {
                    //printf("s=%d \n", tmp);
                    maxStorageSize = tmp;
                }
                break;
            default:
                printf("file di configurazione non valido usare il seguente formatto \n");
                printf("n=numero di thread \n");
                printf("k=numero di file \n");
                printf("s=size di storage \n");
                exit(EXIT_FAILURE);               
        }
    }
    fclose(fp);  
}

void setMaxFileNumberHit(int newCurrFileNumber) {
    currentNumber = newCurrFileNumber;
    if(newCurrFileNumber > maxFileNumberHit) {
        maxFileNumberHit = newCurrFileNumber;
    }
}

void setMaxStorageSizeHit(int newCurrStorageSize) {
    currentSize = newCurrStorageSize;
    if(newCurrStorageSize > maxStorageSizeHit) {
        maxStorageSizeHit = newCurrStorageSize;
    }
}

static void sigHandler (int sig) {
    if(write(selfPipe[1], "q", 1) == -1) {
        _exit(EXIT_FAILURE);
    }
    if(close(selfPipe[1]) == -1) {
        _exit(EXIT_FAILURE); //funzione safe
    }
    if(sig == SIGQUIT || sig == SIGINT) {
        end = -1;
    }
    else {
        end = -2;
    }
}

void emptyJobList() {
    pthread_mutex_lock(&jobList);
    jobNode* tmp = NULL;
    while(headJob != NULL) { //svuota la lista di job
        tmp = headJob;
        headJob = headJob->next;
        free(tmp->requestData);
        free(tmp);
    }
    pthread_mutex_unlock(&jobList);
}

void addExitNodeJobList() { //quando chiamo addExitNodeJobList headJob `e vuoto
    pthread_mutex_lock(&jobList);
    for(int i = 0; i < nThread; i++) {
        jobNode* new = malloc(sizeof(jobNode));
        if(new == NULL) {
            perror("addExitNodeJobList.malloc");
            exit(EXIT_FAILURE);
        }
        new->clientSocketNumber = -1;
        new->requestData = NULL;
        new->next = headJob;
        headJob = new;
    }
    pthread_cond_broadcast(&jobListNotEmpty);
    pthread_mutex_unlock(&jobList);
}

void addJobNode(char* requestData, int clientSocketNumber) {
    pthread_mutex_lock(&jobList);
    jobNode* new = malloc(sizeof(jobNode));
    if(new == NULL) {
        perror("addJobNode.malloc");
        exit(EXIT_FAILURE);
    }
    new->requestData = malloc((strlen(requestData) + 1) * sizeof(char));
    snprintf(new->requestData, strlen(requestData) + 1, "%s", requestData);
    new->clientSocketNumber = clientSocketNumber;
    new->next = NULL;
    if(headJob == NULL) {
        headJob = new;
    }
    else {
        jobNode* tmp = headJob;
        while(tmp->next != NULL) {
            tmp = tmp->next;
        }
        tmp->next = new;
    }
    pthread_cond_broadcast(&jobListNotEmpty);
    pthread_mutex_unlock(&jobList);
}

void closeConnection(int clientSocketNumber) {
    pthread_mutex_lock(&fileList);
    fileNode* tmp = headFile;
    while(tmp != NULL) {
        if(tmp->clientSocketNumber == clientSocketNumber) {
            tmp->clientSocketNumber = -1;
        }
        tmp = tmp->next;
    }// cancellare tutti file lock da clientSocketNumber
    char response[REQUEST];
    memset(response, 0, REQUEST);
    snprintf(response, REQUEST, "0");
    if(write(clientSocketNumber, response, REQUEST) == -1) {
        perror("closeConnection.write");
        exit(EXIT_FAILURE);
    }    
    pthread_mutex_lock(&fileLog);
    fprintf(flog, "thread=%d;op=closeConnection;read=0;write=0;path=NULL;answer=0 \n", gettid());
    pthread_mutex_unlock(&fileLog);
    pthread_cond_signal(&fileListFree);
    pthread_mutex_unlock(&fileList);
}

void openFileNode(int clientSocketNumber, char* path, int flags) { //openFileNode non incrementa numero di file dato che \`e vuoto
    pthread_mutex_lock(&fileList);
    int answer = 0;
    int found = 0;
    fileNode* prev = NULL;
    fileNode* tmp = headFile;
    while(tmp != NULL && found == 0) {
        if(strcmp(path, tmp->abspath) == 0) {
            found = 1;
            if(tmp->clientSocketNumber == -1 || tmp->clientSocketNumber == clientSocketNumber) {
                answer = 1; //file gia creato 
            }
            else {
                answer = -3; // non hai permesso
            }
        }
        else {
            prev = tmp;
            tmp = tmp->next;
        }
    }
    switch(flags) {
        case(O_CREATE): //se non c'e viene creato, non usato dal client
            if(found == 0) {
                fileNode* new = malloc(sizeof(fileNode));
                if(new == NULL) {
                    perror("openFileNode.malloc");
                    exit(EXIT_FAILURE);
                }
                new->data = NULL;
                new->clientSocketNumber = -1; //non metto il lock
                new->next = NULL;
                new->abspath = malloc((strlen(path) + 1) * sizeof(char));
                if(new->abspath == NULL) {
                    perror("openFileNode.malloc");
                    exit(EXIT_FAILURE);
                }
                sprintf(new->abspath, "%s", path);
                prev->next = new;
            }
            break;
        case(O_LOCK): //non usato dal client
            if(found == 0) {
                answer = -5;//file non esistente
            }     
            else {
                if(answer == 1) { //esiste il file e non \`e lockato
                    tmp->clientSocketNumber = clientSocketNumber; //metto il lock
                } //altrimenti answer rimane -3
            }
            break;
        case(O_CREATE | O_LOCK):
            if(found == 0) {
                fileNode* new = malloc(sizeof(fileNode));
                if(new == NULL) {
                    perror("openFileNode.malloc");
                    exit(EXIT_FAILURE);
                }
                new->data = NULL;
                new->clientSocketNumber = clientSocketNumber; // metto il lock
                new->next = NULL;
                new->abspath = malloc((strlen(path) + 1) * sizeof(char));
                if(new->abspath == NULL) {
                    perror("openFileNode.malloc");
                    exit(EXIT_FAILURE);
                }
                snprintf(new->abspath, strlen(path) + 1, "%s", path);
                if(prev == NULL) { //se prev \`e NULL allora la lista \`e vuota se no punta all'ultimo elemento della lista
                    headFile = new;
                }
                else {
                    prev->next = new;
                }
            }
            else {
                if(answer == 1 ) {
                    tmp->clientSocketNumber = clientSocketNumber; //metto il lock
                } //altrimenti answer rimane -3
            }
    }
    char response[REQUEST];
    memset(response, 0, REQUEST);
    snprintf(response, REQUEST, "%d", answer);
    if(write(clientSocketNumber, response, REQUEST) == -1) {
        perror("openFileNode.write");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_unlock(&fileList);
    pthread_mutex_lock(&fileLog);
    fprintf(flog, "thread=%d;op=openFile;read=0;write=0;path=%s;answer=%d \n", gettid(), path, answer);
    pthread_mutex_unlock(&fileLog);
}

void writeFileNode(int clientSocketNumber, char* path, char* data) {
    pthread_mutex_lock(&fileList);
    int answer = 0;
    int found = 0;
    fileNode* prev = NULL;
    fileNode* tmp = headFile;
    while(tmp != NULL && found == 0) {
        if(strcmp(path, tmp->abspath) == 0) {
            found = 1;
            if(tmp->clientSocketNumber == -1 || tmp->clientSocketNumber == clientSocketNumber) {
                if(currentNumber + 1 > maxFileNumber || currentSize + strlen(data) > maxStorageSize) { //controllo se posso mettere questo file
                    fileNode* tmp1 = headFile;
                    int success = 0;
                    int freeSpace = 0;
                    int freeFile = 0;
                    while(success == 0 && tmp1 != NULL) { //controllo se ho spazio libero
                        if(tmp1->clientSocketNumber == -1 && strcmp(tmp1->abspath, path) != 0 && tmp1->data != NULL) {
                            freeSpace = freeSpace + strlen(tmp1->data);
                            freeFile++;
                            answer++; //answer = 0 inizialmente
                            if(currentNumber - freeFile + 1 <= maxFileNumber && currentSize - freeSpace + strlen(data) <= maxStorageSize) {
                                success = 1;
                            }
                        }
                        tmp1 = tmp1->next;
                    }
                    if(success == 0) { //non ha spazio libero per questo nodo
                        pthread_cond_signal(&fileListFree);
                        if(prev == NULL) { // \`e il primo elemento
                            headFile = tmp->next;
                            free(tmp->abspath); //non ha data
                            free(tmp);
                        }
                        else {
                            prev->next = tmp->next;
                            free(tmp->abspath);
                            free(tmp);
                        }
                        answer = -4; //non hai spazio
                    }
                }
            }
            else {
                answer = -3; // non hai permesso
            } 
        }
        else {
            prev = tmp;
            tmp = tmp->next;
        }
    }
    char response[REQUEST];
    memset(response, 0, REQUEST);
    if(found == 0) {
        answer = -5; // file non trovato
    }
    snprintf(response, REQUEST, "%d", answer);
    if(answer < 0) {
        if(write(clientSocketNumber, response, REQUEST) == -1) {
            perror("writeFileNode.write");
            exit(EXIT_FAILURE);
        }
        pthread_mutex_lock(&fileLog);
        fprintf(flog, "thread=%d;op=writeFile;read=0;write=0;path=%s;answer=%d \n", gettid(), tmp->abspath, answer); //writeFIleNode fallito percio 0 per len
        pthread_mutex_unlock(&fileLog);      
    }
    else { //answer >= 0
        tmp->data = malloc((strlen(data) + 1) * sizeof(char));
        if(tmp->data == NULL) {
            perror("writeFileNode.malloc");
            exit(EXIT_FAILURE);
        }
        snprintf(tmp->data, strlen(data) + 1, "%s", data); //copio la data
        setMaxFileNumberHit(currentNumber + 1);
        setMaxStorageSizeHit(currentSize + strlen(data));  
        pthread_mutex_lock(&fileLog);
        fprintf(flog, "thread=%d;op=writeFile;read=0;write=%ld;path=%s;answer=%d \n", gettid(), strlen(tmp->data), tmp->abspath, answer);
        pthread_mutex_unlock(&fileLog);      
        if(write(clientSocketNumber, response, REQUEST) == -1) {
            perror("writeFileNode.write");
            exit(EXIT_FAILURE);
        }
        fileNode* tmp2 = headFile;
        fileNode* prev2 = NULL;
        char outFile[REQUEST + DATA];
        while(answer != 0) {
            memset(outFile, 0, REQUEST + DATA);
            if(tmp2->clientSocketNumber == -1 && strcmp(tmp2->abspath, path) != 0 && tmp2->data != NULL) { //il file da togliere
                snprintf(outFile, REQUEST + DATA, "%s;%s", tmp2->abspath, tmp2->data);
                if(write(clientSocketNumber, outFile, REQUEST + DATA) == -1) {
                    perror("writeFileNode.write");
                    exit(EXIT_FAILURE);
                }
                answer--;
                currentNumber--;
                currentSize = currentSize - strlen(tmp2->data);
                pthread_mutex_lock(&fileLog);
                fprintf(flog, "thread=-1;op=out;read=%ld;write=0;path=%s;answer=-1 \n", strlen(tmp2->data), tmp2->abspath); //thread = -1 operazione interna
                pthread_mutex_unlock(&fileLog);
                if(prev2 == NULL) { // la testa
                    headFile = tmp2->next;
                    free(tmp2->abspath);
                    free(tmp2->data);
                    free(tmp2);
                    tmp2 = headFile;
                }  
                else {
                    prev2->next = tmp2->next;
                    free(tmp2->abspath);
                    free(tmp2->data);
                    free(tmp2);
                    tmp2 = prev2->next;
                }       
            }
            else {
                prev2 = tmp2;
                tmp2 = tmp2->next;
            }
        }
    }
    pthread_mutex_unlock(&fileList);
}

void closeFileNode(int clientSocketNumber, char* path) {
    pthread_mutex_lock(&fileList);
    int answer = 0;
    fileNode* tmp = headFile;
    int found = 0;
    while(tmp != NULL && found == 0) {
        if(strcmp(path, tmp->abspath) == 0) {
            found = 1;
            if(tmp->clientSocketNumber == clientSocketNumber) {
                tmp->clientSocketNumber = -1;
            }
            else {
                answer = -3;
            }
        }
        tmp = tmp->next;
    }
    char response[REQUEST];
    memset(response, 0, REQUEST);
    snprintf(response, REQUEST, "%d", answer);
    if(write(clientSocketNumber, response, REQUEST) == -1) {
        perror("closeFileNode.write");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_lock(&fileLog);
    fprintf(flog, "thread=%d;op=closeFile;read=0;write=0;path=%s;answer=%d \n", gettid(), path, answer);
    pthread_mutex_unlock(&fileLog);
	pthread_mutex_unlock(&fileList);
}

void appendToFile(int clientSocketNumber, char* path, char* data) {
    pthread_mutex_lock(&fileList);
    int found = 0;
    int answer = 0;
    fileNode* tmp = headFile;
    while(tmp != NULL && found == 0) {
        if(strcmp(tmp->abspath, path) == 0) {
            found = 1;
            if(tmp->clientSocketNumber == -1 || tmp->clientSocketNumber == clientSocketNumber) {
                if(strlen(data) + strlen(tmp->data) < 1024) {
                    if(currentSize + strlen(data) + 1 > maxStorageSize) {
                        fileNode* tmp1 = headFile;
                        int success = 0;
                        int freeSpace = 0;
                        while(success == 0 && tmp1 != NULL) { //controllo quali sono file liberi
                            if(tmp1->clientSocketNumber == -1 && strcmp(tmp1->abspath, path) != 0 && tmp1->data != NULL) {
                                freeSpace = freeSpace + strlen(tmp1->data);
                                answer++;
                                if(currentSize - freeSpace + strlen(tmp->data) <= maxStorageSize) {
                                    success = 1;
                                }
                            }
                            tmp1 = tmp1->next;
                        }
                        if(success == 0) {
                            answer = -4; // non hai spazio
                        }          
                    }
                }
                else {
                    answer = -6; // file troppo grande
                }
            }
        }
        else {
            tmp = tmp->next;
        }
    }
    char response[REQUEST];
    memset(response, 0, REQUEST);
    if(found == 0) {
        answer = -5; // file non trovato
    }
    snprintf(response, REQUEST, "%d", answer);
    if(answer < 0) {
        if(write(clientSocketNumber, response, REQUEST) == -1) {
            perror("appentoToFile.write");
            exit(EXIT_FAILURE);
        }
        pthread_mutex_lock(&fileLog);
        fprintf(flog, "thread=%d;op=appendToFile;read=0;write=0;path=%s;answer=%d \n", gettid(), tmp->abspath, answer); //appentoToFile fallito percio o per len
        pthread_mutex_unlock(&fileLog);      
    }
    else { //answer >= 0
        tmp->data = realloc(tmp->data, (strlen(data) + strlen(tmp->data) + 1) * sizeof(char));
        if(tmp->data == NULL) {
            perror("appentoToFile.malloc");
            exit(EXIT_FAILURE);
        }
        strncat(tmp->data, data, strlen(data) + 1); //concateno
        setMaxStorageSizeHit(currentSize + strlen(data));  
        pthread_mutex_lock(&fileLog);
        fprintf(flog, "thread=%d;op=appendToFile;read=0;write=%ld;path=%s;answer=%d \n", gettid(), strlen(data), tmp->abspath, answer);
        fflush(flog);
        pthread_mutex_unlock(&fileLog);      
        if(write(clientSocketNumber, response, REQUEST) == -1) {
            perror("appentoToFile.write");
            exit(EXIT_FAILURE);
        }
        fileNode* tmp2 = headFile;
        fileNode* prev2 = NULL;
        char outFile[REQUEST + DATA];
        while(answer != 0) {
            memset(outFile, 0, REQUEST + DATA);
            if(tmp2->clientSocketNumber == -1 && strcmp(tmp2->abspath, path) != 0 && tmp2->data != NULL) { //il file da togliere
                snprintf(outFile, REQUEST + DATA, "%s;%s", tmp2->abspath, tmp2->data);
                if(write(clientSocketNumber, outFile, REQUEST + DATA) == -1) {
                    perror("appendToFile.write");
                    exit(EXIT_FAILURE);
                }
                answer--;
                currentNumber--;
                currentSize = currentSize - strlen(tmp2->data);
                pthread_mutex_lock(&fileLog);
                fprintf(flog, "thread=-1;op=out;read=%ld;write=0;path=%s;answer=2 \n", strlen(tmp2->data), tmp2->abspath);
                pthread_mutex_unlock(&fileLog);
                if(prev2 == NULL) { // la testa
                    headFile = tmp2->next;
                    free(tmp2->abspath);
                    free(tmp2->data);
                    free(tmp2);
                    tmp2 = headFile;
                }  
                else {
                    prev2->next = tmp2->next;
                    free(tmp2->abspath);
                    free(tmp2->data);
                    free(tmp2);
                    tmp2 = prev2->next;
                }       
            }
            else {
                prev2 = tmp2;
                tmp2 = tmp2->next;
            }
        }
    }
    pthread_mutex_unlock(&fileList);
}

void readFileNode(int clientSocketNumber, char* path) {
    pthread_mutex_lock(&fileList);
    int found = 0;
    int answer = 0;
    fileNode* tmp = headFile;
    while(tmp != NULL && found == 0) {
        if(strcmp(tmp->abspath, path) == 0) {
            found = 1;
            if(tmp->clientSocketNumber == -1 || tmp->clientSocketNumber == clientSocketNumber) {
                if(tmp->data == NULL) {
                    answer = -5;
                }
            }
            else {
                answer = -3; // non hai permesso
            }
        }
        else {
            tmp = tmp->next;
        }
    }
    if(found == 0) {
        answer = -5;
    }
    char response[REQUEST];
    memset(response, 0, REQUEST);
    snprintf(response, REQUEST, "%d", answer);
    if(write(clientSocketNumber, response, REQUEST) == -1) {
        perror("readFileNode.write");
        exit(EXIT_FAILURE);
    }
    if(answer < 0) {
        pthread_mutex_lock(&fileLog);
        fprintf(flog, "thread=%d;op=readFile;read=0;write=0;path=%s;answer=%d \n", gettid(), path, answer);
        pthread_mutex_unlock(&fileLog);
    }
    else {
        memset(response, 0, REQUEST);
        snprintf(response, REQUEST, "%s", tmp->data);
        if(write(clientSocketNumber, response, REQUEST) == -1) {
            perror("readFileNode.write");
            exit(EXIT_FAILURE);
        }
        pthread_mutex_lock(&fileLog);
        fprintf(flog, "thread=%d;op=readFile;read=%ld;write=0;path=%s;answer=%d \n", gettid(), strlen(tmp->data), path, answer);
        pthread_mutex_unlock(&fileLog);
    }
    pthread_mutex_unlock(&fileList);
}

void readNFileNode(int clientSocketNumber, int n) {
    pthread_mutex_lock(&fileList);
    fileNode* tmp = headFile;
    int answer = 0;
    int sendFile = 0;
    while(tmp != NULL && n != 0) {
        if(tmp->clientSocketNumber == clientSocketNumber || tmp->clientSocketNumber == -1) {
            answer++;//se n positivo answer = n, se n negativo answer contiene tutti i file disponibile
            n--;
        }
        tmp = tmp->next;
    }
    char response[REQUEST];
    memset(response, 0, REQUEST);
    sprintf(response, "%d", answer);
    if(write(clientSocketNumber, response, REQUEST) == -1) {
        perror("readNFileNode.write");
        exit(EXIT_FAILURE);
    }
    tmp = headFile;
    char data[REQUEST + DATA];
    while(answer > 0) {
        memset(data, 0, REQUEST + DATA);
        if(tmp->clientSocketNumber == -1 || tmp->clientSocketNumber == clientSocketNumber) {
            snprintf(data, REQUEST + DATA, "%s;%s", tmp->abspath, tmp->data);
            if(write(clientSocketNumber, data, REQUEST + DATA) == -1) {
                perror("readNFileNode.write");
                exit(EXIT_FAILURE);
            }
            sendFile = sendFile + strlen(tmp->data);
            answer--;
        }
        tmp = tmp->next;
    }
    pthread_mutex_lock(&jobList);
    fprintf(flog, "thread=%d;op=readNFile;read=%d;write=0;path=NULL;answer=%d \n", gettid(), sendFile, answer);
    pthread_mutex_unlock(&jobList);
    pthread_mutex_unlock(&fileList);
}

void lockFileNode(int clientSocketNumber, char* path) {
    pthread_mutex_lock(&fileList);
    fileNode* tmp;
    int found = 0;
    int success = 0;
    int answer = 0;
    while(success == 0) {
        tmp = headFile;
        found = 0;
        while(tmp != NULL && found == 0) {
            if(strcmp(tmp->abspath, path) == 0) {
                found = 1;
                if(tmp->clientSocketNumber == -1 || tmp->clientSocketNumber == clientSocketNumber) {
                    success = 1;
                    tmp->clientSocketNumber = clientSocketNumber;
                }
            }
            else {
                tmp = tmp->next;
            }
        }
        //found = 0 success = 0
        //found = 1 success = 0
        //found = 1 success = 1
        if(found == 0) { //caso 0 0
            answer = -5;
            success = 1;
        }
        if(success == 0) { // caso 1 0
            pthread_cond_wait(&fileListFree, &fileList);
        }
        if(end == -2) { //uscita forzata con 
            success = 1;
            answer = -5; //file non esistente
        }
    }
    char response[REQUEST];
    memset(response, 0, REQUEST);
    sprintf(response, "%d", answer);
    if(write(clientSocketNumber, response, REQUEST) == -1) {
        perror("lockFileNode.write");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_unlock(&fileList);
    pthread_mutex_lock(&fileLog);
    fprintf(flog, "thread=%d;op=lockFile;read=0;write=0;path=%s;answer=%d \n", gettid(), path, answer);
    pthread_mutex_unlock(&fileLog);
}

void unlockFileNode(int clientSocketNumber, char* path) {
    pthread_mutex_lock(&fileList);
    fileNode* tmp = headFile;
    int found = 0;
    int answer = 0;
    while(tmp != NULL && found == 0) {
        if(strcmp(tmp->abspath, path) == 0) {
            found = 1;
            if(tmp->clientSocketNumber == clientSocketNumber) {
                tmp->clientSocketNumber = -1;
            }
            else {
                answer = -3; // non hai permesso
            }
        }
        else {
            tmp = tmp->next;
        }
    }
    if(found == 0) {
        answer = -5; //file non trvato
    }
    char response[REQUEST];
    memset(response, 0, REQUEST);
    sprintf(response, "%d", answer);
    if(write(clientSocketNumber, response, REQUEST) == -1) {
        perror("unlockFileNode.write");
        exit(EXIT_FAILURE);
    }
    pthread_cond_signal(&fileListFree);
    pthread_mutex_unlock(&fileList);
    pthread_mutex_lock(&fileLog);
    fprintf(flog, "thread=%d;op=unlockFile;read=0;write=0;path=%s;answer=%d \n", gettid(), path, answer);
    pthread_mutex_unlock(&fileLog);
}

void removeFileNode(int clientSocketNumber, char* path) {
    pthread_mutex_lock(&fileList);
    fileNode* tmp = headFile;
    fileNode* prev = NULL;
    int found = 0;
    int answer = 0;
    while(tmp != NULL && found == 0) {
        if(strcmp(tmp->abspath, path) == 0) {
            found = 1;
            if(tmp->clientSocketNumber == clientSocketNumber) {
                if(prev == NULL) {
                    headFile = tmp->next;
                }
                else {
                    prev->next = tmp->next;
                }
                currentNumber--;
                currentSize = currentNumber - strlen(tmp->data);
                free(tmp->data);
                free(tmp->abspath);
                free(tmp);
            }
            else {
                answer = -3; // non hai permesso
            }
        }
        else {
            prev = tmp;
            tmp = tmp->next;
        }
    }
    if(found == 0) {
        answer = -5; //file non trvato
    }
    char response[REQUEST];
    memset(response, 0, REQUEST);
    sprintf(response, "%d", answer);
    if(write(clientSocketNumber, response, REQUEST) == -1) {
        perror("removeFileNode.write");
        exit(EXIT_FAILURE);
    }
    pthread_cond_signal(&fileListFree);
    pthread_mutex_unlock(&fileList);
    pthread_mutex_lock(&fileLog);
    fprintf(flog, "thread=%d;op=removeFile;read=0;write=0;path=%s;answer=%d \n", gettid(), path, answer);
    pthread_mutex_unlock(&fileLog);
}
void *worker() {
    int running = 1;
    int count = 0;
    while(running == 1 ) {
        pthread_mutex_lock(&jobList);
        while(headJob == NULL) {
            pthread_cond_wait(&jobListNotEmpty, &jobList);
        }
        int clientSocketNumber = headJob->clientSocketNumber;
        char requestData[REQUEST + DATA];
        memset(requestData, 0, REQUEST + DATA);
        if(headJob->requestData != NULL) {
            sprintf(requestData, "%s", headJob->requestData);
        }
        jobNode* tmp = headJob->next;
        free(headJob->requestData);
        free(headJob);
        headJob = tmp;
        pthread_mutex_unlock(&jobList);
        if(clientSocketNumber == -1) {
            running = 0;
        }
        else {
            count++;
            char* save = NULL;
            char* token2;
            char* token3;
            char* token = strtok_r(requestData, ";", &save);
            switch(token[0]) { //token ha solo una lettera
                case('C'):
                    closeConnection(clientSocketNumber);
                    break;
                case('o'):
                    token2 = strtok_r(NULL, ";", &save);
                    token3 = strtok_r(NULL, ";", &save);
                    int flags = atoi(token3);
                    openFileNode(clientSocketNumber, token2, flags);
                    break;
                case('w'):
                    token2 = strtok_r(NULL, ";", &save);
                    token3 = strtok_r(NULL, ";", &save);
                    writeFileNode(clientSocketNumber, token2, token3);
                    break;
                case('c'):
                    token2 = strtok_r(NULL, ";", &save);
                    closeFileNode(clientSocketNumber, token2);
                    break;
                case('a'):
                    token2 = strtok_r(NULL, ";", &save);
                    token3 = strtok_r(NULL, ";", &save);
                    appendToFile(clientSocketNumber, token2, token3);
                    break;
                case('r'):
                    token2 = strtok_r(NULL, ";", &save);
                    readFileNode(clientSocketNumber, token2);
                    break;
                case('R'):
                    token2 = strtok_r(NULL, ";", &save);
                    readNFileNode(clientSocketNumber, atoi(token2));
                    break;
                case('l'):
                    token2 = strtok_r(NULL, ";", &save);
                    lockFileNode(clientSocketNumber, token2);
                    break;
                case('u'):
                    token2 = strtok_r(NULL, ";", &save);;
                    unlockFileNode(clientSocketNumber, token2);
                    break;
                case('d'):
                    token2 = strtok_r(NULL, ";", &save);
                    removeFileNode(clientSocketNumber, token2);
                    break;
            }
        }
    }
    pthread_cond_signal(&fileListFree);
    return (void*)0;
}

int main(int argc, char *argv[]) {
    if(argc == 2) {
        parser(argv[1]);
    }
    int currConnection = 0;
    int serverSocket;
    int clientID;
    int max_client = 0;
    fd_set set;
    fd_set rdset;
    struct sockaddr_un sa;
    strncpy(sa.sun_path, "./socket", UNIX_PATH_MAX);
    sa.sun_family=AF_UNIX;
    if((serverSocket = socket(AF_UNIX,SOCK_STREAM, 0)) == -1) {
        perror("main.socket");
        exit(EXIT_FAILURE);
    }
    if(bind(serverSocket, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
        perror("main.bind");
        exit(EXIT_FAILURE);
    }
    if((listen(serverSocket, SOMAXCONN)) == -1) {
        perror("main.listen");
        exit(EXIT_FAILURE);
    }
    flog = fopen("./log.txt", "w");
    if(flog == NULL) {
        perror("main.fopen");
        exit(EXIT_FAILURE);
    }
    if(serverSocket > max_client){
        max_client = serverSocket;
    }
    sigset_t sigset;
    if(sigfillset(&sigset) == -1) {
        perror("main.sigfillset");
        exit(EXIT_FAILURE);
    }
    if(pthread_sigmask(SIG_SETMASK, &sigset, NULL) != 0) {
        perror("main.pthread_sigmask");
        exit(EXIT_FAILURE);
    }
    struct sigaction s;
    memset(&s, 0, sizeof(s));
    s.sa_handler = sigHandler;
    if(sigaction(SIGINT, &s, NULL) == -1) {
        perror("main.sigaction");
        exit(EXIT_FAILURE);
    }
    if(sigaction(SIGQUIT, &s, NULL) == -1) {
        perror("main.sigaction");
        exit(EXIT_FAILURE);
    }
    if(sigaction(SIGHUP, &s, NULL) == -1) {
        perror("main.sigaction");
        exit(EXIT_FAILURE);
    }
    s.sa_handler = SIG_IGN;
    if(sigaction(SIGPIPE, &s, NULL) == -1) {
        perror("main.sigaction");
        exit(EXIT_FAILURE);
    }
    if(pipe(selfPipe) == -1) {
        perror("main.pipe");
        exit(EXIT_FAILURE);
    }
    if(sigemptyset(&sigset) == -1) {
        perror("main.sigemptyset");
        exit(EXIT_FAILURE);
    }
    if(pthread_sigmask(SIG_SETMASK, &sigset, NULL) != 0) {
        perror("main.pthread_sigmask");
        exit(EXIT_FAILURE);
    }
    FD_ZERO(&set);
    FD_SET(serverSocket, &set);
    FD_SET(selfPipe[0], &set);
    pthread_t workerID;
    pthread_t *workerIDArray = malloc(nThread*sizeof(pthread_t));
    if(workerIDArray == NULL) {
        perror("main.malloc");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < nThread; i++) {
        if(pthread_create(&workerID, NULL, worker, NULL) != 0) {
            perror("main.pthread_create");
            exit(EXIT_FAILURE);
        }
        workerIDArray[i] = workerID;
    }
    int run = 1;
    printf("server pronto \n" );
    char h;
    while(run == 1) {
        rdset = set;
        if(select(max_client + 1, &rdset, NULL, NULL, NULL) == -1) { //controllo se segnale `e arrivato durante attesa di select
            if(end == -1) {
                read(selfPipe[0], &h, 1);
                emptyJobList();
                addExitNodeJobList();
                run = 0;
            }
            else {
                if(end == -2) {
                    FD_CLR(serverSocket, &set); //non accetta piu connessioni, viene ripetuto piu volte
                    if(currConnection == 0) {
                        read(selfPipe[0], &h, 1);
                        if(headJob == NULL) {
                            addExitNodeJobList();   
                            run = 0;
                        }
                    }
                }
                else {
                    perror("main.slect");
                    exit(EXIT_FAILURE);
                }
            }
        }
        else {
            for(int j = 0; j <= max_client; j++) {
                if(FD_ISSET(j,&rdset)) {
                    if(j == serverSocket) {
                        clientID = accept(serverSocket, NULL, 0);
                        if(clientID == -1) {
                            perror("main.accept");
                            exit(EXIT_FAILURE);
                        }
                        FD_SET(clientID, &set);
                        if(clientID > max_client) {
                            max_client = clientID;
                        }
                        currConnection++;
                        pthread_mutex_lock(&fileLog);
                        fprintf(flog, "thread=main;op=openConnection;read=0;write=0;path=NULL;answer=0 \n");
                        pthread_mutex_unlock(&fileLog);
                    }
                    else {
                        if(j == selfPipe[0]) {
                            read(selfPipe[0], &h, 1);
                            if(end == -1) {
                                emptyJobList();
                                addExitNodeJobList();
                                run = 0;
                            }
                            else {
                                if(end == -2) {
                                    FD_CLR(serverSocket, &set); //non accetta piu connessioni, viene ripetuto piu volte
                                    if(currConnection == 0) {
                                        if(headJob == NULL) {
                                            addExitNodeJobList();   
                                            run = 0;
                                        }
                                    }
                                }
                            }                            
                        }
                        else {
                            char* buffer = malloc((REQUEST + DATA ) * sizeof(char));
                            memset(buffer, 0, REQUEST + DATA);
                            int readByte = read(j, buffer, REQUEST + DATA);
                            if(readByte == -1) {
                                perror("main.read");
                                exit(EXIT_FAILURE);
                            }
                            else {
                                if(strlen(buffer) == 0) {
                                    currConnection--;
                                    FD_CLR(j, &set); //cancellare dal set
                                }
                                else {
                                    //printf("il messaggio ricevuto: %s, %d, %ld\n", buffer, j, strlen(buffer));
                                    addJobNode(buffer, j);
                                }
                            }
                            free(buffer);
                        }
                    }
                }
            }//ha finito di leggere tutti i socket pronti
            if(end == -2) {
                if(currConnection == 0) {
                    if(headJob == NULL) {
                        addExitNodeJobList();
                        run = 0;
                    }
                }
            }
        }
    }
    for(int i = 0; i < nThread; i++) {
        if(pthread_join(workerIDArray[i], NULL) != 0) {
            perror("main.join");
            exit(EXIT_FAILURE);
        }
    }
    free(workerIDArray);
    //inizia a fare lavori di fine
    fclose(flog);
    pthread_mutex_lock(&fileList); 
    fileNode* tmpFree = NULL;
    while(headFile != NULL) {
        tmpFree = headFile;
        printf("%s : %s \n", headFile->abspath, headFile->data);
        headFile = headFile->next;
        free(tmpFree->abspath);
        free(tmpFree->data);
        free(tmpFree);
    }
    pthread_mutex_unlock(&fileList);
    printf("massimo dimensione raggiunto : %d \n", maxStorageSizeHit);
    printf("massimo numero di file raggiunto : %d \n", maxFileNumberHit);
    printf("numerdo di algoritmo di rimpiazzamento : %d \n", outFile);
}