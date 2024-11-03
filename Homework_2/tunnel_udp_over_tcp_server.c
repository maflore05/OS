#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#define BUFFER_SIZE 216
#define UDP_BUFFER_SIZE (BUFFER_SIZE + 2)

void handle_error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// Helper function to convert the port name to a 16-bit unsigned integer
static int convert_port_name(uint16_t *port, const char *port_name) {
    char *end;
    long long int nn;
    uint16_t t;
    long long int tt;

    if (port_name == NULL || *port_name == '\0') return -1;
    nn = strtoll(port_name, &end, 0);
    if (*end != '\0') return -1;
    if (nn < 0) return -1;
    
    t = (uint16_t) nn;
    tt = (long long int) t;
    if (tt != nn) return -1;

    *port = t;
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <TCP port> <UDP server name> <UDP port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    uint16_t tcp_port;
    if (convert_port_name(&tcp_port, argv[1]) < 0) {
        fprintf(stderr, "Invalid TCP port: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    char *udp_server_name = argv[2];
    uint16_t udp_port;
    if (convert_port_name(&udp_port, argv[3]) < 0) {
        fprintf(stderr, "Invalid UDP port: %s\n", argv[3]);
        exit(EXIT_FAILURE);
    }

    // Create TCP socket
    int tcp_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sockfd < 0) {
        handle_error("Failed to create TCP socket");
    }

    struct sockaddr_in tcp_addr;
    memset(&tcp_addr, 0, sizeof(tcp_addr));
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_addr.sin_port = htons(tcp_port);

    // Bind TCP socket
    if (bind(tcp_sockfd, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) < 0) {
        handle_error("Failed to bind TCP socket");
    }

    // Listen on TCP socket
    if (listen(tcp_sockfd, 1) < 0) {
        handle_error("Failed to listen on TCP socket");
    }

    // Get UDP socket
    struct addrinfo hints, *udp_info, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(udp_server_name, argv[3], &hints, &udp_info) != 0) {
        handle_error("Failed to get UDP address info");
    }

    int udp_sockfd = -1;
    for (p = udp_info; p != NULL; p = p->ai_next) {
        udp_sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (udp_sockfd == -1) continue;

        if (connect(udp_sockfd, p->ai_addr, p->ai_addrlen) != -1) break; // Success
        close(udp_sockfd);
    }
    if (p == NULL) {
        handle_error("Failed to create UDP socket");
    }

    freeaddrinfo(udp_info); // Free the linked list

    // Accept a TCP connection
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int tcp_connfd = accept(tcp_sockfd, (struct sockaddr *)&client_addr, &client_len);
    if (tcp_connfd < 0) {
        handle_error("Failed to accept TCP connection");
    }

    printf("Accepted a TCP connection\n");

    // Prepare for select()
    fd_set read_fds;
    char tcp_buffer[BUFFER_SIZE];
    char udp_buffer[UDP_BUFFER_SIZE];

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(tcp_connfd, &read_fds);
        FD_SET(udp_sockfd, &read_fds);
        int max_fd = (tcp_connfd > udp_sockfd) ? tcp_connfd : udp_sockfd;

        // Wait for data on either TCP or UDP sockets
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            handle_error("select failed");
        }

        // Handle incoming data from TCP
        if (FD_ISSET(tcp_connfd, &read_fds)) {
            ssize_t bytes_received = read(tcp_connfd, tcp_buffer, BUFFER_SIZE);
            if (bytes_received < 0) {
                handle_error("TCP Read failed");
            }
            // Break if the connection is closed
            if (bytes_received == 0) {
                printf("TCP connection closed\n");
                break; 
            }
            
            if (send(udp_sockfd, tcp_buffer, bytes_received, 0) < 0) {
                handle_error("UDP Send failed");
            }
            printf("Forwarded %zd bytes from TCP to UDP\n", bytes_received + 2);
        }

        // Handle incoming data from UDP
        if (FD_ISSET(udp_sockfd, &read_fds)) {
            ssize_t bytes_received = recv(udp_sockfd, udp_buffer, UDP_BUFFER_SIZE, 0);
            if (bytes_received < 0) {
                handle_error("UDP Receive failed");
            }

            // Send the data over TCP
            if (send(tcp_connfd, udp_buffer, bytes_received, 0) < 0) {
                handle_error("TCP Send failed");
            }
            printf("Forwarded %zd bytes from UDP to TCP\n", bytes_received);
        }
    }

    close(tcp_connfd);
    close(tcp_sockfd);
    close(udp_sockfd);
    return 0;
}
