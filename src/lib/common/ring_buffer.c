// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "ring_buffer.h"
#include <memory.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <time.h>

struct ring_buffer* ring_buffer_create(unsigned items, unsigned isize)
{
    size_t sz = items * isize + sizeof(struct ring_buffer);
    struct ring_buffer* obj;
    if (items == 0 || isize == 0)
        return NULL;

    int res = posix_memalign((void**)&obj, CACHE_SIZE, sz);
    if (res)
        return NULL;

    obj->items = items;
    obj->isize = isize;
    obj->pidx = 0;
    obj->cidx = 0;

    res = sem_init(&obj->producer, 0, items);
    if (res)
        goto failed_s0;
    res = sem_init(&obj->consumer, 0, 0);
    if (res)
        goto failed_s1;

    return obj;

failed_s1:
    sem_destroy(&obj->producer);
failed_s0:
    free(obj);
    return NULL;
}

void ring_buffer_destroy(struct ring_buffer* rb)
{
    sem_destroy(&rb->producer);
    sem_destroy(&rb->consumer);
    free(rb);
}

char* ring_buffer_at(struct ring_buffer* rb, unsigned idx)
{
    unsigned int_idx = (rb->items - 1) & idx;
    return &rb->data[rb->isize * int_idx];
}

static int ring_buffer_stdwait(sem_t* sem, int usecs)
{
    int res;
    if (usecs == -1) {
        res = sem_wait(sem);
    } else if (usecs == 0) {
        res = sem_trywait(sem);
    } else {
        struct timespec t;
        res = clock_gettime(CLOCK_REALTIME, &t);
        if (res)
            return res;

        t.tv_nsec += (usecs % 1000000) * 1000;
        t.tv_sec += (usecs / 1000000);
        if (t.tv_nsec > 1000000000) {
            t.tv_nsec -= 1000000000;
            t.tv_sec++;
        }

        res = sem_timedwait(sem, &t);
    }
    return res;
}

unsigned ring_buffer_pwait(struct ring_buffer* rb, int usecs)
{
    int res;
    do {
        res = ring_buffer_stdwait(&rb->producer, usecs);
    } while (res == -1 && errno == EINTR);
    if (res == 0)
        return rb->pidx++;

    return IDX_TIMEDOUT;
}

void ring_buffer_ppost(struct ring_buffer* rb)
{
    int res = sem_post(&rb->consumer);
    assert(res == 0);
}

unsigned ring_buffer_cwait(struct ring_buffer* rb, int usecs)
{
    int res;
    do {
        res = ring_buffer_stdwait(&rb->consumer, usecs);
    } while (res == -1 && errno == EINTR);
    if (res == 0)
        return rb->cidx++;

    return IDX_TIMEDOUT;
}

void ring_buffer_cpost(struct ring_buffer* rb)
{
    int res = sem_post(&rb->producer);
    assert(res == 0);
}




