#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>

#define BUFFER_SIZE 65536

// Function to convert the port name to a 16-bit unsigned integer
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
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return -1;
    }

    int sockfd;
    struct sockaddr_in servaddr, clientaddr;
    uint16_t port;
    char buffer[BUFFER_SIZE];
    ssize_t recv_bytes;
    socklen_t addrlen = sizeof(clientaddr);

    // Convert port name to 16-bit unsigned integer
    if (convert_port_name(&port, argv[1]) != 0) {
        fprintf(stderr, "Invalid port number: %s\n", argv[1]);
        return -1;
    }

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "Could not open a UDP socket: %s\n", strerror(errno));
        return -1;
    }

    /* Bind the socket to an address and a port */
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);

    // Bind the socket to the port
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        fprintf(stderr, "Could not bind a socket: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }
    
    write(1, "reply_udp is running on port: ", 31);
    write(1, argv[1], 4);
    write(1, "\n", 2);

    // Main loop to receive and reply to incoming packets
    while (1) {
        // Receive a packet from the client
        recv_bytes = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&clientaddr, &addrlen);
        if (recv_bytes < 0) {
            fprintf(stderr, "recvfrom Error: %s\n", strerror(errno));
            close(sockfd);
            return -1;
        }

        // Send the received packet back to the client
        if (sendto(sockfd, buffer, recv_bytes, 0, (struct sockaddr *)&clientaddr, addrlen) != recv_bytes) {
            fprintf(stderr, "sendto: %s\n", strerror(errno));
            close(sockfd);
            return -1;
        }
    }

    // Close the socket
    close(sockfd);
    return 0;
}
