#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <afxres.h>

#define PORT 8888
#define MAX_CLIENTS 30
#define BUFFER_SZ 2048

static _Atomic unisigned int cli_count = 0;
static int uid = 0;
int count[46] = {0};

typedef struct{
    struct sockaddr_in address;
    int sockfd;
    int uid;
    char username[32];
    int categoria;
    int coda;
} client_t;

client_t *clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void str_trim_lf(char* arr, int length){
    int i;
    for(i = 0; i < length; i++){
        if(arr[i] == '\n'){
            arr[i] = '\0';
            break;
        }
    }
}

//stampa l'indirizzo ip del client
void print_client_addr(struct sockaddr_in addr){
    printf("%d.%d.%d.%d\n", addr.sin_addr.s_addr & 0xff,
           (addr.sin_addr.s_addr & 0xff00) >> 8,
           (addr.sin_addr.s_addr & 0xff0000) >> 16,
           (addr.sin_addr.s_addr & 0xff000000) >> 24);
}

//aggiunge il client alla coda
void queue_add(client_t *cl){
    pthread_mutex_lock(&clients_mutex);

    for(int i=0; i<MAX_CLIENTS; i++){
        if(!clients[i]) {
            clients[i] = cl;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}


//rimuove il client dalla coda una volta disconesso
void queue_remove(int uid){
    pthread_mutex_lock(&clients_mutex);

    for(int i=0; i<MAX_CLIENTS; i++){
        if(!clients[i]) {
            if(clients[i]->uid == uid)
                clients[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

//manda messaggio a tutti client che si trovano nella stessa categoria ma non a se stesso
void send_message(char *s, int uid, int categoria){
    pthread_mutex_lock(&clients_mutex);
    for(int i=0; i<cli_count;i++){
        if(clients[i]){
            if(clients[i]->categoria == categoria && clients[i]->coda == 0 && clients[i]->categoria != 0){
                if(clients[i]->uid != uid){
                    if(write(clients[i]->sockfd, s, strlen(s))<0){
                        perror("ERRORE: Scrittura sul descrittore fallita");
                        break;
                    }
                }
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

//manda messaggio solo a se stesso
void send_self_message(char *s, int uid){
    pthread_mutex_lock(&clients_mutex);
    for(int i=0; i<cli_count;i++){
        if(clients[i]){
            if(clients[i]->uid == uid){
                if(write(clients[i]->sockfd, s, strlen(s))<0){
                    perror("ERRORE: Scrittura sul descrittore fallita");
                    break;
                }
            }
        }
    }
}

//manda messaggio a tutti i client che si trovano nella stessa categoria
void send_message_count(char *s, int categoria){
    pthread_mutex_lock(&clients_mutex);
    for(int i=0; i<cli_count;i++){
        if(clients[i]){
            if(clients[i]->categoria == categoria && clients[i]->categoria != 0){
                if(write(clients[i]->sockfd, s, strlen(s))<0){
                    perror("ERRORE: Scrittura sul descrittore fallita");
                    break;
                }
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

//gestione della connessione per il singolo client
void *handle_client(void *arg){
    char buff_out[BUFFER_SZ];
    char username[32];
    int leave_flag = 0;
    char message[BUFFER_SZ];
    int randomNumber = 0;
    int categoria;
    int flag = 0;

    //set del massimo numero di utenti in una stanza
    int maxUserChat = 2;

    cli_count++;
    client_t *cli = (client_t *)arg;

    cli->coda=0;
    //prendo dal client nome utente e categoria scelta
    recv(cli->sockfd, username, 32, 0);
    recv(cli->sockfd, buff_out, BUFFER_SZ, 0);
    categoria = atoi(buff_out);
    if(username != NULL && categoria != 0) {
        //randomNumber = rand()%3;
        cli->categoria = categoria + (randomNumber * 5);
        str_trim_lf(username, 32);
        str_trim_lf(buff_out, BUFFER_SZ);
        strcpy(cli->username, username);

        //controllo se la stanza è piena
        if (count[cli->categoria] == maxUserChat) {
            cli->coda = 1;
            printf("%s è in attesa. La stanza %d è piena\n", cli->username, cli->categoria);
            strcpy(buff_out, "@@");
            sprintf(message, "%s\n", buff_out);
            _sleep(1);
            send_self_message(message, cli->uid);
            bzero(buff_out, BUFFER_SZ);

            while (1) {
                //se si libera un posto nella stanza rimuovo dall'attesa il client
                if (count[cli->categoria] < maxUserChat) {
                    strcpy(buff_out, "çç\n");
                    sprintf(message, "%s", buff_out);
                    cli->coda = 0;
                    flag = 1;
                    send_self_message(message, cli->uid);
                    sprintf(buff_out, "%s è entrato nella chat", cli->username);
                    count[cli->categoria] = maxUserChat;
                    sprintf(message, "%s|%d\n", buff_out, count[cli->categoria]);
                    break;
                }
            }
        }

        bzero(buff_out, BUFFER_SZ);
        sprintf(buff_out, "%s è entrato nella chat", cli->username);
        printf("%s(%d)[%d]\n", buff_out, cli->categoria, cli->sockfd);

        if (flag == 0) {
            count[cli->categoria]++;
            printf("users(%d): %d\n", cli->categoria, count[cli->categoria]);
            sprintf(message, "%s|%d\n", buff_out, count[cli->categoria]);
            _sleep(1);
            send_message_count(message, cli->categoria);
        } else {
            flag = 0;
            printf("users(%d): %d\n", cli->categoria, count[cli->categoria]);
            sprintf(message, "%s|%d\n", buff_out, count[cli->categoria]);
            send_message(message, cli->uid, cli->categoria);
            _sleep(1);
            send_self_message(message, cli->uid);
        }

        bzero(buff_out, BUFFER_SZ);

        //il client si trova nella chat
        while (1) {
            bzero(buff_out, BUFFER_SZ);
            if (leave_flag)
                break;
            int receive = recv(cli->sockfd, buff_out, BUFFER_SZ, 0);
            if (receive > 0) {
                if (strcmp(buff_out, "ananasbananamela\n") == 0) {
                    sprintf(buff_out, "%s è uscito dalla chat", cli->username);
                    printf("%s(%d)\n", buff_out, cli->categoria);
                    count[cli->categoria]--;
                    printf("users(%d): %d\n", cli->categoria, count[cli->categoria]);
                    sprintf(message, "%s|%d\n", buff_out, count[cli->categoria]);
                    send_message(message, cli->uid, cli->categoria);
                    leave_flag = 1;
                    cli->categoria = 0;
                    _sleep(1);
                } else if (strlen(buff_out) > 0) {
                    sprintf(message, "%s#%s", cli->username, buff_out);
                    printf("%s(%d) -> %s", cli->username, buff_out);
                    send_message(message, cli->uid, cli->categoria);
                    bzero(buff_out, BUFFER_SZ);
                }
            }
        }
        cli->categoria = 0;
        flag = 0;
        close(cli->sockfd);
        queue_remove(cli->uid);
        free(cli);
        cli_count--;
    }
    return NULL;
}

int main(int argc, char **argv){
    int option = 1;
    int listenfd = 0, connfd = 0, master_socket;
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;
    pthread_t tid;
    int new_socket, activity;
    int max_sd;
    fd_set readfds;

    srand(time(NULL));

    //apertura connessione con porta 8888
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = hotnos(PORT);

    signal(SIGPIPE, SIG_IGN);

    //crea il socket
    if((master_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0){
        perror("socket fallito");
        exit(EXIT_FAILURE);
    }

    //setta il socket
    if(setsockopt(master_socket, SOL_SOCKET, (SO_REUSEPORT | SO_REUSEADDR), (char*)&option, sizeof(option)) < 0){
        perror("ERRORE: setsockopt fallita");
        return EXIT_FAILURE;
    }

    //bind del socket
    if(bind(master_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
        perror("Errore bind fallita");
        return EXIT_FAILURE;
    }
    printf("In ascolto sulla porta %d \n", PORT);

    if(listen(master_socket, 1) < 0){
        perror("ERRORE: Ascolto sul socket fallito");
        return EXIT_FAILURE;
    }

    while(1){

        FD_ZERO(&readfds);

        FD_SET(master_socket, &readfds);
        max_sd = master_socket;
        socklen_t clien = sizeof(cli_addr);

        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if((activity < 0) && (errno != ENTER)){
            printf("select error");
        }

        //accettazione delle connessioni
        if(FD_ISSET(master_socket, &readfds)){
            sleep(1);
            if((new_socket = accept(master_socket, (struct sockaddr *)&cli_addr), (sock_len_t*)&client)) < 0){
                perror("accept");
                ecit(EXIT_FAILURE);
            }

        client_t *cli = NULL

        if((cli_count + 1) == MAX_CLIENTS){
            printf("Client massimi raggiunti. Respinto: ");
            print_client_addr(cli_addr);
            printf(":%d\n", cli_addr.sin_port);
            close(new socket);
            continue;
        }

        //assegnamento dei dati ai vari clients
        cli = (client_t *)malloc(sizeof(client_t));
        cli->address = cli_addr;
        cli->sockfd = new_socket;
        printf("[%d]connessione accettata da ip:", cli->sockfd);
        print_client_addr(cli->address);
        cli->uid = uid++;

        queue_add(cli);
        pthread_create(&tid, NULL, &handle_client, (void*)cli);
        }

        sleep(1);
    }

    return EXIT_SUCCESS;
}



