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

#define MYPORT "3543" // the port users will be connecting to
#define BACKLOG 5     // how many pending connections
#define TRUE 1
#define FALSE 0
#define MAXDATASIZE 100

int server(struct commandOptions *commandOptions);
int createServerSocket(struct commandOptions *commandOptions);
int serverPolling(int sockfd, struct commandOptions *commandOptions);
int client(struct commandOptions *commandOptions);
int createClientSocket(struct commandOptions *commandOptions);
int clientPolling(int sockfd, struct commandOptions *commandOptions);

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
        int server_fd = server(&cmdOps);
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
        int client_fd = client(&cmdOps);
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
    char* address = commandOptions->hostname;
    unsigned int port = commandOptions -> port;
    int VERBOSE = commandOptions -> option_v;
    char *serverHost = NULL;
    char *portNumber = (char *)malloc(sizeof(port));

    int rv;
    int bytesReceived;
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

    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    return sockfd;
}

int server(struct commandOptions *commandOptions)
{
    int listenfd = createServerSocket(commandOptions);
    serverPolling(listenfd, commandOptions);
}

// Server performs polling and reads from a connection and writes to stdout and every other connection
int serverPolling(int sockfd, struct commandOptions *commandOptions)
{
    struct pollfd fds[100];
    int timeout;
    int nfds = 2, currentSize = 0;
    int pollin_happened = FALSE;
    struct sockaddr_storage their_addr; // connector's address
    socklen_t sin_size;
    int new_sd;
    int closeConnection;
    int endServer = FALSE, compress_array = FALSE;
    int rv;
    char buffer[1000];
    char send_buff[1000];
    int bytesToSend, bytesSent;
    int VERBOSE = commandOptions->option_v;

    //initialize the poll fd structure
    memset(fds, 0, sizeof(fds));

    // set up stdin
    fds[0].fd = 0;
    fds[0].events = POLLIN;

    // set up the initial listening socket
    fds[1].fd = sockfd;
    fds[1].events = POLLIN;

    // The server should never timeout no matter what the timeout value given
    timeout = -1;

    // Loop waiting for incoming connects or for incoming data on any of the connected sockets
    do
    {
        // call poll and wait for 3 minutes for it to expire
        if (VERBOSE)
        {
            printf("Waiting on poll()...\n");
        }
        rv = poll(fds, nfds, timeout);
        if (VERBOSE)
        {
            printf("Awake from poll\n");
        }

        // check to see if the poll call failed.
        if (rv < 0)
        {

            if (VERBOSE)
            {
                perror("poll() failed");
            }
            break;
        }

        // one or more descriptors are readable. Need to determine which ones they are
        currentSize = nfds;

        for (int i = 0; i < currentSize; i++)
        {
            // Loop through to find the descriptors that returned
            // POLLIN and determine whether it's the listening or the active connection

            pollin_happened = fds[i].revents & POLLIN;

            if (fds[i].revents == 0)
            {
                continue;
            }

            if (fds[i].revents != POLLIN)
            {
                if (VERBOSE)
                {
                    printf("Error! revents = %d. End Program.\n", fds[i].revents);
                }
                endServer = TRUE;
                break;
            }

            if (pollin_happened)
            {
                // file descriptor is ready to read
                if (VERBOSE)
                {
                    printf("file descriptor %d is ready to read\n", fds[i].fd);
                }
            }

            if (fds[i].fd == sockfd)
            {
                // Listening descriptor is readable
                if (VERBOSE)
                {
                    printf("Listening socket is readable\n");
                }

                // printf("new_sd is %d\n", new_sd);
                do
                {
                    if ((!commandOptions->option_r && nfds == 3) || (commandOptions->option_r && nfds == 7))
                    {
                        if (VERBOSE)
                        {
                            printf("No more connections allowed\n");
                        }
                        new_sd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
                        close(new_sd);
                        break;
                    }
                    if (VERBOSE)
                    {
                        printf("start of loop \n");
                    }
                    // Accept all incoming connection that are queued up on the listening socket
                    // before we loop back and call poll again

                    // printf("Checkpoint\n");
                    new_sd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
                    // printf("After checkpoint, new_sd is %d\n", new_sd);

                    if (new_sd < 0)
                    {
                        if (errno != EWOULDBLOCK)
                        {
                            if (VERBOSE)
                            {
                                perror("accept failed");
                            }
                            //end program
                            endServer = TRUE;
                        }
                        break;
                    }

                    // add the new incoming connection to the pollfd structure
                    if (VERBOSE)
                    {
                        printf("New incoming connection - %d\n", new_sd);
                    }
                    fds[nfds].fd = new_sd;
                    // we're interested in whenever this socket is ready to be read
                    fds[nfds].events = POLLIN;
                    nfds++;
                    // loop back up and accept another incoming connection
                } while (new_sd != -1);
            }

            else if (fds[i].fd == 0)
            {
                // standard in is ready to read
                if (VERBOSE)
                {
                    printf("server's stdin ready to read\n");
                }
                char *line = fgets(send_buff, sizeof(send_buff), stdin);
                if (line == NULL)
                {
                    endServer = TRUE;
                    break;
                }
                bytesToSend = strlen(send_buff);

                for (int j = 2; j < currentSize; j++)
                {
                    bytesSent = send(fds[j].fd, send_buff, bytesToSend, 0);
                }
            }
            else
            {
                if (VERBOSE)
                {
                    printf("Descriptor %d is readable\n", fds[i].fd);
                }
                closeConnection = FALSE;

                // receive all incoming data on this socket before we loop back and call poll again
                int bytesReceived;
                do
                {
                    // receive data on this connection until the recv fails with EWOULDBLOCK
                    // If any other failure occurs, we will close the connection
                    bytesReceived = recv(fds[i].fd, buffer, sizeof(buffer), MSG_DONTWAIT);

                    if (bytesReceived < 0)
                    {
                        if (VERBOSE)
                        {
                            printf("Bytes received <0\n");
                        }
                        if (errno != EWOULDBLOCK)
                        {
                            perror("recv() failed");
                            closeConnection = TRUE;
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
                        closeConnection = TRUE;
                        break;
                    }
                    if (VERBOSE)
                    {
                        printf("%d bytes received\n", bytesReceived);
                        // write to standard out

                        printf("Client with fd %d says:\n", fds[i].fd);
                    }
                    fwrite(buffer, sizeof(char), bytesReceived, stdout);

                    // write to every other connection except the one currently read
                    int bytesSent;
                    int totalByteSent;

                    for (int j = 2; j < currentSize; j++)
                    {
                        totalByteSent = 0;
                        if (i != j)
                        {

                            while (totalByteSent < bytesReceived)
                            {
                                bytesSent = send(fds[j].fd, buffer, bytesReceived, 0);
                                totalByteSent += bytesSent;

                                if (bytesSent < 0)
                                {
                                    if (VERBOSE)
                                    {
                                        perror("send() failed");
                                    }
                                    closeConnection = TRUE;
                                    break;
                                }
                            }
                        }
                    }

                } while (TRUE);

                // if the closeConnection flag was turned on, we need to clean up this active connection
                // This clean up process includes removing the descriptor
                if (closeConnection)
                {
                    close(fds[i].fd);
                    fds[i].fd = -1;
                    compress_array = TRUE;

                    // check if there are any connections left and if we should end the server
                    if (!commandOptions->option_k && nfds == 2)
                    {
                        endServer = TRUE;
                    }
                }
            }
        }

        /* If the compress_array flag was turned on, we need       */
        /* to squeeze together the array and decrement the number  */
        /* of file descriptors. We do not need to move back the    */
        /* events and revents fields because the events will always*/
        /* be POLLIN in this case, and revents is output.          */

        if (compress_array)
        {
            compress_array = FALSE;
            for (int i = 0; i < nfds; i++)
            {
                if (fds[i].fd == -1)
                {
                    for (int j = i; j < nfds; j++)
                    {
                        fds[j].fd = fds[j + 1].fd;
                    }
                    i--;
                    nfds--;
                }
            }
        }
    } while (endServer == FALSE);
}

int createClientSocket(struct commandOptions* commandOptions)
{
    int sockfd, numbytes;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_in client_addr;
    int rv;
    char buf[MAXDATASIZE];
    char* address = commandOptions->hostname;
    unsigned int port = commandOptions -> port;
    unsigned int sourcePort = commandOptions -> source_port;

    int VERBOSE = commandOptions -> option_v;
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
    return sockfd;
}

int client(struct commandOptions *commandOptions)
{

    int clientfd = createClientSocket(commandOptions);
    clientPolling(clientfd, commandOptions);
}

int clientPolling(int sockfd, struct commandOptions *commandOptions)
{

    char recv_buff[1000];
    char send_buff[1000];
    int bytesSent, bytesToSend;
    int closeConnection = FALSE;
    struct pollfd client_fds[2];
    int timeout;
    int nfds = 2;
    int rv;
    int pollin_happened;
    int VERBOSE = commandOptions->option_v;

    //initialize the poll fd structure
    memset(client_fds, 0, sizeof(client_fds));

    // set up stdin
    client_fds[0].fd = 0;
    client_fds[0].events = POLLIN;

    // set up the connection socket
    client_fds[1].fd = sockfd;
    client_fds[1].events = POLLIN;

    if (commandOptions->timeout != 0)
    {
        timeout = (1000 * commandOptions->timeout);
    }
    else
    {
        timeout = -1;
    }

    do
    {
        // call poll and wait for given timeout to expire
        if (VERBOSE)
        {
            printf("Waiting on poll()...\n");
        }
        rv = poll(client_fds, nfds, timeout);
        if (VERBOSE)
        {
            printf("Awake from poll\n");
        }

        // check to see if the poll call failed.
        if (rv < 0)
        {
            if (VERBOSE)
            {
                perror("poll() failed");
            }
            break;
        }

        // No events happened
        if (rv == 0)
        {
            if (VERBOSE)
            {
                printf("poll() timed out. End Program. \n");
            }
            closeConnection = TRUE;
            break;
        }

        // one or more descriptors are readable. Need to determine which ones they are

        for (int i = 0; i < 2; i++)
        {
            // Loop through to find the descriptors that returned
            // POLLIN and determine whether it's the listening or the active connection

            pollin_happened = client_fds[i].revents & POLLIN;

            if (client_fds[i].revents == 0)
            {
                continue;
            }

            if (client_fds[i].revents != POLLIN)
            {
                if (VERBOSE)
                {
                    printf("Error! revents = %d. End Program.\n", client_fds[i].revents);
                }
                closeConnection = TRUE;
                break;
            }

            if (pollin_happened)
            {
                // file descriptor is ready to read
                if (VERBOSE)
                {
                    printf("file descriptor %d is ready to read\n", client_fds[i].fd);
                }
            }

            if (client_fds[i].fd == 0)
            {
                // standard in is ready to read
                if (VERBOSE)
                {
                    printf("client's stdin ready to read\n");
                }
                char *line = fgets(send_buff, sizeof(send_buff), stdin);
                if (line == NULL)
                {
                    closeConnection = TRUE;
                    break;
                }

                bytesToSend = strlen(send_buff);
                bytesSent = send(sockfd, send_buff, bytesToSend, 0);
            }
            else
            {
                if (VERBOSE)
                {
                    printf("Descriptor %d is readable\n", client_fds[i].fd);
                }
                closeConnection = FALSE;

                // receive all incoming data on this socket before we loop back and call poll again
                int bytesReceived;
                do
                {
                    // receive data on this connection until the recv fails with EWOULDBLOCK
                    // If any other failure occurs, we will close the connection
                    bytesReceived = recv(client_fds[i].fd, recv_buff, sizeof(recv_buff), MSG_DONTWAIT);

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
                            closeConnection = TRUE;
                        }
                        break;
                    }

                    // check to see if the connection has been closed by server
                    if (bytesReceived == 0)
                    {
                        if (VERBOSE)
                        {
                            printf("Connection closed\n");
                        }
                        closeConnection = TRUE;
                        break;
                    }

                    if (VERBOSE)
                    {
                        printf("%d bytes received\n", bytesReceived);

                        // write to standard out
                        printf("Server Says:\n");
                    }
                    fwrite(recv_buff, sizeof(char), bytesReceived, stdout);

                } while (TRUE);

                // if the closeConnection flag was turned on, we need to clean up this active connection
                // This clean up process includes removing the descriptor
                if (closeConnection)
                {
                    close(client_fds[1].fd);
                    client_fds[1].fd = -1;
                }
            }
        }
    } while (!closeConnection);
}