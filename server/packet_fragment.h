#ifndef AESDSOCKET_PACKET_FRAGMENT_H
#define AESDSOCKET_PACKET_FRAGMENT_H

#include <stddef.h>

#define BUFFER_SIZE 4

struct packet_fragment {

    struct packet_fragment *next;

    size_t size;  // The size of the data in the buffer.
    const char *data;  // The offset of the data in the buffer.
    char buffer[BUFFER_SIZE];
};

/// Free the memory allocated for the list of packet fragments.
///
void packet_fragments_free(struct packet_fragment *fragment);

/// Allocates a new packet fragment.
struct packet_fragment *packet_fragment_alloc();

#endif // AESDSOCKET_PACKET_FRAGMENT_H
