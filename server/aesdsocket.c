#include "client_flow.h"
#include "queue.h"

#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
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
    const struct addrinfo *rp;
    for (rp = result; rp != NULL; rp = rp->ai_next) {

        sock_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock_fd == -1)
            continue;

        // Set the SO_REUSEADDR option
        const int opt_val = 1;
        if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val)) == -1) {
            close(sock_fd);
            continue;
        }

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

void run_server_logic(const int server_sock_fd) {

    int res = listen(server_sock_fd, 1);
    if (res == -1) {
        syslog(LOG_ERR, "listen: %s", strerror(errno));
        return;
    }

    // The main loop.
    //
    while (g_running == 1) {

        struct sockaddr_storage peer_addr;
        socklen_t peer_addrlen = sizeof(peer_addr);
        int peer_fd = accept(server_sock_fd, (struct sockaddr *) &peer_addr, &peer_addrlen);
        if (peer_fd == -1) {
            syslog(LOG_WARNING, "accept: %s", strerror(errno));
            continue;
        }

        char host[NI_MAXHOST], service[NI_MAXSERV];
        res = getnameinfo((struct sockaddr *) &peer_addr, peer_addrlen, host, NI_MAXHOST, service, NI_MAXSERV,
                          NI_NUMERICSERV);
        if (res == 0) {
            syslog(LOG_INFO, "Accepted connection from %s:%s", host, service);
            process_client(peer_fd);
        } else {
            syslog(LOG_WARNING, "getnameinfo: %s", gai_strerror(res));
        }
        close(peer_fd);
    }

    if (g_running == 0) {
        syslog(LOG_INFO, "Caught signal, exiting");
    }
}

int main(const int argc, const char **const argv) {

    unlink(SOCKET_DATA_FILE);

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
    syslog(LOG_INFO, "Started");

    // Set up signal handlers.
    //
    struct sigaction sigbreak;
    memset(&sigbreak, 0, sizeof sigbreak);
    sigbreak.sa_handler = &signal_handler;
    sigaction(SIGINT, &sigbreak, NULL);
    sigaction(SIGTERM, &sigbreak, NULL);

    run_server_logic(sock_fd);

    close(sock_fd);
    unlink(SOCKET_DATA_FILE);

    syslog(LOG_INFO, "Completed!");
    return 0;
}
