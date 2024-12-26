#ifndef AESDSOCKET_CLIENT_FLOW_H
#define AESDSOCKET_CLIENT_FLOW_H

#include "packet_fragment.h"
#include "queue.h"

#include <pthread.h>
#include <stdbool.h>

#define SOCKET_DATA_FILE "/var/tmp/aesdsocketdata"

struct shared_info {
    pthread_rwlock_t rw_file_lock;
};

struct client_info {

    int peer_fd;
    pthread_t thread;
    struct shared_info *shared;
    struct packet_fragment *fragments;

    TAILQ_ENTRY(client_info) nodes;
};

void *process_client_thread(void *);

#endif // AESDSOCKET_CLIENT_FLOW_H
