// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

// Communiction protocol
#ifndef VPU_H
#define VPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct pheader {
    uint8_t type;
    uint8_t tag;
    uint16_t size; // Total size of packet in bytes
};


enum ptype {
    // followed by 32bit address + data
    VPT_MEMORY_WRITE_32 = 0,
    // followed by 32bit address + data
    VPT_MEMORY_READ_REQ_32 = 1,

    VPT_MEMORY_READ_CPLD = 2,
    VPT_MEMORY_READ_CPLD_LAST = 3,

    // followed by 64bit address + data
    VPT_MEMORY_WRITE_64 = 4,
    // followed by 64bit address + data
    VPT_MEMORY_READ_REQ_64 = 5,

    // Transaction abortion
    VPT_MEMORY_READ_CPL = 6,

    // MSI number n tag field
    VPT_INTERRUPT_NOTIFICATON = 7,

    // mmap() 1DW start address, 2DW length, 3DW flags
    VPT_MMAP_REGON_32 = 254,
    // Get internal device UUID of simulation
    VPT_GET_UUID = 255,
};

enum vpt_mem_region_flags {
    VPT_MRF_NONE = 0,
    VPT_MRF_READ = 1,
    VPT_MRF_WRITE = 2,
};

struct vll_chan;

int vpu_recv_pkt(struct vll_chan* pvpu, struct pheader* ph, uint32_t* data, unsigned maxsz);

int vpu_send_devuuid(struct vll_chan* pvpu, uint8_t devid[16]);
int vpu_send_msi(struct vll_chan* pvpu, uint32_t msi);
int vpu_send_cpldl_32(struct vll_chan* pvpu, uint8_t tag, uint32_t data);
int vpu_send_cpldl_64(struct vll_chan* pvpu, uint8_t tag, uint64_t data);
int vpu_send_memwr32(struct vll_chan* pvpu, uint32_t addr, const uint32_t* pdw, unsigned dwcnt);
int vpu_send_memrdreq32(struct vll_chan* pvpu, uint32_t addr, unsigned dwcnt, uint8_t tag);
int vpu_send_mmap_req32(struct vll_chan* pvpu, uint32_t addr, uint32_t size, uint32_t flags);


#ifdef __cplusplus
}
#endif

#endif
