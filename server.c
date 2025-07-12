/**
 * @file server_discovery.c
 * @brief A multi-client chat server with automatic discovery via UDP broadcast.
 *
 * This server listens for TCP connections for the chat functionality.
 * Additionally, it creates a UDP socket to broadcast a discovery message
 * every 5 seconds, allowing clients on the same local network to find it
 * automatically without needing to know its IP address beforehand.
 *
 * Compilation:
 * gcc server_discovery.c -o server_discovery
 *
 * Usage:
 * ./server_discovery
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define TCP_PORT 8888
#define DISCOVERY_PORT 8889
#define MAX_CLIENTS 5
#define BUFFER_SIZE 1024
#define DISCOVERY_MSG "CHAT_SERVER_HERE"

void get_client_address(int sockfd, char *addr_buf) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_len) < 0) {
        perror("getpeername");
        strcpy(addr_buf, "unknown");
        return;
    }
    sprintf(addr_buf, "%s:%d", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
}

int main(int argc, char *argv[]) {
    int opt = 1;
    int master_socket, addrlen, new_socket;
    int client_socket[MAX_CLIENTS] = {0};
    int num_clients = 0;

    int discovery_socket;
    struct sockaddr_in address, broadcast_addr;
    char buffer[BUFFER_SIZE + 1];

    fd_set readfds;
    struct timeval timeout;

    // --- Setup TCP Chat Server ---
    if ((master_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("TCP socket failed");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0) {
        perror("TCP setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(TCP_PORT);
    if (bind(master_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("TCP bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(master_socket, MAX_CLIENTS) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    printf("Chat server listening on TCP port %d\n", TCP_PORT);

    // --- Setup UDP Discovery Service ---
    if ((discovery_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("UDP socket failed");
        exit(EXIT_FAILURE);
    }
    // Enable broadcasting
    if (setsockopt(discovery_socket, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        perror("UDP setsockopt broadcast failed");
        close(discovery_socket);
        exit(EXIT_FAILURE);
    }
    // Configure broadcast address
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(DISCOVERY_PORT);
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255"); // Broadcast address

    printf("Broadcasting presence on UDP port %d\n", DISCOVERY_PORT);
    addrlen = sizeof(address);
    puts("Waiting for connections ...");

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(master_socket, &readfds);
        int max_sd = master_socket;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_socket[i] > 0) {
                FD_SET(client_socket[i], &readfds);
            }
            if (client_socket[i] > max_sd) {
                max_sd = client_socket[i];
            }
        }

        // Set timeout for select() to handle periodic broadcast
        timeout.tv_sec = 5; // Broadcast every 5 seconds
        timeout.tv_usec = 0;

        int activity = select(max_sd + 1, &readfds, NULL, NULL, &timeout);

        if ((activity < 0) && (errno != EINTR)) {
            printf("select error");
        }

        // --- Handle TCP Connections ---
        if (FD_ISSET(master_socket, &readfds)) {
            if ((new_socket = accept(master_socket, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
                perror("accept");
                exit(EXIT_FAILURE);
            }
            char client_addr_str[30];
            get_client_address(new_socket, client_addr_str);
            printf("Server: Received a new connection from client %s\n", client_addr_str);
            if (num_clients < MAX_CLIENTS) {
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (client_socket[i] == 0) {
                        client_socket[i] = new_socket;
                        num_clients++;
                        break;
                    }
                }
            } else {
                printf("Max clients reached. Connection from %s rejected.\n", client_addr_str);
                send(new_socket, "Server is full. Try again later.\n", 33, 0);
                close(new_socket);
            }
        }

        // --- Handle TCP Messages ---
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_socket[i];
            if (sd > 0 && FD_ISSET(sd, &readfds)) {
                int valread;
                if ((valread = read(sd, buffer, BUFFER_SIZE)) == 0) {
                    char client_addr[30];
                    get_client_address(sd, client_addr);
                    printf("Client %s disconnected\n", client_addr);
                    close(sd);
                    client_socket[i] = 0;
                    num_clients--;
                } else {
                    buffer[valread] = '\0';
                    char sender_addr[30];
                    get_client_address(sd, sender_addr);
                    if (buffer[strlen(buffer) - 1] == '\n') buffer[strlen(buffer) - 1] = '\0';
                    printf("Server: Received message \"%s\" from client %s\n", buffer, sender_addr);

                    if (num_clients < 2) {
                        printf("Server: Insufficient clients, \"%s\" from client %s dropped\n", buffer, sender_addr);
                    } else {
                        char broadcast_msg[BUFFER_SIZE + 50];
                        snprintf(broadcast_msg, sizeof(broadcast_msg), "%s %s", sender_addr, buffer);
                        for (int j = 0; j < MAX_CLIENTS; j++) {
                            int dest_sd = client_socket[j];
                            if (dest_sd > 0 && dest_sd != sd) {
                                send(dest_sd, broadcast_msg, strlen(broadcast_msg), 0);
                                char recipient_addr[30];
                                get_client_address(dest_sd, recipient_addr);
                                printf("Server: Send message \"%s\" from client %s to %s\n", buffer, sender_addr, recipient_addr);
                            }
                        }
                    }
                }
            }
        }

        // --- Send UDP Discovery Broadcast on Timeout ---
        if (activity == 0) { // select() timed out
            sendto(discovery_socket, DISCOVERY_MSG, strlen(DISCOVERY_MSG), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
            // printf("Sent discovery broadcast.\n"); // Uncomment for debugging
        }
    }
    return 0;
}
