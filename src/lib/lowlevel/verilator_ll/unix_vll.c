// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "unix_vll.h"
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


static
int _vll_chan_create_int(struct vll_chan* vc, const char* dev, bool server)
{
    int s, err;
    s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0)
        return errno;

    struct sockaddr_un address;
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, dev, sizeof(address.sun_path));

    if (server) {
        err = bind(s, (struct sockaddr*)(&address), sizeof(address));
        if (err)
            return -errno;

        err = listen(s, 2);
        if (err)
            return -errno;
    } else {

        err = connect(s, (struct sockaddr*)(&address), sizeof(address));
        if (err)
            return -errno;
    }

    vc->fd = s;
    return 0;
}


int vll_chan_create(struct vll_chan* vc, const char* dev)
{
    return _vll_chan_create_int(vc, dev, true);
}


int vll_chan_connect(struct vll_chan* vc, const char* dev)
{
    return _vll_chan_create_int(vc, dev, false);
}

int vll_chan_send_sync(struct vll_chan* vc, const uint8_t* data, unsigned size)
{
    int res;
    res = send(vc->fd, data, size, MSG_NOSIGNAL); //MSG_DONTWAIT
    return res;
}

int vll_chan_recv_sync(struct vll_chan* vc, uint8_t* data, unsigned max_size)
{
    int res;
    res = recv(vc->fd, data, max_size, 0);
    return res;
}

int vll_chan_close(struct vll_chan* vc)
{
    close(vc->fd);
    return 0;
}





static
int _vll_mem_create_int(struct vll_mem* mm, const char* memfile, size_t maxsz, bool crt)
{
    int fd, res;
    void *addr;

    fd = open(memfile, (crt ? O_CREAT : 0) | O_RDWR, 0666);
    if (fd < 0)
        return -errno;

    if (crt) {
        res = ftruncate(fd, maxsz);
        if (res)
            return -errno;
    }

    addr = mmap(NULL, maxsz, PROT_NONE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED)
        return -errno;

    mm->fd = fd;
    mm->addr = addr;
    mm->len = maxsz;
    return 0;
}

int vll_mem_create(struct vll_mem* mm, const char* mem, size_t maxsz)
{
    return _vll_mem_create_int(mm, mem, maxsz, true);
}

int vll_mem_open(struct vll_mem* mm, const char* mem, size_t maxsz)
{
    return _vll_mem_create_int(mm, mem, maxsz, false);
}


int vll_mem_protect(struct vll_mem* mm, uint32_t addr, uint32_t sz, int flags)
{
    int res = mprotect(mm->addr + addr, sz, flags);
    if (res)
        return -errno;
    return 0;
}

int vll_mem_close(struct vll_mem* mm)
{
    munmap(mm->addr, mm->len);
    close(mm->fd);

    return 0;
}

