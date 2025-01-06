#ifndef AESDSOCKET_CLIENT_FLOW_H
#define AESDSOCKET_CLIENT_FLOW_H

#include "packet_fragment.h"
#include "queue.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>

#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1
#endif

#if USE_AESD_CHAR_DEVICE
#define SOCKET_DATA_FILE "/dev/aesdchar"
#else
#define SOCKET_DATA_FILE "/var/tmp/aesdsocketdata"
#endif

struct shared_info
{
    pthread_rwlock_t rw_file_lock;
};

struct client_info
{
    int peer_fd;
    pthread_t thread;
    struct shared_info *shared;
    struct packet_fragment *fragments;
    FILE *file;

    TAILQ_ENTRY(client_info) nodes;
};

void *process_client_thread(void *);

#endif // AESDSOCKET_CLIENT_FLOW_H
