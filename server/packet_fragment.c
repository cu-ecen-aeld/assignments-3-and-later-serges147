#include "packet_fragment.h"

#include <stdlib.h>

struct packet_fragment *packet_fragment_alloc()
{
    struct packet_fragment *fragment = malloc(sizeof(struct packet_fragment));
    if (fragment != NULL)
    {
        fragment->next = NULL;
        fragment->size = 0;
        fragment->data = fragment->buffer;
    }
    return fragment;
}

void packet_fragments_free(struct packet_fragment *fragment)
{
    while (fragment != NULL)
    {
        struct packet_fragment *next = fragment->next;
        free(fragment);
        fragment = next;
    }
}
