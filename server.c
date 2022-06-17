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

typedef struct jobNode {
    char* requestData;
    int clientSocketNumber;
    struct jobNode* next;
} jobNode;

typedef struct closeNode {
    int clientSocketNumber;
    struct closeNode* next;
} closeNode;

typedef struct fileNode {
    char* abspath;
    char* data;
    int clientSocketNumber;
    struct fileNode* next;
} fileNode;

int nThread = 2; // numero di thread

FILE* flog = NULL;

closeNode* headClose = NULL; //LIFO
jobNode* headJob = NULL; //FIFO
fileNode* headFile = NULL; 

pthread_mutex_t closeList = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t fileLog = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t jobList = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t jobListNotEmpty = PTHREAD_COND_INITIALIZER;
pthread_mutex_t fileList = PTHREAD_MUTEX_INITIALIZER;

void addJobNode(char* requestData, int clientSocketNumber) {
    pthread_mutex_lock(&jobList);
    jobNode* new = malloc(sizeof(jobNode));
    new->requestData = malloc((strlen(requestData) + 1) * sizeof(char));
    sprintf(new->requestData, "%s", requestData);
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
    pthread_cond_signal(&jobListNotEmpty);
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
    }
    char answer[REQUEST];
    sprintf(answer, "0");
    if(write(clientSocketNumber, answer, REQUEST) == -1) {
        perror("closeConnection.write");
        exit(EXIT_FAILURE);
    }    
    pthread_mutex_unlock(&fileList);
    pthread_mutex_lock(&closeList);
    closeNode* new = malloc(sizeof(closeNode));
    if(new == NULL) {
        perror("closeConnection.malloc");
        exit(EXIT_FAILURE);
    }
    new->clientSocketNumber = clientSocketNumber;
    new->next = headClose;
    headClose = new;
    pthread_mutex_unlock(&closeList);
    pthread_mutex_lock(&fileLog);
    fprintf(flog, "exit client \n");
    fflush(flog);
    pthread_mutex_unlock(&fileLog);
}

void *worker(){
    int running = 1;
    while(running == 1 ) {
        pthread_mutex_lock(&jobList);
        while(headJob == NULL) {
            pthread_cond_wait(&jobListNotEmpty, &jobList);
        }
        int clientSocketNumber = headJob->clientSocketNumber;
        char requestData[REQUEST + DATA];
        sprintf(requestData, "%s", headJob->requestData);
        jobNode* tmp = headJob->next;
        free(headJob->requestData);
        free(headJob);
        headJob = tmp;
        pthread_mutex_unlock(&jobList);
        if(clientSocketNumber == -1) {
            running = 0;
        }
        else {
            char* save = NULL;
            char* token = strtok_r(requestData, ";", &save);
            switch(token[0]) { //token ha solo una lettera
                case('c'):
                    closeConnection(clientSocketNumber);
                    break;
            }
        }
    }


}

int main() {
    unlink("./socket"); 
    unlink("./log");
    int serverSocket;
    int clientID;
    int num_client = 0;
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
    if(serverSocket > num_client){
        num_client = serverSocket;
    }
    FD_ZERO(&set);
    FD_SET(serverSocket, &set);
    pthread_t workerID;
    int *workerIDArray = malloc(nThread*sizeof(int));
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
    while(run == 1) {
        rdset = set;
        if(select(num_client + 1, &rdset, NULL, NULL, NULL) == -1) {
            perror("main.select");
            exit(EXIT_FAILURE);
        }
        else {
            for(int j = 0; j <= num_client; j++) {
                if(FD_ISSET(j,&rdset)) {
                    if(j == serverSocket) {
                        clientID = accept(serverSocket, NULL, 0);
                        if(clientID == -1) {
                            perror("main.accept");
                            exit(EXIT_FAILURE);
                        }
                        FD_SET(clientID, &set);
                        if(clientID > num_client) {
                            num_client = clientID;
                        }
                        pthread_mutex_lock(&fileLog);
                        fprintf(flog, "new client \n");
                        fflush(flog);
                        pthread_mutex_unlock(&fileLog);
                    }
                    else {

                        char* buffer = malloc((REQUEST + DATA ) * sizeof(char));
                        int readByte = read(j, buffer, REQUEST + DATA);
                        FD_CLR(j, &set);
                        if(readByte == -1 ) {
                            perror("main.read");
                            exit(EXIT_FAILURE);
                        }
                        else {
                            addJobNode(buffer, j);
                        }
                        free(buffer);
                        
                    }
                }
            } //ha finito di leggere tutti i socket pronti
        }
        pthread_mutex_lock(&closeList);
        closeNode *tmp;
        while(headClose != NULL) {
            FD_CLR(headClose->clientSocketNumber, &set);
            close(headClose->clientSocketNumber);
            tmp = headClose;
            headClose = headClose->next;
            free(tmp);
        }
        pthread_mutex_unlock(&closeList);

    }
}