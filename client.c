/**
 * @file client_discovery.c
 * @brief A chat client that automatically discovers the server on the local network.
 *
 * This client does not require the server's IP address as an argument.
 * It first listens on a specific UDP port for a broadcast message from the
 * server. Once the server is found, it extracts its IP address and connects
 * to the chat service via TCP.
 *
 * Compilation:
 * gcc client_discovery.c -o client_discovery
 *
 * Usage:
 * ./client_discovery
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define TCP_PORT 8888
#define DISCOVERY_PORT 8889
#define BUFFER_SIZE 1024
#define DISCOVERY_MSG "CHAT_SERVER_HERE"

void get_my_address(int sock, char *buf) {
    struct sockaddr_in my_addr;
    socklen_t len = sizeof(my_addr);
    if (getsockname(sock, (struct sockaddr *)&my_addr, &len) < 0) {
        perror("getsockname");
        strcpy(buf, "unknown");
        return;
    }
    sprintf(buf, "%s:%d", inet_ntoa(my_addr.sin_addr), ntohs(my_addr.sin_port));
}

int main(int argc, char const *argv[]) {
    int discovery_sock, sock = 0;
    struct sockaddr_in serv_addr, broadcast_addr;
    char buffer[BUFFER_SIZE + 1] = {0};
    char server_ip[INET_ADDRSTRLEN];

    // --- Discover Server via UDP Broadcast ---
    printf("Searching for chat server on the local network...\n");
    if ((discovery_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("UDP socket creation error");
        return -1;
    }

    // Allow multiple sockets to use the same port number
    int reuse = 1;
    if (setsockopt(discovery_sock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse)) < 0) {
        perror("UDP setsockopt(SO_REUSEADDR) failed");
        close(discovery_sock);
        return -1;
    }

    // Bind the socket to the discovery port
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_addr.s_addr = INADDR_ANY;
    broadcast_addr.sin_port = htons(DISCOVERY_PORT);

    if (bind(discovery_sock, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0) {
        perror("UDP bind failed");
        close(discovery_sock);
        return -1;
    }

    // Wait to receive the broadcast message
    socklen_t serv_len = sizeof(serv_addr);
    int len = recvfrom(discovery_sock, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&serv_addr, &serv_len);
    if (len < 0) {
        perror("recvfrom failed");
        close(discovery_sock);
        return -1;
    }
    buffer[len] = '\0';

    // Check if the message is correct
    if (strcmp(buffer, DISCOVERY_MSG) == 0) {
        inet_ntop(AF_INET, &serv_addr.sin_addr, server_ip, INET_ADDRSTRLEN);
        printf("Server found at %s. Connecting to chat...\n", server_ip);
    } else {
        printf("Received unknown broadcast. Exiting.\n");
        close(discovery_sock);
        return -1;
    }
    close(discovery_sock); // We're done with discovery

    // --- Connect to Server via TCP for Chat ---
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\nTCP Socket creation error \n");
        return -1;
    }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(TCP_PORT); // Use the known TCP port

    // The server_ip was already populated during discovery
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/Address not supported \n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nTCP Connection Failed \n");
        return -1;
    }
    printf("Connected successfully! You can start typing now.\n");

    // --- Main Chat Loop ---
    fd_set readfds;
    char input_buffer[BUFFER_SIZE] = {0};
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sock, &readfds);

        select(sock + 1, &readfds, NULL, NULL, NULL);

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            int n = read(STDIN_FILENO, input_buffer, sizeof(input_buffer) - 1);
            if (n > 0) {
                if (input_buffer[n - 1] == '\n')
                    input_buffer[n - 1] = '\0';
                else
                    input_buffer[n] = '\0';
                if (strlen(input_buffer) == 0) continue;
                send(sock, input_buffer, strlen(input_buffer), 0);
                char my_addr_str[30];
                get_my_address(sock, my_addr_str);
                printf("Client <%s>: Message \"%s\" sent to server\n", my_addr_str, input_buffer);
                memset(input_buffer, 0, sizeof(input_buffer));
            }
        }

        if (FD_ISSET(sock, &readfds)) {
            int valread = read(sock, buffer, BUFFER_SIZE);
            if (valread > 0) {
                buffer[valread] = '\0';
                char *sender_info = buffer;
                char *message = strchr(buffer, ' ');
                if (message != NULL) {
                    *message = '\0';
                    message++;
                    printf("Client: Received Message \"%s\" from <%s>\n", message, sender_info);
                } else {
                    printf("Server broadcast: %s\n", buffer);
                }
                memset(buffer, 0, sizeof(buffer));
            } else if (valread == 0) {
                printf("Server disconnected.\n");
                close(sock);
                exit(0);
            }
        }
    }

    close(sock);
    return 0;
}
