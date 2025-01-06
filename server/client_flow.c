#include "aesd_ioctl.h"
#include "client_flow.h"
#include "packet_fragment.h"

#include "assert.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

/// Atomically read the file and send it to the client.
///
/// Note that the file is locked for reading,
/// so not preventing other threads from reading it as well.
///
static void reply_to_client(struct client_info *const client)
{
    assert(client != NULL);

    pthread_rwlock_rdlock(&client->shared->rw_file_lock);
    {
        FILE *file_to_read = fopen(SOCKET_DATA_FILE, "r");
        if (file_to_read != NULL)
        {
            char buffer[BUFFER_SIZE];
            size_t bytes_read;
            while ((bytes_read = fread(buffer, 1, sizeof(buffer), file_to_read)) > 0)
            {
                send(client->peer_fd, buffer, bytes_read, 0);
            }

            fclose(file_to_read);
        }
    }
    pthread_rwlock_unlock(&client->shared->rw_file_lock);
}

static char *flatten_fragments(struct client_info *const client, size_t *const size)
{
    assert(client != NULL);
    assert(size != NULL);

    size_t total_size = 0;
    struct packet_fragment *curr_fragment;
    for (curr_fragment = client->fragments; curr_fragment != NULL; curr_fragment = curr_fragment->next)
    {
        total_size += curr_fragment->size;
    }

    char *const buffer = malloc(total_size);
    if (buffer != NULL)
    {
        char *curr_buffer = buffer;
        struct packet_fragment *next_fragment;
        for (curr_fragment = client->fragments; curr_fragment != NULL; curr_fragment = next_fragment)
        {
            memcpy(curr_buffer, curr_fragment->data, curr_fragment->size);
            curr_buffer += curr_fragment->size;

            next_fragment = curr_fragment->next;
            if (next_fragment != NULL)
            {
                free(curr_fragment);
                client->fragments = next_fragment;
            }
        }

        *size = total_size;
    }

    return buffer;
}

/// Atomically writes list of packet fragments to the file.
///
/// The list is freed as the fragments are written to the file -
/// except the last fragment b/c it could be still reused.
///
static void write_new_packet(struct client_info *const client)
{
    assert(client != NULL);

    size_t flat_size = 0;
    char *const flat_data = flatten_fragments(client, &flat_size);
    if (flat_data != NULL)
    {
        assert(flat_size > 0);
        assert(flat_data[flat_size - 1] == '\n');
        struct aesd_seekto seekto = {0, 0};
        flat_data[flat_size - 1] = '\0'; // null-terminate the string
        const int params = sscanf(flat_data, "AESDCHAR_IOCSEEKTO:%u,%u", &seekto.write_cmd, &seekto.write_cmd_offset);
        flat_data[flat_size - 1] = '\n'; // restore the newline

        pthread_rwlock_wrlock(&client->shared->rw_file_lock);
        {
            FILE *file_to_append = fopen(SOCKET_DATA_FILE, "a+");
            if (file_to_append != NULL)
            {
                if (params == 2)
                {
                    ioctl(fileno(file_to_append), AESDCHAR_IOCSEEKTO, &seekto);
                }
                else
                {
                    fwrite(flat_data, 1, flat_size, file_to_append);
                    fflush(file_to_append);
                }
                fclose(file_to_append);
            }
            else
            {
                syslog(LOG_ERR, "fopen '%s': %s", SOCKET_DATA_FILE, strerror(errno));
            }
        }
        pthread_rwlock_unlock(&client->shared->rw_file_lock);

        free(flat_data);
    }

    reply_to_client(client);
}

static void process_client(struct client_info *const client)
{
    assert(client != NULL);
    assert(client->peer_fd >= 0);
    assert(client->shared != NULL);
    assert(client->fragments == NULL);

    client->fragments = packet_fragment_alloc();
    struct packet_fragment *curr_fragment = client->fragments;

    ssize_t bytes_read;
    while ((bytes_read = recv(client->peer_fd, curr_fragment->buffer, sizeof(curr_fragment->buffer), 0)) > 0)
    {
        curr_fragment->data = curr_fragment->buffer;
        ssize_t bytes_left = bytes_read;

        const char *newline_pos;
        while (NULL != (newline_pos = memchr(curr_fragment->data, '\n', bytes_left)))
        {
            curr_fragment->size = newline_pos - curr_fragment->data + 1; // including \n

            write_new_packet(client);
            assert(client->fragments == curr_fragment);

            bytes_left -= curr_fragment->size;
            curr_fragment->data += curr_fragment->size;
        }

        if (bytes_left > 0)
        {
            curr_fragment->size = bytes_left;
            curr_fragment->next = packet_fragment_alloc();
            curr_fragment = curr_fragment->next;
        }
    }

    // All done so we can free the fragments (instead of postponing it to the thread join).
    packet_fragments_free(client->fragments);
    client->fragments = NULL;

    if (bytes_read == -1)
    {
        syslog(LOG_ERR, "recv: %s", strerror(errno));
    }
}

void *process_client_thread(void *const arg)
{

    struct client_info *const client = arg;
    if (client == NULL)
    {
        syslog(LOG_ERR, "process_client_thread: NULL argument");
        return NULL;
    }
    syslog(LOG_DEBUG, "Started client thread (peer_fd=%d, thread=%p).", //
           client->peer_fd, (const void *)pthread_self());

    process_client(client);

    syslog(LOG_DEBUG, "Finished client thread (peer_fd=%d, thread=%p).", //
           client->peer_fd, (const void *)pthread_self());

    close(client->peer_fd);
    client->peer_fd = -1; // this marks the client as done

    return client;
}
