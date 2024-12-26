#include "client_flow.h"
#include "packet_fragment.h"

#include "assert.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <unistd.h>

/// Atomically read the file and send it to the client.
///
/// Note that the file is locked for reading,
/// so not preventing other threads from reading it as well.
///
static void reply_to_client(struct client_info *const client) {

    assert(client != NULL);

    pthread_rwlock_rdlock(&client->shared->rw_file_lock);
    {
        FILE *file_to_read = fopen(SOCKET_DATA_FILE, "r");
        if (file_to_read != NULL) {

            char buffer[BUFFER_SIZE];
            size_t bytes_read;
            while ((bytes_read = fread(buffer, 1, sizeof(buffer), file_to_read)) > 0) {

                send(client->peer_fd, buffer, bytes_read, 0);
            }

            fclose(file_to_read);
        }
    }
    pthread_rwlock_unlock(&client->shared->rw_file_lock);
}

/// Atomically writes list of packet fragments to the file.
///
/// The list is freed as the fragments are written to the file -
/// except the last fragment b/c it could be still reused.
///
static void write_new_packet(struct client_info *const client) {

    assert(client != NULL);

    pthread_rwlock_wrlock(&client->shared->rw_file_lock);
    {
        FILE *file_to_append = fopen(SOCKET_DATA_FILE, "a+");
        if (file_to_append != NULL) {

            struct packet_fragment *curr_fragment, *next_fragment;
            for (curr_fragment = client->fragments; curr_fragment != NULL; curr_fragment = next_fragment) {

                fwrite(curr_fragment->data, 1, curr_fragment->size, file_to_append);
                next_fragment = curr_fragment->next;
                if (next_fragment != NULL) {

                    free(curr_fragment);
                    client->fragments = next_fragment;
                }
            }

            fflush(file_to_append);
            fclose(file_to_append);

        } else {
            syslog(LOG_ERR, "fopen '%s': %s", SOCKET_DATA_FILE, strerror(errno));
        }
    }
    pthread_rwlock_unlock(&client->shared->rw_file_lock);

    reply_to_client(client);
}

static void process_client(struct client_info *const client) {

    assert(client != NULL);
    assert(client->peer_fd >= 0);
    assert(client->shared != NULL);
    assert(client->fragments == NULL);

    client->fragments = packet_fragment_alloc();
    struct packet_fragment *curr_fragment = client->fragments;

    ssize_t bytes_read;
    while ((bytes_read = recv(client->peer_fd, curr_fragment->buffer, sizeof(curr_fragment->buffer), 0)) > 0) {

        curr_fragment->data = curr_fragment->buffer;
        ssize_t bytes_left = bytes_read;

        const char *newline_pos;
        while (NULL != (newline_pos = memchr(curr_fragment->data, '\n', bytes_left))) {

            curr_fragment->size = newline_pos - curr_fragment->data + 1;  // including \n

            write_new_packet(client);
            assert(client->fragments == curr_fragment);

            bytes_left -= curr_fragment->size;
            curr_fragment->data += curr_fragment->size;
        }

        if (bytes_left > 0) {

            curr_fragment->size = bytes_left;
            curr_fragment->next = packet_fragment_alloc();
            curr_fragment = curr_fragment->next;
        }
    }

    // All done so we can free the fragments (instead of postponing it to the thread join).
    packet_fragments_free(client->fragments);
    client->fragments = NULL;

    if (bytes_read == -1) {
        syslog(LOG_ERR, "recv: %s", strerror(errno));
    }
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
