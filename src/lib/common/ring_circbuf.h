// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef RING_CIRCBUF_H
#define RING_CIRCBUF_H

#include <stdint.h>
#include <stddef.h>

// Simple circbuffer
struct ring_circbuf {
    uint64_t size;
    uint64_t wpos;
    uint64_t rpos;
    uint64_t reseved;

    char data[0];
};
typedef struct ring_circbuf ring_circbuf_t;

ring_circbuf_t* ring_circbuf_create(size_t max_size);
void ring_circbuf_destroy(ring_circbuf_t* rb);

static inline uint64_t ring_circbuf_rspace(ring_circbuf_t* rb) {
    return rb->wpos - rb->rpos;
}

static inline uint64_t ring_circbuf_wspace(ring_circbuf_t* rb) {
    return rb->size - (rb->wpos - rb->rpos);
}

static inline void* ring_circbuf_wptr(ring_circbuf_t* rb) {
    return rb->data + (rb->wpos % rb->size);
}

// These function doesn't check space availablity
void ring_circbuf_read(ring_circbuf_t* rb, void* ptr, size_t sz);
void ring_circbuf_write(ring_circbuf_t* rb, const void* ptr, size_t sz);

#endif
