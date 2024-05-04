// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <semaphore.h>

#define CACHE_SIZE  64

// Fixed size circular array
struct ring_buffer
{
    unsigned items; // power of 2
    unsigned isize; // item size in bytes

    unsigned pidx;
    unsigned cidx;

    char reserved[CACHE_SIZE - 4*sizeof(unsigned)];

    sem_t producer;
    sem_t consumer;

    char data[0];
};
typedef struct ring_buffer ring_buffer_t;

enum {
    IDX_TIMEDOUT = ~0u
};

ring_buffer_t* ring_buffer_create(unsigned items, unsigned isize);
void ring_buffer_destroy(ring_buffer_t* rb);
char* ring_buffer_at(ring_buffer_t* rb, unsigned idx);

// Producer
unsigned ring_buffer_pwait(ring_buffer_t* rb, int usecs);
void ring_buffer_ppost(ring_buffer_t* rb);

// Consumer
unsigned ring_buffer_cwait(ring_buffer_t* rb, int usecs);
void ring_buffer_cpost(ring_buffer_t* rb);

#endif
