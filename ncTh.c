#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include "commonProto.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/poll.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include "Thread.h"
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define MYPORT "3543" // the port users will be connecting to
#define BACKLOG 5     // how many pending connections
#define TRUE 1
#define FALSE 0
#define MAXCONNECTIONS 5
#define MAXDATASIZE 100

int server_fds[100];
int nConnections = 0;
int currMaxConnections = 1;
sem_t mutex;

struct threadArgs
{
    int fd;
    struct commandOptions *cmdOps;
    clock_t startTime;
};

int server(struct commandOptions *commandOptions);
int client(struct commandOptions *commandOptions);
int createServerSocket(struct commandOptions *commandOptions);
void serverInThread();
void connectionThread(void *arg);
int createClientSocket(struct commandOptions *commandOptions);
void clientRecvThread(void *args);
void clientSendThread(void *args);
int pthread_yield(void);

int main(int argc, char **argv)
{

    // This is some sample code feel free to delete it
    // This is the main program for the thread version of nc

    struct commandOptions cmdOps;
    int retVal = parseOptions(argc, argv, &cmdOps);
    int VERBOSE = cmdOps.option_v;
    if (cmdOps.option_v)
    {
        printf("Command parse outcome %d\n", retVal);

        printf("-k = %d\n", cmdOps.option_k);
        printf("-l = %d\n", cmdOps.option_l);
        printf("-v = %d\n", cmdOps.option_v);
        printf("-r = %d\n", cmdOps.option_r);
        printf("-p = %d\n", cmdOps.option_p);
        printf("-p port = %u\n", cmdOps.source_port);
        printf("-w  = %d\n", cmdOps.option_w);
        printf("Timeout value = %u\n", cmdOps.timeout);
        printf("Host to connect to = %s\n", cmdOps.hostname);
        printf("Port to connect to = %u\n", cmdOps.port);
    }
    if (cmdOps.option_l)
    {
        if (cmdOps.option_p)
        {
            if (VERBOSE)
            {
                printf("It is an error to use options -l and -p together.\n");
            }
            exit(1);
        }
        //initialize the semaphore
        if (cmdOps.option_r)
        {
            currMaxConnections = MAXCONNECTIONS;
            sem_init(&mutex, 0, MAXCONNECTIONS);
        }
        else
        {
            sem_init(&mutex, 0, 1);
        }
        server(&cmdOps);
    }
    else
    {
        if (cmdOps.option_k)
        {
            if (VERBOSE)
            {
                printf("It is an error to use option -k without option -l.\n");
            }
            exit(1);
        }
        client(&cmdOps);
    }
}

// creates a socket to listen for incoming connections
int createServerSocket(struct commandOptions *commandOptions)
{
    // listen on sock_fd
    int sockfd;

    // hints, result of get address info, and a pointer to iterate over the linked list
    struct addrinfo hints, *servinfo, *p;
    socklen_t sin_size;
    char *address = commandOptions->hostname;
    unsigned int port = commandOptions->port;
    int VERBOSE = commandOptions->option_v;
    int rv;
    int bytesReceived;
    char *serverHost = "localhost";
    char *portNumber = (char *)malloc(sizeof(port));
    if (address != NULL)
    {
        serverHost = address;
    }
    if (port > 0)
    {
        sprintf(portNumber, "%u", port);
    }
    else
    {
        strcpy(portNumber, MYPORT);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;       // use IPv4
    hints.ai_socktype = SOCK_STREAM; // use TCP
    hints.ai_flags = AI_PASSIVE;     // use my IP address
    if (address != NULL)
    {
        serverHost = address;
    }
    if (port > 0)
    {
        sprintf(portNumber, "%u", port);
    }
    else
    {
        strcpy(portNumber, MYPORT);
    }
    if ((rv = getaddrinfo(serverHost, portNumber, &hints, &servinfo)) != 0)
    {
        if (VERBOSE)
        {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        }
        return 1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if (p->ai_family == AF_INET)
        {

            // get a socket descriptor
            if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
            {
                if (VERBOSE)
                {
                    perror("server: socket");
                }
            }

            int yes = 1;

            if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
            {
                if (VERBOSE)
                {
                    perror("sockopt = -1");
                }
                exit(1);
            }

            if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
            {
                close(sockfd);
                if (VERBOSE)
                {
                    perror("server: bind");
                }
                continue;
            }

            // successfully got a socket and binded
            break;
        }
        else
        {
            continue;
        }
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)
    {
        if (VERBOSE)
        {
            fprintf(stderr, "server: failed to bind\n");
        }
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1)
    {
        if (VERBOSE)
        {
            perror("listen");
        }
        exit(1);
    }
    free(portNumber);
    return sockfd;
}

int server(struct commandOptions *commandOptions)
{
    int listen_fd = createServerSocket(commandOptions);
    int new_sd;
    struct Thread *newConnectionThread;
    int *sem_value = (int *)malloc(sizeof(int));
    int fd_index;
    struct sockaddr_storage their_addr; // connector's address
    socklen_t sin_size;
    struct threadArgs *args = (struct threadArgs *)malloc(sizeof(struct threadArgs));

    pthread_attr_t attr;
    pthread_attr_init(&attr);

    struct Thread *serverInthread = createThread((void *) &serverInThread, &listen_fd);
    runThread(serverInthread, &attr);
    pthread_yield();
    while (TRUE)
    {
        new_sd = accept(listen_fd, (struct sockaddr *)&their_addr, &sin_size);

        sem_trywait(&mutex);

        // if the semaphore is at 0 ie. we cannot have more connections
        if (errno == EAGAIN)
        {
            close(new_sd);
            continue;
        }

        sem_getvalue(&mutex, sem_value);
        nConnections = currMaxConnections - *sem_value;

        fd_index = nConnections - 1;
        server_fds[fd_index] = new_sd;

        // create a new thread to monitor this connection
        args->fd = server_fds[fd_index];
        args->cmdOps = commandOptions;
        newConnectionThread = (struct Thread *)createThread((void *) &connectionThread, args);
        runThread(newConnectionThread, &attr);
        pthread_yield();
    }

    close(listen_fd);
    // end the process
    sem_destroy(&mutex);
    free(args);
    free(sem_value);
    exit(0);
}

// thread to monitor server's stdin to write to every connection
void serverInThread(void *listen_fd)
{

    int listen_fd_socket = *(int *)listen_fd;
    char send_buff[1000];
    int bytesToSend, bytesSent;

    while (fgets(send_buff, sizeof(send_buff), stdin))
    {
        bytesToSend = strlen(send_buff);

        for (int i = 0; i < nConnections; i++)
        {
            bytesSent = send(server_fds[i], send_buff, bytesToSend, 0);
        }
    }

    close(listen_fd_socket);
    // end the process
    sem_destroy(&mutex);
    exit(0);
}

// thread to monitor every connection to write to stdout
void connectionThread(void *args)
{
    struct threadArgs *arg = (struct threadArgs *)args;
    int this_fd = arg->fd;
    struct commandOptions *commandOptions = arg->cmdOps; // passed as the arg    int VERBOSE = commandOptions->option_v;
    char recv_buff[1000];
    int bytesSent, this_fd_index;
    int *sem_value = (int *)malloc(sizeof(int));
    int VERBOSE = commandOptions->option_v;
    int closeConnection = FALSE;
    if (VERBOSE)
    {
        printf("Descriptor %d is readable\n", this_fd);
    }

    // receive all incoming data on this socket before waiting for more data
    int bytesReceived;
    do
    {
        // receive data on this connection until the recv fails with EWOULDBLOCK
        // If any other failure occurs, we will close the connection
        bytesReceived = recv(this_fd, recv_buff, sizeof(recv_buff), MSG_DONTWAIT);

        if (bytesReceived < 0)
        {
            if (VERBOSE)
            {
                printf("Bytes received <0\n");
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                if (VERBOSE)
                {
                    perror("recv() failed No data ");
                }
                pthread_yield();
                continue;
            }
            break;
        }

        // check to see if the connection has been closed by client
        if (bytesReceived == 0)
        {
            if (VERBOSE)
            {
                printf("Connection closed\n");
            }
            break;
        }

        if (VERBOSE)
        {
            printf("%d bytes received\n", bytesReceived);
            // write to standard out
            printf("Client with fd %d says:\n", this_fd);
        }

        fwrite(recv_buff, sizeof(char), bytesReceived, stdout);
        // if (feof(server_fds[this_fd_index])!=0){
        for (int i = 0; i < nConnections; i++)
        {
            if (server_fds[i] != this_fd)
            {
                bytesSent = send(server_fds[i], recv_buff, bytesReceived, 0);
            }
            else
            {
                this_fd_index = i;
            }
        }
        // }
        pthread_yield();
    } while (TRUE);

    close(this_fd);

    server_fds[this_fd_index] = -1;

    // compress array
    sem_getvalue(&mutex, sem_value);
    nConnections = currMaxConnections - *sem_value;
    // printf("nconnn at the end = %d\n", nConnections);
    for (int i = 0; i < nConnections; i++)
    {
        if (server_fds[i] == -1)
        {
            for (int j = i; j < nConnections; j++)
            {
                server_fds[j] = server_fds[j + 1];
            }
            i--;
            break;
        }
    }

    sem_post(&mutex);
    sem_getvalue(&mutex, sem_value);
    nConnections = currMaxConnections - *sem_value;
    if (nConnections == 0 && !commandOptions->option_k)
    {
        exit(0);
    }
    free(sem_value);
}

int createClientSocket(struct commandOptions *commandOptions)
{
    int sockfd, numbytes;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_in client_addr;
    int rv;
    char *address = commandOptions->hostname;
    unsigned int port = commandOptions->port;
    unsigned int sourcePort = commandOptions->source_port;
    int VERBOSE = commandOptions->option_v;
    char buf[MAXDATASIZE];
    char *serverHost = NULL;
    char *portNumber = (char *)malloc(sizeof(port));
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       // use IPv4
    hints.ai_socktype = SOCK_STREAM; // use TCP
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = INADDR_ANY;
    client_addr.sin_port = htons(sourcePort);

    if (address != NULL)
    {
        serverHost = address;
    }
    if (port > 0)
    {
        sprintf(portNumber, "%u", port);
    }
    else
    {
        strcpy(portNumber, MYPORT);
    }

    if ((rv = getaddrinfo(address, portNumber, &hints, &servinfo)) != 0)
    {
        if (VERBOSE)
        {
            fprintf(stderr, "netcat: error in getaddrinfo() with source: %s\n", gai_strerror(rv));
        }
        exit(0);
    }

    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if (p->ai_family == AF_INET)
        {

            sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

            if (sockfd == -1)
            {
                if (VERBOSE)
                {
                    perror("netcat: error in socket()");
                }
                continue;
            }

            if (sourcePort > 0)
            {
                int yes = 1;
                if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
                {
                    if (VERBOSE)
                    {
                        perror("sockopt = -1");
                    }
                    exit(1);
                }
                if (bind(sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr)) == -1)
                {
                    close(sockfd);
                    if (VERBOSE)
                    {
                        perror("client: bind");
                    }
                    continue;
                }
            }
            if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
            {
                close(sockfd);
                if (VERBOSE)
                {
                    perror("netcat: error in connect()");
                }
                continue;
            }

            break;
        }
        else
        {
            continue;
        }
    }

    if (p == NULL)
    {
        if (VERBOSE)
        {
            printf("netcat: error could not connect\n");
        }
        exit(0);
    }
    freeaddrinfo(servinfo);
    free(portNumber);
    return sockfd;
}

int client(struct commandOptions *commandOptions)
{
    // we don't need a semaphore for the client
    sem_destroy(&mutex);
    int client_fd = createClientSocket(commandOptions);
    struct threadArgs *args = (struct threadArgs *)malloc(sizeof(struct threadArgs));
    args->fd = client_fd;
    args->cmdOps = commandOptions;
    args->startTime = clock();
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    struct Thread *recvThread = (struct Thread *)createThread((void *) &clientRecvThread, args);
    runThread(recvThread, &attr);
    struct Thread *sendThread = (struct Thread *)createThread((void *) &clientSendThread, args);
    runThread(sendThread, &attr);

    joinThread(recvThread, NULL);
    joinThread(sendThread, NULL);
}

// client thread that monitors the connection and writes to stdout
void clientRecvThread(void *args)
{
    char recv_buff[1000];
    int bytesReceived;
    int closeConnection = FALSE;
    struct threadArgs *arg = (struct threadArgs *)args;
    int this_fd = arg->fd;                               // passed as the arg
    struct commandOptions *commandOptions = arg->cmdOps; // passed as the arg
    int VERBOSE = commandOptions->option_v;
    int timeout = 0;
    if (commandOptions->timeout > 0)
    {
        timeout = (1000 * commandOptions->timeout);
    }
    // receive all incoming data on this socket before waiting for more data
    do
    {
        // receive data on this connection until the recv fails with EWOULDBLOCK
        // If any other failure occurs, we will close the connection
        // printf("in recv T %d\n", (((clock() - arg->startTime) * 1000 / CLOCKS_PER_SEC)));
        bytesReceived = recv(this_fd, recv_buff, sizeof(recv_buff), MSG_DONTWAIT);

        if (bytesReceived < 0)
        {
            if (VERBOSE)
            {
                printf("Bytes received <0\n");
            }

            if (errno != EWOULDBLOCK)
            {
                if (VERBOSE)
                {
                    perror("recv() failed");
                }
            }
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                if (VERBOSE)
                {
                    perror("recv() failed No data ");
                }
                pthread_yield();
                continue;
            }
            break;
        }

        // check to see if the connection has been closed by client
        if (bytesReceived == 0)
        {
            if (VERBOSE)
            {
                printf("Connection closed\n");
            }
            break;
        }

        if (VERBOSE)
        {
            printf("%d bytes received\n", bytesReceived);
        }
        // write to standard out
        fwrite(recv_buff, sizeof(char), bytesReceived, stdout);
        arg->startTime = clock();

    } while (!(commandOptions->timeout) || (((clock() - arg->startTime) * 1000 / CLOCKS_PER_SEC) < timeout));

    close(this_fd);
    exit(0);
    free(args);

    // return NULL;
}

// thread to monitor client's stdin to write to server
void clientSendThread(void *args)
{
    char send_buff[1000];
    int bytesToSend, bytesSent;

    struct threadArgs *arg = (struct threadArgs *)args;
    int this_fd = arg->fd;                               // passed as the arg
    struct commandOptions *commandOptions = arg->cmdOps; // passed as the arg

    int closeConnection = FALSE;
    int VERBOSE = commandOptions->option_v; // the commandOpts are passed in args
    int timeout = 0;
    if (commandOptions->timeout > 0)
    {
        timeout = 1000 * (commandOptions->timeout);
    }

    // send data
    do
    {
        // standard in is ready to read
        if (VERBOSE)
        {
            printf("client's stdin ready to read\n");
        }
        char *line = fgets(send_buff, sizeof(send_buff), stdin);

        if (line == NULL)
        {
            break;
        }

        bytesToSend = strlen(send_buff);
        bytesSent = send(this_fd, send_buff, bytesToSend, 0);
        if (bytesSent > 0)
        {
            arg->startTime = clock();
        }

    } while (!(commandOptions->timeout) || (((clock() - arg->startTime) * 1000 / CLOCKS_PER_SEC) < timeout));

    close(this_fd);
    exit(0);
}