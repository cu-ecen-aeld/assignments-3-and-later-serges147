#include "client_flow.h"

#include "assert.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

/// Atomically read the file and send it to the client.
///
/// Note that the file is locked for reading,
/// so not preventing other threads from reading it as well.
///
static void reply_to_client(struct client_info *const client) {

    assert(client != NULL);

    pthread_rwlock_rdlock(&client->shared->rw_file_lock);

    FILE *file_to_read = fopen(SOCKET_DATA_FILE, "r");
    if (file_to_read != NULL) {

        char buffer[BUFFER_SIZE];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file_to_read)) > 0) {

            send(client->peer_fd, buffer, bytes_read, 0);
        }

        fclose(file_to_read);
    }

    pthread_rwlock_unlock(&client->shared->rw_file_lock);
}

static void process_client(struct client_info *const client) {

    assert(client != NULL);
    assert(client->peer_fd >= 0);
    assert(client->shared != NULL);

    FILE *file_to_append = fopen(SOCKET_DATA_FILE, "a+");
    if (file_to_append == NULL) {
        syslog(LOG_ERR, "fopen '%s': %s", SOCKET_DATA_FILE, strerror(errno));
        return;
    }

    ssize_t bytes_read;
    char buffer[BUFFER_SIZE + 1];
    while ((bytes_read = recv(client->peer_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {

        buffer[bytes_read] = '\0';
        const char *start = buffer;

        char *newline_pos;
        while ((newline_pos = strchr(start, '\n')) != NULL) {

            *newline_pos = '\0';
            fputs(start, file_to_append);
            fputc('\n', file_to_append);
            fflush(file_to_append);
            start = newline_pos + 1;

            reply_to_client(client);
        }
        if (*start != '\0') {
            fputs(start, file_to_append);
        }
    }

    if (bytes_read == -1) {
        syslog(LOG_ERR, "recv: %s", strerror(errno));
    }

    fflush(file_to_append);
    fclose(file_to_append);
}

void *process_client_thread(void *const arg) {

    struct client_info *const client = arg;
    if (client == NULL) {
        syslog(LOG_ERR, "process_client_thread: NULL argument");
        return NULL;
    }
    syslog(LOG_DEBUG, "Started client thread (peer_fd=%d, thread=%p).",  //
           client->peer_fd, (const void *) pthread_self());

    process_client(client);

    syslog(LOG_DEBUG, "Finished client thread (peer_fd=%d, thread=%p).",  //
           client->peer_fd, (const void *) pthread_self());

    close(client->peer_fd);
    client->peer_fd = -1;  // this marks the client as done

    return client;
}
