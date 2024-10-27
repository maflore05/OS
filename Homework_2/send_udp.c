#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#define BUFFER_SIZE 480

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <server_name> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *server_name = argv[1];
    const char *port_name = argv[2];
    struct addrinfo hints, *result, *rp;
    int gai_code, sockfd;
    ssize_t bytes_read, bytes_sent;
    char buffer[BUFFER_SIZE];

    // Set up hints for getaddrinfo
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;         // IPv4
    hints.ai_socktype = SOCK_DGRAM;    // UDP
    hints.ai_protocol = 0;             // Any protocol
    hints.ai_flags = 0;                // No flags

    // Get the address info
    gai_code = getaddrinfo(server_name, port_name, &hints, &result);
    if (gai_code != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai_code));
        return EXIT_FAILURE;
    }

    // Iterate over the results and create the socket
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) continue; // Try the next address if socket creation fails

        // If we successfully created the socket, break
        break;
    }

    // If socket creation failed for all address info results
    if (rp == NULL) {
        fprintf(stderr, "Could not create socket: %s\n", strerror(errno));
        freeaddrinfo(result);
        return EXIT_FAILURE;
    }

    // Read from standard input and send to the server
    while ((bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0) {
        // Send the bytes read in a UDP packet
        bytes_sent = sendto(sockfd, buffer, bytes_read, 0, rp->ai_addr, rp->ai_addrlen);
        if (bytes_sent == -1) {
            fprintf(stderr, "sendto: %s\n", strerror(errno));
            close(sockfd);
            freeaddrinfo(result);
            return EXIT_FAILURE;
        }
    }

    // If there was an error reading from standard input
    if (bytes_read == -1) {
        fprintf(stderr, "read: %s\n", strerror(errno));
        close(sockfd);
        freeaddrinfo(result);
        return EXIT_FAILURE;
    }

    // Send the 0-byte packet to signal the end of transmission
    bytes_sent = sendto(sockfd, "", 0, 0, rp->ai_addr, rp->ai_addrlen);
    if (bytes_sent == -1) {
        fprintf(stderr, "sendto (end of transmission): %s\n", strerror(errno));
    }

    // Clean up: close the socket and free the address info
    close(sockfd);
    freeaddrinfo(result);

    return EXIT_SUCCESS;
}
