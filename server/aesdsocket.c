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

static volatile sig_atomic_t g_running = 1;

static void signal_handler(const int sig) {

    switch (sig) {
        case SIGINT:
        case SIGTERM:
            g_running = 0;
            break;
        default:
            break;
    }
}

static int open_aesd_socket() {

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

TAILQ_HEAD(clients_s, client_info);

static void start_client_processing(struct clients_s *clients, struct shared_info *shared, const int peer_fd) {

    assert(clients != NULL);
    assert(shared != NULL);
    assert(peer_fd >= 0);

    struct client_info *client = malloc(sizeof(struct client_info));
    if (client == NULL) {
        syslog(LOG_ERR, "malloc `client_info`: %s", strerror(errno));
        close(peer_fd);
        return;
    }
    memset(client, 0, sizeof(struct client_info));

    client->shared = shared;
    client->peer_fd = peer_fd;
    client->fragments = NULL;

    if (0 != pthread_create(&client->thread, NULL, process_client_thread, client)) {
        syslog(LOG_ERR, "pthread_create: %s", strerror(errno));
        free(client);
        close(peer_fd);
    }

    TAILQ_INSERT_TAIL(clients, client, nodes);
}

void join_completed_clients(struct clients_s *clients, const bool cancel) {

    assert(clients != NULL);

    struct client_info *client = NULL;
    struct client_info *next = NULL;
    TAILQ_FOREACH_SAFE(client, clients, nodes, next) {

        if (cancel) {
            pthread_cancel(client->thread);
        }

        if (cancel || (client->peer_fd < 0)) {

            syslog(LOG_DEBUG, "Joining client thread (thread=%p)...", (const void *) client->thread);
            pthread_join(client->thread, NULL);
            syslog(LOG_DEBUG, "Joined the client thread (thread=%p).", (const void *) client->thread);

            // Deallocate any leftovers (if any) from the client thread.
            //
            if (client->peer_fd >= 0) {
                close(client->peer_fd);
            }
            packet_fragments_free(client->fragments);

            TAILQ_REMOVE(clients, client, nodes);
            free(client);
        }
    }
}

static void timer_thread(union sigval sigval) {

    struct shared_info *const shared = sigval.sival_ptr;
    if (shared == NULL) {
        syslog(LOG_ERR, "timer_thread: NULL argument");
        return;
    }

    const time_t now = time(NULL);
    const struct tm *const tm_info = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%a, %d %b %Y %T %z", tm_info);

    pthread_rwlock_wrlock(&shared->rw_file_lock);
    {
        FILE *const file_to_append = fopen(SOCKET_DATA_FILE, "a+");
        if (file_to_append != NULL) {

            fprintf(file_to_append, "timestamp:%s\n", time_str);
            fflush(file_to_append);
            fclose(file_to_append);

        } else {
            syslog(LOG_ERR, "timer_thread: fopen '%s': %s", SOCKET_DATA_FILE, strerror(errno));
        }
    }
    pthread_rwlock_unlock(&shared->rw_file_lock);
}

static timer_t setup_timer(struct shared_info *const shared) {

    timer_t timer_id = NULL;

    struct sigevent sev;
    memset(&sev, 0, sizeof(struct sigevent));
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_value.sival_ptr = shared;
    sev.sigev_notify_function = timer_thread;
    if (timer_create(CLOCK_MONOTONIC, &sev, &timer_id) == 0) {

        struct itimerspec timer_spec;
        memset(&timer_spec, 0, sizeof(struct itimerspec));
        timer_spec.it_value.tv_sec = 10;
        timer_spec.it_interval.tv_sec = 10;
        if (timer_settime(timer_id, 0, &timer_spec, NULL) != 0) {
            perror("timer_settime");
        }
    } else {
        perror("timer_create");
    }

    return timer_id;
}

static void run_server_logic(const int server_sock_fd) {

    assert(server_sock_fd >= 0);

    int res = listen(server_sock_fd, 1);
    if (res == -1) {
        syslog(LOG_ERR, "listen: %s", strerror(errno));
        return;
    }

    struct shared_info shared;
    if (pthread_rwlock_init(&shared.rw_file_lock, NULL) != 0) {
        perror("pthread_rwlock_init");
        exit(EXIT_FAILURE);
    }

    const timer_t timer_id = setup_timer(&shared);

    struct clients_s clients;
    TAILQ_INIT(&clients);

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
            syslog(LOG_INFO, "Accepted connection from %s:%s (peer_fd=%d)", host, service, peer_fd);
        } else {
            syslog(LOG_WARNING, "getnameinfo: %s", gai_strerror(res));
        }
        start_client_processing(&clients, &shared, peer_fd);

        join_completed_clients(&clients, false);
    }

    if (g_running == 0) {
        syslog(LOG_INFO, "Caught signal, exiting");
    }
    timer_delete(timer_id);

    join_completed_clients(&clients, true);
    assert(TAILQ_EMPTY(&clients));

    pthread_rwlock_destroy(&shared.rw_file_lock);
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
