#ifndef AESDSOCKET_CLIENT_FLOW_H
#define AESDSOCKET_CLIENT_FLOW_H

#include "queue.h"

#include <stdbool.h>

#define SOCKET_DATA_FILE "/var/tmp/aesdsocketdata"

struct main_thread_data {
};

struct client_thread_data {
    int peer_fd;
    bool is_job_done;
    SLIST_ENTRY(client_thread_data) entries;
};

void process_client(const int peer_fd);

#endif // AESDSOCKET_CLIENT_FLOW_H
