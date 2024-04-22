#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <limits.h>
#include <pthread.h>
#include "myqueue.h"

#define SERVER_PORT 8989
#define BUFFER_SIZE 4096
#define SOCKET_ERROR (-1)
#define SERVER_BACKLOG 100
#define THREAD_POOL_SIZE 20

pthread_t thread_pool[THREAD_POOL_SIZE];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition_var = PTHREAD_COND_INITIALIZER; // condition variable: permite que threads esperem um evento acontecer

typedef struct sockaddr_in SA_IN;
typedef struct sockaddr SA;

void * handle_connection(void* p_client_socket);
int check(int exp, const char *msg);
void * thread_function(void *arg);

int main(int argc, char **argv)
{
    int server_socket, client_socket, addr_size;
    SA_IN server_addr, client_addr;

    check((server_socket = socket(AF_INET, SOCK_STREAM, 0)),
            "Failed to create socket.");
    
    // Inicializando thread pool
    for (int i = 0; i < THREAD_POOL_SIZE; i++)
    {
        pthread_create(&thread_pool[i], NULL, thread_function, NULL);
    }

    // Inicializando estrutura de Addr
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    check(bind(server_socket, (SA*) &server_addr, sizeof(server_addr)),
            "Failed to Bind.");
    check(listen(server_socket, SERVER_BACKLOG),
            "Failed to Listen.");
    
    while(true)
    {
        printf("Waiting for connections . . . \n");
        // Esperando para eventualmente aceitar conexão
        addr_size = sizeof(SA_IN);
        check(client_socket = 
                accept(server_socket, (SA*) &client_addr, (socklen_t*) &addr_size),
                "accept failed");
        printf("Connected!\n");

        // Colocar a informação em algum lugar para que a thread possa encontra-lo
        int *pclient = malloc(sizeof(int));
        *pclient = client_socket;
        pthread_mutex_lock(&mutex); // fecha a queue
        enqueue(pclient);
        pthread_cond_signal(&condition_var);
        pthread_mutex_unlock(&mutex); // abre a queue
    }

    return 0;
}

int check(int exp, const char *msg)
{
    if(exp == SOCKET_ERROR)
    {
        perror(msg);
        exit(1);
    }
    return exp;
}

void * thread_function(void *arg)
{
    while (true)
    {
        int *pclient;
        pthread_mutex_lock(&mutex);

        if ((pclient = dequeue()) == NULL)
        {
            pthread_cond_wait(&condition_var, &mutex);
            // try again
            pclient = dequeue();            
        }

        pthread_mutex_unlock(&mutex);

        if (pclient != NULL)
        {
           // Conexão estabelecida
           handle_connection(pclient); 
        }        
    }
}

void * handle_connection(void* p_client_socket)
{
    int client_socket = *((int*) p_client_socket);
    free(p_client_socket);
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    int msgsize = 0;
    char actualpath[PATH_MAX+1];

    // le a mensagem do cliente -- nome do arquivo para ler
    while((bytes_read = read(client_socket, buffer+msgsize, sizeof(buffer)-msgsize-1)) > 0)
    {
        msgsize += bytes_read;
        if (msgsize > BUFFER_SIZE-1 || buffer[msgsize-1] == '\n') break;
    }

    check(bytes_read, "recv error");
    buffer[msgsize-1] = 0; // null terminate o buffer e remove o \n

    printf("REQUEST: %s\n", buffer);
    fflush(stdout);

    // checagem de validação
    if (realpath(buffer, actualpath) == NULL)
    {
        printf("ERROR(bad path): %s\n", buffer);
        close(client_socket);
        return NULL;
    }

    // Le o arquivo e envia o conteúdo pro cliente
    FILE *fp = fopen(actualpath, "r");
    if(fp == NULL)
    {
        printf("ERROR(open): %s\n", buffer);
        close(client_socket);
        return NULL;
    }

    // le o conteúdo do arquivo e envia pro client
    // AVISO: não é um exemplo seguro
    // um programa real limitaria o client para determinato tipo de arquivos
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0)
    {
        printf("Sending %zu bytes\n", bytes_read);
        write(client_socket, buffer, bytes_read);
    }
    close(client_socket);
    fclose(fp);
    printf("Closing connection\n");

    return NULL;
}

