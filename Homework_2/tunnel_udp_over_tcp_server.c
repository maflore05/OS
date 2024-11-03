#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#define BUFFER_SIZE 65536
#define UDP_BUFFER_SIZE (BUFFER_SIZE + 2)

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
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <TCP port> <UDP server name> <UDP port>\n", argv[0]);
        return -1;
    }

    uint16_t tcp_port;
    if (convert_port_name(&tcp_port, argv[1]) < 0) {
        fprintf(stderr, "Invalid TCP port: %s\n", argv[1]);
        return -1;
    }

    char *udp_server_name = argv[2];
    uint16_t udp_port;
    if (convert_port_name(&udp_port, argv[3]) < 0) {
        fprintf(stderr, "Invalid UDP port: %s\n", argv[3]);
        return -1;
    }

    // Create TCP socket
    int tcp_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sockfd < 0) {
        fprintf(stderr, "Could not open a UDP socket: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_in tcp_addr;
    memset(&tcp_addr, 0, sizeof(tcp_addr));
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_addr.sin_port = htons(tcp_port);

    // Bind TCP socket
    if (bind(tcp_sockfd, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) < 0) {
        fprintf(stderr, "Could not bind a socket: %s\n", strerror(errno));
        return -1;
    }

    // Listen on TCP socket
    if (listen(tcp_sockfd, 1) < 0) {
        fprintf(stderr, "Failed to listen on TCP socket: %s\n", strerror(errno));
        return -1;
    }

    // Get UDP socket
    struct addrinfo hints, *udp_info, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(udp_server_name, argv[3], &hints, &udp_info) != 0) {
        fprintf(stderr, "Could not look up server address info for %s %s\n", udp_server_name, argv[3]);
        return -1;
    }

    int udp_sockfd = -1;
    for (p = udp_info; p != NULL; p = p->ai_next) {
        udp_sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (udp_sockfd == -1) continue;

        if (connect(udp_sockfd, p->ai_addr, p->ai_addrlen) != -1) break; // Success
        close(udp_sockfd);
    }
    if (p == NULL) {
        fprintf(stderr, "Failed to create UDP socket: %s\n", strerror(errno));
        return -1;
    }

    freeaddrinfo(udp_info); // Free the linked list

    // Accept a TCP connection
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int tcp_connfd = accept(tcp_sockfd, (struct sockaddr *)&client_addr, &client_len);
    if (tcp_connfd < 0) {
        fprintf(stderr, "Failed to accept on TCP socket: %s\n", strerror(errno));
        return -1;
    }

    write(1, "Accepted a TCP connection\n", 27);

    // Prepare for select()
    fd_set read_fds;
    char tcp_buffer[BUFFER_SIZE];
    char udp_buffer[UDP_BUFFER_SIZE];

    int goterror = 0;
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(tcp_connfd, &read_fds);
        FD_SET(udp_sockfd, &read_fds);
        int max_fd = (tcp_connfd > udp_sockfd) ? tcp_connfd : udp_sockfd;

        // Wait for data on either TCP or UDP sockets
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            fprintf(stderr, "select failed: %s\n", strerror(errno));
            goterror = 1;
            break;
        }

        // Handle incoming data from TCP
        if (FD_ISSET(tcp_connfd, &read_fds)) {
            ssize_t bytes_received = read(tcp_connfd, tcp_buffer, BUFFER_SIZE);
            if (bytes_received < 0) {
                fprintf(stderr, "TCP Read failed: %s\n", strerror(errno));
                goterror = 1;
                break;
            }
            // if the connection is closed
            if (bytes_received == 0) {
                write(1, "TCP connection closed\n", 23);
                fprintf(stderr, "TCP connection failed: %s\n", strerror(errno));
                break; 
            }

            if (send(udp_sockfd, tcp_buffer, bytes_received, 0) < 0) {
                fprintf(stderr, "UDP send failed: %s\n", strerror(errno));
                goterror = 1;
                break;
            }
            write(1, "Forwarded bytes from TCP to UDP\n", 33);
            // Write the received data to standard input. 
            if (better_write(STDOUT_FILENO, tcp_buffer, bytes_received) < 0) {
                fprintf(stderr, "write Error: %s\n", strerror(errno));
                close(tcp_connfd);
                return -1;
            }
        }

        // Handle incoming data from UDP
        if (FD_ISSET(udp_sockfd, &read_fds)) {
            ssize_t bytes_received = recv(udp_sockfd, udp_buffer, UDP_BUFFER_SIZE, 0);
            if (bytes_received < 0) {
                fprintf(stderr, "UDP Receive failed: %s\n", strerror(errno));
                goterror = 1;
                break;
            }

            // Send the data over TCP
            if (send(tcp_connfd, udp_buffer, bytes_received, 0) < 0) {
                fprintf(stderr, "TCP send failed: %s\n", strerror(errno));
                goterror = 1;
                break;
            }
            write(1, "Forwarded bytes from UDP to TCP\n", 33);

            // Write the received data to standard input. 
            if (better_write(STDOUT_FILENO, udp_buffer, bytes_received) < 0) {
                fprintf(stderr, "write Error: %s\n", strerror(errno));
                close(udp_sockfd);
                return -1;
            }
        }
    }

    // Cleanup
    close(tcp_connfd);
    close(tcp_sockfd);
    close(udp_sockfd);
    if (goterror) {return -1;} // did not successed
    return 0;
}
