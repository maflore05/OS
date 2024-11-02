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

int create_tcp_socket(uint16_t port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) handle_error("Failed to create TCP socket");

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port),
    };

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        handle_error("Failed to bind TCP socket");
    }

    if (listen(sockfd, 1) < 0) {
        close(sockfd);
        handle_error("Failed to listen on TCP socket");
    }

    return sockfd;
}

int create_udp_socket(const char *server_name, const char *port_name) {
    struct addrinfo hints = {0}, *udp_info;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(server_name, port_name, &hints, &udp_info) != 0) {
        handle_error("Failed to get UDP address info");
    }

    int udp_sockfd = -1;
    for (struct addrinfo *p = udp_info; p != NULL; p = p->ai_next) {
        udp_sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (udp_sockfd == -1) continue;

        if (connect(udp_sockfd, p->ai_addr, p->ai_addrlen) != -1) break; // Success
        close(udp_sockfd);
    }
    freeaddrinfo(udp_info); // Free the linked list

    if (udp_sockfd == -1) {
        handle_error("Failed to create UDP socket");
    }

    return udp_sockfd;
}

void forward_tcp_to_udp(int tcp_connfd, int udp_sockfd) {
    char tcp_buffer[BUFFER_SIZE];
    ssize_t bytes_received = read(tcp_connfd, tcp_buffer, BUFFER_SIZE);
    
    if (bytes_received < 0) handle_error("TCP Read failed");
    if (bytes_received == 0) return; // Connection closed by client

    // Prepare and send the UDP packet
    uint16_t udp_length = (uint16_t)bytes_received;
    char udp_packet[UDP_BUFFER_SIZE] = {0};
    udp_packet[0] = (udp_length >> 8) & 0xFF; // High byte
    udp_packet[1] = udp_length & 0xFF;        // Low byte
    memcpy(udp_packet + 2, tcp_buffer, bytes_received);

    if (send(udp_sockfd, udp_packet, bytes_received + 2, 0) < 0) {
        handle_error("UDP Send failed");
    }
    printf("Forwarded %zd bytes from TCP to UDP\n", bytes_received + 2);
}

void forward_udp_to_tcp(int udp_sockfd, int tcp_connfd) {
    char udp_buffer[UDP_BUFFER_SIZE];
    ssize_t bytes_received = recv(udp_sockfd, udp_buffer, UDP_BUFFER_SIZE, 0);

    if (bytes_received < 0) handle_error("UDP Receive failed");

    if (send(tcp_connfd, udp_buffer, bytes_received, 0) < 0) {
        handle_error("TCP Send failed");
    }
    printf("Forwarded %zd bytes from UDP to TCP\n", bytes_received);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <TCP port> <UDP server name> <UDP port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    uint16_t tcp_port;
    if (sscanf(argv[1], "%hu", &tcp_port) != 1) {
        fprintf(stderr, "Invalid TCP port: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    char *udp_server_name = argv[2];
    uint16_t udp_port;
    if (sscanf(argv[3], "%hu", &udp_port) != 1) {
        fprintf(stderr, "Invalid UDP port: %s\n", argv[3]);
        exit(EXIT_FAILURE);
    }

    int tcp_sockfd = create_tcp_socket(tcp_port);
    int udp_sockfd = create_udp_socket(udp_server_name, argv[3]);

    // Accept a TCP connection
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int tcp_connfd = accept(tcp_sockfd, (struct sockaddr *)&client_addr, &client_len);
    if (tcp_connfd < 0) handle_error("Failed to accept TCP connection");

    printf("Accepted a TCP connection\n");

    // Forwarding loop
    fd_set read_fds;
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(tcp_connfd, &read_fds);
        FD_SET(udp_sockfd, &read_fds);
        int max_fd = (tcp_connfd > udp_sockfd) ? tcp_connfd : udp_sockfd;

        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            handle_error("select failed");
        }

        if (FD_ISSET(tcp_connfd, &read_fds)) {
            forward_tcp_to_udp(tcp_connfd, udp_sockfd);
        }

        if (FD_ISSET(udp_sockfd, &read_fds)) {
            forward_udp_to_tcp(udp_sockfd, tcp_connfd);
        }
    }

    // Cleanup
    close(tcp_connfd);
    close(tcp_sockfd);
    close(udp_sockfd);
    return 0;
}
