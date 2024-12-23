#include <assert.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <unistd.h>

volatile sig_atomic_t g_running = 1;

void signal_handler(const int sig) {
    switch (sig) {
        case SIGINT:
        case SIGTERM:
            g_running = 0;
            break;
        default:
            break;
    }
}

int open_aesd_socket() {
    // Build address data structure.
    //
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* Datagram socket */
    hints.ai_flags = AI_PASSIVE;     /* For wildcard IP address */
    hints.ai_protocol = 0;           /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
    //
    struct addrinfo *result = NULL;
    const int res = getaddrinfo(NULL, "9000", &hints, &result);
    if (res != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(res));
        exit(EXIT_FAILURE);
    }
    assert(result != NULL);

    // `getaddrinfo()` returns a list of address structures.
    // Try each address until we successfully `bind`.
    // If `socket` (or `bind`) fails, we close the socket and try the next address.
    //
    int sock_fd = -1;
    const struct addrinfo * rp;
    for (rp = result; rp != NULL; rp = rp->ai_next) {

        sock_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock_fd == -1)
            continue;

        if (bind(sock_fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break; // Success

        close(sock_fd);
        sock_fd = -1;
    }
    assert((sock_fd != -1) || (rp == NULL));

    freeaddrinfo(result); // No longer needed
    result = NULL;

    if (sock_fd == -1) { // No address succeeded
        fprintf(stderr, "Could not bind\n");
        exit(EXIT_FAILURE);
    }

    return sock_fd;
}

int main(const int argc, const char **const argv) {
    // int                      sfd, s;
    // char                     buf[BUF_SIZE];
    // ssize_t                  nread;
    // socklen_t                peer_addrlen;
    // struct addrinfo          *result, *rp;
    // struct sockaddr_storage  peer_addr;

    // Make sure we have socket open.
    //
    int sock_fd = open_aesd_socket();

    // Detect `-d` flag in the command line arguments.
    //
    const bool daemonize = ((argc == 2) && (strcmp(argv[1], "-d") == 0));
    if (daemonize) {
        const int res = fork();
        if (res < 0) {
            perror("fork");
            close(sock_fd);
            exit(EXIT_FAILURE);
        }
        if (res > 0) {
            // Parent process.
            close(sock_fd);
            exit(EXIT_SUCCESS);
        }
    }

    openlog(NULL, LOG_PID | LOG_NDELAY, daemonize ? LOG_DAEMON : LOG_USER);

    // Set up signal handlers.
    //
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // The main loop.
    //
    while (g_running == 1) {
        pause();
    }
    syslog(LOG_INFO, "Caught signal, exiting");

    close(sock_fd);

    return 0;
}
