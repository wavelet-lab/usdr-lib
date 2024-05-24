#include "ring_circbuf.h"
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <assert.h>

// Can be MT-safe if rpos and wpos updated atomic

ring_circbuf_t* ring_circbuf_create(size_t max_size)
{
    size_t sz = sizeof(ring_circbuf_t) + max_size;
    ring_circbuf_t* b;
    int res = posix_memalign((void**)&b, 16, sz);
    if (res) {
        return NULL;
    }

    b->size = max_size;
    b->wpos = 0;
    b->rpos = 0;
    return b;
}

void ring_circbuf_destroy(ring_circbuf_t* rb)
{
    free(rb);
}


void ring_circbuf_read(ring_circbuf_t* rb, void* ptr, size_t sz)
{
    char* cptr = (char*)ptr;
    size_t pos = (rb->rpos % rb->size);
    size_t p1 = rb->size - pos;
    size_t rsz = (sz > p1) ? p1 : sz;

    memcpy(cptr, rb->data + pos, rsz);
    rb->rpos += rsz;

    assert(rb->rpos <= rb->wpos);
    if (rsz == sz)
        return;

    cptr += rsz;
    rsz = sz - rsz;

    memcpy(cptr, rb->data, rsz);
    rb->rpos += rsz;
    assert(rb->rpos <= rb->wpos);
}

void ring_circbuf_write(ring_circbuf_t* rb, const void* ptr, size_t sz)
{
    const char* cptr = (const char*)ptr;
    size_t pos = (rb->wpos % rb->size);
    size_t p1 = rb->size - pos;
    size_t rsz = (sz > p1) ? p1 : sz;

    memcpy(rb->data + pos, cptr, rsz);
    rb->wpos += rsz;
    if (rsz == sz)
        return;

    cptr += rsz;
    rsz = sz - rsz;

    memcpy(rb->data, cptr, rsz);
    rb->wpos += rsz;
}
