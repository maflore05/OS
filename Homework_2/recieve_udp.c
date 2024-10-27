#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUFFER_SIZE 216

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

// A wrapper around the write system call to ensure all bytes are written
static ssize_t better_write(int fd, const void *buf, size_t count) {
    ssize_t written, total_written = 0;
    
    while (count > 0) {
        written = write(fd, buf + total_written, count);
        if (written < 0) {
            return -1;
        }
        total_written += written;
        count -= written;
    }
    return total_written;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int sockfd;
    struct sockaddr_in servaddr;
    uint16_t port;
    char buffer[BUFFER_SIZE];
    ssize_t recv_bytes;

    // Convert port name to 16-bit unsigned integer
    if (convert_port_name(&port, argv[1]) != 0) {
        fprintf(stderr, "Invalid port number: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    // Zero out the server address structure and set the port and IP family
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;  // Listen on any local interface
    servaddr.sin_port = htons(port);  // Convert port to network byte order

    // Bind the socket to the specified port
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind");
        close(sockfd);
        return EXIT_FAILURE;
    }

    // Continuously receive packets until a 0-byte UDP packet is received
    while (1) {
        recv_bytes = recv(sockfd, buffer, BUFFER_SIZE, 0);
        if (recv_bytes < 0) {
            perror("recv");
            close(sockfd);
            return EXIT_FAILURE;
        }

        // If a 0-byte packet is received, terminate the program
        if (recv_bytes == 0) {
            printf("Received 0-byte packet. Terminating.\n");
            break;
        }

        // Write the received data to stdout
        if (better_write(STDOUT_FILENO, buffer, recv_bytes) < 0) {
            perror("write");
            close(sockfd);
            return EXIT_FAILURE;
        }
    }

    // Close the socket before exiting
    close(sockfd);
    return EXIT_SUCCESS;
}
