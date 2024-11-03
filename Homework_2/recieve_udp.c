#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>

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

ssize_t better_write(int fd, const char *buf, size_t count) {
  size_t already_written, to_be_written, written_this_time, max_count;
  ssize_t res_write;

  if (count == ((size_t) 0)) return (ssize_t) count;
  
  already_written = (size_t) 0;
  to_be_written = count;
  while (to_be_written > ((size_t) 0)) {
    max_count = to_be_written;
    if (max_count > ((size_t) 8192)) {
      max_count = (size_t) 8192;
    }
    res_write = write(fd, &(((const char *) buf)[already_written]), max_count);
    if (res_write < ((size_t) 0)) {
      /* Error */
      return res_write;
    }
    if (res_write == ((ssize_t) 0)) {
      /* Nothing written, stop trying */
      return (ssize_t) already_written;
    }
    written_this_time = (size_t) res_write;
    already_written += written_this_time;
    to_be_written -= written_this_time;
  }
  return (ssize_t) already_written;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return -1;
    }

    int sockfd;
    struct sockaddr_in servaddr;
    uint16_t port;
    char buffer[BUFFER_SIZE];
    ssize_t recv_bytes;

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

    // Continuously receive packets until a 0-byte UDP packet is received
    while (1) {
        recv_bytes = recv(sockfd, buffer, BUFFER_SIZE, 0);
        if (recv_bytes < 0) {
            fprintf(stderr, "recv Error: %s\n", strerror(errno));
            close(sockfd);
            return -1;
        }

        // If a 0-byte packet is received, terminate the program
        if (recv_bytes == 0) {
            better_write(1, "Received 0-byte packet. Terminating.\n", 38);
            break;
        }

        // Write the received data to standard input. 
        if (better_write(STDOUT_FILENO, buffer, recv_bytes) < 0) {
            fprintf(stderr, "write Error: %s\n", strerror(errno));
            close(sockfd);
            return -1;
        }
    }

    // Close the socket
    close(sockfd);
    return 0;
}
