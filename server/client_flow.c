#include "client_flow.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>

#define BUFFER_SIZE 1024

static void reply_to_client(const int peer_fd) {

    FILE *file_to_read = fopen(SOCKET_DATA_FILE, "r");
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file_to_read)) > 0) {

        send(peer_fd, buffer, bytes_read, 0);
    }

    fclose(file_to_read);
}

void process_client(const int peer_fd) {

    FILE *file_to_append = fopen(SOCKET_DATA_FILE, "a+");
    if (file_to_append == NULL) {
        syslog(LOG_ERR, "fopen '%s': %s", SOCKET_DATA_FILE, strerror(errno));
        return;
    }

    ssize_t bytes_read;
    char buffer[BUFFER_SIZE + 1];
    while ((bytes_read = recv(peer_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {

        buffer[bytes_read] = '\0';
        const char *start = buffer;

        char *newline_pos;
        while ((newline_pos = strchr(start, '\n')) != NULL) {

            *newline_pos = '\0';
            fputs(start, file_to_append);
            fputc('\n', file_to_append);
            fflush(file_to_append);
            start = newline_pos + 1;

            reply_to_client(peer_fd);
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
