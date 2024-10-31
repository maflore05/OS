#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <sys/select.h>

#define BUF_SIZE 480        // Buffer size for sending data
#define RECV_BUF_SIZE 65536 // Buffer size for receiving data

// Error handling function
void handle_error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// Set up UDP socket and return its file descriptor
int setup_udp_socket(const char *server_name, const char *port_name, struct addrinfo **res) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       // Use IPv4
    hints.ai_socktype = SOCK_DGRAM;  // Use UDP

    // Get address info
    if (getaddrinfo(server_name, port_name, &hints, res) != 0) {
        handle_error("Failed to get address info");
    }

    // Create socket
    int sockfd = socket((*res)->ai_family, (*res)->ai_socktype, (*res)->ai_protocol);
    if (sockfd == -1) {
        handle_error("Error opening socket");
    }

    return sockfd;
}

// Read from standard input and send data over UDP
int send_data(int sockfd, struct addrinfo *res) {
    char send_buf[BUF_SIZE];
    ssize_t bytes_read = read(STDIN_FILENO, send_buf, BUF_SIZE);
    
    if (bytes_read < 0) {
        handle_error("Error reading from standard input");
    }
    if (bytes_read == 0) {
        return 0; // EOF reached
    }

    ssize_t bytes_sent = sendto(sockfd, send_buf, bytes_read, 0, res->ai_addr, res->ai_addrlen);
    if (bytes_sent == -1) {
        handle_error("Error sending UDP packet");
    }
    return 1; // Data sent successfully
}

// Receive data from UDP and write to standard output
int receive_data(int sockfd) {
    char recv_buf[RECV_BUF_SIZE];
    ssize_t bytes_received = recvfrom(sockfd, recv_buf, RECV_BUF_SIZE, 0, NULL, NULL);
    
    if (bytes_received < 0) {
        handle_error("Error receiving UDP packet");
    }
    if (bytes_received == 0) {
        return 0; // Empty packet received
    }

    if (write(STDOUT_FILENO, recv_buf, bytes_received) < 0) {
        handle_error("Error writing to standard output");
    }
    return 1; // Data received successfully
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <server> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct addrinfo *res;
    int sockfd = setup_udp_socket(argv[1], argv[2], &res);

    fd_set read_fds;
    int keep_running = 1;

    while (keep_running) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sockfd, &read_fds);
        int max_fd = sockfd;

        // Wait for input on either stdin or UDP socket
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) {
            handle_error("Error with select");
        }

        // Handle data on standard input
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            keep_running = send_data(sockfd, res);
        }

        // Handle data on the UDP socket
        if (FD_ISSET(sockfd, &read_fds)) {
            keep_running = receive_data(sockfd);
        }
    }

    // Send an empty UDP packet to signal end-of-file
    sendto(sockfd, "", 0, 0, res->ai_addr, res->ai_addrlen);

    // Cleanup
    freeaddrinfo(res);
    close(sockfd);

    return EXIT_SUCCESS;
}
