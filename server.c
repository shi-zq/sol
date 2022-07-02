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
int maxFileNumber = 10; // massimo numero di file
int maxStorageSize = 10*1024; // massimo capacita

int currentNumber = 0; //numero di file corrente
int currentSize = 0; //numero di capacita usato

int maxFileNumberHit = 0;
int maxStorageSizeHit = 0;
int outFile = 0;

FILE* flog = NULL;

jobNode* headJob = NULL; //FIFO
fileNode* headFile = NULL; //aggiungere in fondo

volatile sig_atomic_t end = 0;

pthread_mutex_t fileLog = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t jobList = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t jobListNotEmpty = PTHREAD_COND_INITIALIZER;
pthread_mutex_t fileList = PTHREAD_MUTEX_INITIALIZER;

void setMaxFileNumberHit(int newCurrFileNumber) {
    currentNumber = newCurrFileNumber;
    if(newCurrFileNumber > maxFileNumberHit) {
        maxFileNumber = newCurrFileNumber;
    }
}

void setMaxStorageSizeHit(int newCurrStorageSize) {
    currentSize = newCurrStorageSize;
    if(newCurrStorageSize > maxStorageSizeHit) {
        maxStorageSizeHit = newCurrStorageSize;
    }

}
static void sigHandler (int sig){
    if (sig == SIGINT || sig == SIGQUIT){
        end = -1; //chiudere il prima possibile
    }
    if (sig == SIGHUP){
        end = -2; //chiudere dopo aver finito
    }
}

void emptyJobList() {
    pthread_mutex_lock(&jobList);
    jobNode* tmp = NULL;
    while(headJob != NULL) { //svuola la lista di job
        tmp = headJob;
        headJob = headJob->next;
        free(tmp->requestData);
        free(tmp);
    }
    pthread_mutex_unlock(&jobList);
}

void addExitNodeJobList() { //aggiungere alla fine della headJob
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
        pthread_cond_signal(&jobListNotEmpty);
    }
    pthread_mutex_unlock(&jobList);
}

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
    }// cancellare tutti file lock da clientSocketNumber
    char response[REQUEST];
    memset(response, 0, REQUEST);
    sprintf(response, "0");
    if(write(clientSocketNumber, response, REQUEST) == -1) {
        perror("closeConnection.write");
        exit(EXIT_FAILURE);
    }    
    pthread_mutex_unlock(&fileList);
    pthread_mutex_lock(&fileLog);
    fprintf(flog, "%d;closeConnection;0;NULL;0 \n", gettid()); //thread;operazione;byte interessati;path;esito
    fflush(flog);
    pthread_mutex_unlock(&fileLog);
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
                answer = 1; //file esistente 
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
        /*case(O_CREATE): //se non c'e viene creato, 
            if(found == 0) {
                fileNode* new = malloc(sizeof(fileNode));
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
        case(O_LOCK):
            if(found == 0) {
                answer = -5;//file non esistente
            }
            else {
                if(answer == 1) { //esiste il file e non \`e lockato
                    tmp->clientSocketNumber = clientSocketNumber; //metto il lock
                } //altrimenti answer rimane -3
            }
            break;*/
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
                sprintf(new->abspath, "%s", path);
                if(prev == NULL) {
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
    sprintf(response, "%d", answer);
    if(write(clientSocketNumber, response, REQUEST) == -1) {
        perror("openFileNode.write");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_unlock(&fileList);
    pthread_mutex_lock(&fileLog);
    fprintf(flog, "%d;openFilenode;0;%s;%d \n", gettid(), path, answer); //thread;operazione;byte interessati;path;esito
    fflush(flog);
    pthread_mutex_unlock(&fileLog);
}

void writeFileNode(int clientSocketNumber, char* path, char* data) {
    pthread_mutex_lock(&fileList);
    int answer = 0;
    int found = 0;
    int success = 0;
    int freeSpace = 0;
    int freeFile = 0;
    fileNode* tmp = headFile;
    fileNode* prev = NULL;
    while(tmp != NULL && found == 0) {
        if(strcmp(path, tmp->abspath) == 0) {
            found = 1;
            if(tmp->clientSocketNumber == -1 || tmp->clientSocketNumber == clientSocketNumber) {
                if(currentNumber + 1 > maxFileNumber && currentSize + strlen(data) > maxStorageSize) { //controllo se posso mettere queto file
                    fileNode* tmp1 = headFile;
                    while(success == 0 && tmp1 != NULL) { //controllo quali sono file liberi
                        if(tmp1->clientSocketNumber == -1 && strcmp(tmp1->abspath, path) != 0 && tmp1->data != NULL) {
                            freeSpace = freeSpace + strlen(tmp1->data);
                            freeFile++;
                            answer++;
                        }
                        if(currentNumber - freeFile + 1 <= maxFileNumber && currentSize - freeSpace + strlen(tmp->data) <= maxStorageSize) {
                            success = 1;
                        }
                        else {
                            tmp1 = tmp1->next;
                        }
                    }
                    if(success == 0) {
                        //liberare questo nodo
                        if(prev == NULL) { // \`e il primo elemento
                            headFile = tmp->next;
                            free(tmp->abspath);
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
    char outFile[REQUEST + DATA];
    if(found == 0) {
        answer = -5;
    }
    if(answer < 0) {
        sprintf(response, "%d", answer);
        if(write(clientSocketNumber, response, REQUEST) == -1) {
            perror("writeFileNode.write");
            exit(EXIT_FAILURE);
        }
        pthread_mutex_lock(&fileLog);
        fprintf(flog, "%d;writeFileNode;0;%s;%d \n", gettid(), tmp->abspath, answer); //writeFIleNode fallito percio o per len
        fflush(flog);
        pthread_mutex_unlock(&fileLog);      
    }
    else { //answer >= 0
        sprintf(response, "%d", answer);
        tmp->data = malloc((strlen(data) + 1) * sizeof(char));
        if(tmp->data == NULL) {
            perror("writeFileNode.malloc");
            exit(EXIT_FAILURE);
        }
        sprintf(tmp->data, "%s", data); //copio la data
        currentNumber++;
        currentSize = currentSize + strlen(data);  
        pthread_mutex_lock(&fileLog);
        fprintf(flog, "%d;writeFileNode;%ld;%s;0 \n", gettid(), strlen(tmp->data), tmp->abspath);
        fflush(flog);
        pthread_mutex_unlock(&fileLog);      
        if(write(clientSocketNumber, response, REQUEST) == -1) {
            perror("writeFileNode.write");
            exit(EXIT_FAILURE);
        }
        fileNode* tmp2 = headFile;
        fileNode* prev2 = NULL;
        while(answer != 0) {
            outFile[0] = '\0';
            if(tmp2->clientSocketNumber == -1) { //il file da togliere
                sprintf(outFile, "%s;%s", tmp2->abspath, tmp2->data);
                if(write(clientSocketNumber, outFile, REQUEST + DATA) == -1) {
                    perror("writeFileNode.write");
                    exit(EXIT_FAILURE);
                }
                answer--;
                currentNumber--;
                currentSize = currentSize - strlen(tmp2->data);
                pthread_mutex_lock(&fileLog);
                fprintf(flog, "-1;out;%ld;%s;0 \n", strlen(tmp2->data), tmp2->abspath);
                fflush(flog);
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
            if(tmp->clientSocketNumber == clientSocketNumber || tmp->clientSocketNumber == -1) {
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
    sprintf(response, "%d", answer);
    if(write(clientSocketNumber, response, REQUEST) == -1) {
        perror("writeFileNode.write");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_unlock(&fileList);
    pthread_mutex_lock(&fileLog);
    fprintf(flog, "%d;closeFileNode;0;%s;%d \n", gettid(), path, answer);
    fflush(flog);
    pthread_mutex_unlock(&fileLog); 
    
}
void *worker() {
    //printf("sono %d \n", gettid());
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
                    closeFileNode(clientSocketNumber, strtok_r(NULL, ";", &save));
                    break;
                default:
                    printf("default \n");
            }
        }
    }
    return (void*)count;
}

int main() {
    unlink("./socket"); 
    unlink("./log");
    int currConnection = 0;
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
            if(end == -1) { //chiusura il prima possibile
                printf("inizio empty");
                emptyJobList();
                addExitNodeJobList();
                run = 0;
            }
            else {
                if(end == -2) { 
                    FD_CLR(serverSocket, &set); //non accetta piu connessioni
                    close(serverSocket);
                    rdset = set;
                    if(currConnection == 0) {
                        addExitNodeJobList();
                        run = 0;
                    }
                    else {
                        if(select(num_client + 1, &rdset, NULL, NULL, NULL) == -1) {
                            perror("main.select");
                            exit(EXIT_FAILURE);
                        }
                    }
                }
            }
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
                        currConnection++;
                        fprintf(flog, "new client \n");
                        fflush(flog);
                        pthread_mutex_unlock(&fileLog);
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
                                printf("end of file \n");
                                currConnection--;
                                FD_CLR(j, &set);
                                pthread_mutex_lock(&fileLog);
                                fprintf(flog, "client exit \n");
                                fflush(flog);
                                pthread_mutex_unlock(&fileLog);
                            }
                            else {
                                printf("il messaggio ricevuto: %s , %d, %ld\n", buffer, j, strlen(buffer));
                                addJobNode(buffer, j);
                            }
                        }
                        free(buffer);
                    }
                }
            }//ha finito di leggere tutti i socket pronti
            if(end == -1) {
                emptyJobList();
                addExitNodeJobList();
                run = 0;
            }
            if(end == -2) {
                FD_CLR(serverSocket, &set); //non accetta piu connessioni, viene ripetuto piu volte
                if(currConnection == 0) {
                    addExitNodeJobList();
                    run = 0;
                }
            }
        }
    }
    for(int i = 0; i < nThread; i++) {
        pthread_join(workerIDArray[i], NULL);
    }
    free(workerIDArray);
    //inizia a fare lavori di fine
    fclose(flog);
    pthread_mutex_lock(&fileList); 
    fileNode* tmpFree = NULL;
    while(headFile != NULL) {
        tmpFree = headFile;
        printf("%s : ", headFile->abspath);
        printf("%s \n", headFile->data);
        headFile = headFile->next;
        free(tmpFree->abspath);
        free(tmpFree->data);
        free(tmpFree);
    }
    pthread_mutex_unlock(&fileList);
    printf("massimo dimensione raggiunto : %d \n", maxStorageSizeHit);
    printf("massimo numero di file raggiunto : %d \n", maxFileNumberHit);
    //printf("numerdo di algoritmo di rimpiazzamento : %d \n", );
}