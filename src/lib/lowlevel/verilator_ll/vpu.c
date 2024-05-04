// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "vpu.h"
#include "unix_vll.h"

#include <errno.h>


int vpu_recv_pkt(struct vll_chan* pvpu, struct pheader* ph, uint32_t* data, unsigned maxsz)
{
    int res;
    res = vll_chan_recv_sync(pvpu, (uint8_t* )ph, sizeof(*ph));
    if (res < 0)
        return res;
    if (res != sizeof (*ph))
        return -EIO;

    if (ph->size % 4)
        return -EIO;
    if (ph->size - sizeof(*ph) > maxsz * 4)
        return -EIO;
    if (ph->size - sizeof(*ph) == 0)
        return 0;

    res = vll_chan_recv_sync(pvpu, (uint8_t* )data, ph->size - sizeof(*ph));
    if (res < 0)
        return res;
    if (res != ph->size - sizeof(*ph))
        return -EIO;

    return 0;
}

int vpu_send_devuuid(struct vll_chan* pvpu, uint8_t devid[16])
{
    const unsigned psz = (sizeof(struct pheader) + 16) / sizeof(uint32_t);
    uint32_t buff[psz];
    int res;
    struct pheader* ph = (struct pheader*)&buff[0];

    ph->type = VPT_GET_UUID;
    ph->tag = 0;
    ph->size = psz * sizeof(uint32_t);
    memcpy(buff + 1, devid, 16);

    res = vll_chan_send_sync(pvpu, (const uint8_t*)buff, ph->size);
    return res == ph->size ? 0 : -EIO;
}

int vpu_send_msi(struct vll_chan* pvpu, uint32_t msi)
{
    uint32_t buff[1];
    int res;
    struct pheader* ph = (struct pheader*)&buff[0];

    ph->type = VPT_INTERRUPT_NOTIFICATON;
    ph->tag = msi;
    ph->size = 1 * sizeof(uint32_t);

    res = vll_chan_send_sync(pvpu, (const uint8_t*)buff, ph->size);
    return res == ph->size ? 0 : -EIO;
}

int vpu_send_cpldl_32(struct vll_chan* pvpu, uint8_t tag, uint32_t data)
{
    uint32_t buff[2];
    int res;
    struct pheader* ph = (struct pheader*)&buff[0];

    ph->type = VPT_MEMORY_READ_CPLD_LAST;
    ph->tag = tag;
    ph->size = 2 * sizeof(uint32_t);

    buff[1] = data;

    res = vll_chan_send_sync(pvpu, (const uint8_t*)buff, ph->size);
    return res == ph->size ? 0 : -EIO;
}

int vpu_send_cpldl_64(struct vll_chan* pvpu, uint8_t tag, uint64_t data)
{
    uint32_t buff[3];
    int res;
    struct pheader* ph = (struct pheader*)&buff[0];

    ph->type = VPT_MEMORY_READ_CPLD_LAST;
    ph->tag = tag;
    ph->size = 3 * sizeof(uint32_t);

    buff[1] = data;
    buff[2] = data >> 32;

    res = vll_chan_send_sync(pvpu, (const uint8_t*)buff, ph->size);
    return res == ph->size ? 0 : -EIO;
}

int vpu_send_memwr32(struct vll_chan* pvpu, uint32_t addr, const uint32_t* pdw, unsigned dwcnt)
{
    const unsigned psz = sizeof(struct pheader) / sizeof(uint32_t) + 1 + dwcnt;
    uint32_t buff[psz];
    int res;
    struct pheader* ph = (struct pheader*)&buff[0];

    ph->type = VPT_MEMORY_WRITE_32;
    ph->tag = 0;
    ph->size = psz * sizeof(uint32_t);
    buff[1] = addr;
    memcpy(buff + 2, pdw, dwcnt * 4);

    res = vll_chan_send_sync(pvpu, (const uint8_t*)buff, ph->size);
    return res == ph->size ? 0 : -EIO;
}

int vpu_send_memrdreq32(struct vll_chan* pvpu, uint32_t addr, unsigned dwcnt, uint8_t tag)
{
    uint32_t buff[3];
    int res;
    struct pheader* ph = (struct pheader*)&buff[0];

    ph->type = VPT_MEMORY_READ_REQ_32;
    ph->tag = tag;
    ph->size = 3 * sizeof(uint32_t);

    buff[1] = addr;
    buff[2] = dwcnt;

    res = vll_chan_send_sync(pvpu, (const uint8_t*)buff, ph->size);
    return res == ph->size ? 0 : -EIO;
}

int vpu_send_mmap_req32(struct vll_chan* pvpu, uint32_t addr, uint32_t size, uint32_t flags)
{
    uint32_t buff[4];
    int res;
    struct pheader* ph = (struct pheader*)&buff[0];

    ph->type = VPT_MMAP_REGON_32;
    ph->tag = 0;
    ph->size = 4 * sizeof(uint32_t);

    buff[1] = addr;
    buff[2] = size;
    buff[3] = flags;

    res = vll_chan_send_sync(pvpu, (const uint8_t*)buff, ph->size);
    return res == ph->size ? 0 : -EIO;
}
