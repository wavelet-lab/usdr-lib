// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef UNIX_VLL_H
#define UNIX_VLL_H

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/un.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


struct vll_chan {
    int fd;
};

int vll_chan_create(struct vll_chan* vc, const char* dev);
int vll_chan_connect(struct vll_chan* vc, const char* dev);

// Return number of bytes sent
int vll_chan_send_sync(struct vll_chan* vc, const uint8_t* data, unsigned size);
int vll_chan_recv_sync(struct vll_chan* vc, uint8_t* data, unsigned max_size);

int vll_chan_close(struct vll_chan* vc);


struct vll_mem {
    void *addr;
    size_t len;
    int fd;

};

int vll_mem_create(struct vll_mem* mm, const char* mem, size_t maxsz);
int vll_mem_open(struct vll_mem* mm, const char* mem, size_t maxsz);

int vll_mem_protect(struct vll_mem* mm, uint32_t addr, uint32_t sz, int flags);

int vll_mem_close(struct vll_mem* mm);

#ifdef __cplusplus
}
#endif

#endif
