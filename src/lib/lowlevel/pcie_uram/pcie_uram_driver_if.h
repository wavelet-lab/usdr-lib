// Copyright (c) 2023-2024 Wavelet Lab
//
// This work is dual-licensed under MIT and GPL 2.0.
// You can choose between one of them if you use this work.
//
// SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) OR MIT

#ifndef PCIE_URAM_DRIVER_H
#define PCIE_URAM_DRIVER_H

#include <linux/ioctl.h>
#include <linux/string.h>
#include <linux/types.h>

struct pcie_driver_uuid {
    unsigned char id[16];
};

#define MAX_SPI_COUNT      8
#define MAX_I2C_COUNT      4
#define MAX_INDEXED_SPACES 4
#define MAX_STREAM_COUNT   16
#define MAX_MEMORY_COUNT   4

// [3-0] MIN_BUFS  2^n
// [7-4] MAX_BUFS  2^n
// [11-8] MAX_BUFSIZE 2^(n+12)


#define STREAM_CAP_MINBUFS_OFF 0
#define STREAM_CAP_MAXBUFS_OFF 4
#define STREAM_CAP_MAXBUFSZ_OFF 8

#define STREAM_CAP_SZ_MSK 0xf

struct pcie_driver_devlayout {
    unsigned spi_cnt;
    unsigned i2c_cnt;
    unsigned idx_regsp_cnt;

    unsigned interrupt_count;
    unsigned interrupt_core;
    unsigned interrupt_base;

    unsigned streams_count;
    unsigned intfifo_count;

    unsigned spi_base[MAX_SPI_COUNT];
    unsigned spi_core[MAX_SPI_COUNT];
    unsigned spi_int_number[MAX_SPI_COUNT];

    unsigned i2c_base[MAX_I2C_COUNT];
    unsigned i2c_core[MAX_I2C_COUNT];
    unsigned i2c_int_number[MAX_I2C_COUNT];

    unsigned idx_regsp_base[MAX_INDEXED_SPACES];
    unsigned idx_regsp_vbase[MAX_INDEXED_SPACES];

    unsigned stream_cfg_base[MAX_STREAM_COUNT];
    unsigned stream_cnf_base[MAX_STREAM_COUNT];
    unsigned stream_core[MAX_STREAM_COUNT];
    unsigned stream_cap[MAX_STREAM_COUNT];
    unsigned stream_int_number[MAX_STREAM_COUNT];

    unsigned intfifo_uaddr[MAX_MEMORY_COUNT];
    unsigned intfifo_length[MAX_MEMORY_COUNT];
    unsigned intfifo_flags[MAX_MEMORY_COUNT];

    unsigned bucket_core;
    unsigned bucket_base;
    unsigned bucket_count;

    int poll_event_rd;
    int poll_event_wr;
};

struct pcie_driver_spi32 {
    unsigned busno;
    unsigned dw_io;
};

struct pcie_driver_spi_bulk {
    unsigned busno;
    unsigned widthcount;
    unsigned rbmask;
    void* out_data;
    void* in_data;
};

struct pcie_driver_si2c {
    unsigned addr;
    unsigned wcnt;
    unsigned rcnt;
    unsigned char wrb[8];
    unsigned char rdb[8];
    void* wrb_p; // Reserved for future I2C cores
    void* rdb_p; // Reserved for future I2C cores
};

enum sdma_stream_type {
    STREAM_MMAPED,
    STREAM_KERNEL,
};

struct pcie_driver_sdma_conf {
    unsigned type;
    unsigned sno;         //Stream number

    unsigned dma_bufs;    // Total number of buffers
    unsigned dma_buf_sz;  // Size of each buffer in bytes

    off_t out_vma_off;    // Offset need to be passed to mmap() for this stream
    size_t out_vma_length;// Length of total allocated space
};

struct pcie_driver_hwreg32 {
    unsigned addr;
    unsigned value;
};

struct pcie_driver_hwreg64 {
    unsigned addr;
    uint64_t value;
};

struct pcie_driver_srecv {
    unsigned flags;
    unsigned streamnoto;
    unsigned user_bufsz;
    unsigned aux_bufsz;
    void* user_buffer;
    void* aux_buffer;
};

struct pcie_driver_woa_oob {
    unsigned streamnoto;
    unsigned ooblength;
    void* oobdata;
};

// Driver functions

#define PCIE_DRIVER_MAGIC          0xDD
#define PCIE_DRIVER_GET_UUID       _IOR(PCIE_DRIVER_MAGIC, 0, struct pcie_driver_uuid)
#define PCIE_DRIVER_CLAIM          _IO(PCIE_DRIVER_MAGIC, 1)
#define PCIE_DRIVER_SET_DEVLAYOUT  _IOW(PCIE_DRIVER_MAGIC, 2, struct pcie_driver_devlayout)

#define PCIE_DRIVER_HWREG_RD32     _IOR(PCIE_DRIVER_MAGIC, 3, struct pcie_driver_hwreg32)
#define PCIE_DRIVER_HWREG_WR32     _IOWR(PCIE_DRIVER_MAGIC, 3, struct pcie_driver_hwreg32)
#define PCIE_DRIVER_HWREG_RD64     _IOR(PCIE_DRIVER_MAGIC, 3, struct pcie_driver_hwreg64)
#define PCIE_DRIVER_HWREG_WR64     _IOWR(PCIE_DRIVER_MAGIC, 3, struct pcie_driver_hwreg64)

#define PCIE_DRIVER_SPI32_TRANSACT _IOWR(PCIE_DRIVER_MAGIC, 4, struct pcie_driver_spi32)
#define PCIE_DRIVER_SI2C_TRANSACT  _IOWR(PCIE_DRIVER_MAGIC, 5, struct pcie_driver_si2c)

#define PCIE_DRIVER_WAIT_SINGLE_EVENT     _IOW(PCIE_DRIVER_MAGIC, 6, uint32_t)

#define PCIE_DRIVER_DMA_CONF              _IOWR(PCIE_DRIVER_MAGIC, 16, struct pcie_driver_sdma_conf)
#define PCIE_DRIVER_DMA_UNCONF            _IOW(PCIE_DRIVER_MAGIC, 16, uint32_t)

#define PCIE_DRIVER_DMA_WAIT     _IOW(PCIE_DRIVER_MAGIC, 17, uint32_t)
#define PCIE_DRIVER_DMA_WAIT_OOB _IOWR(PCIE_DRIVER_MAGIC, 17, struct pcie_driver_woa_oob)

#define PCIE_DRIVER_DMA_RELEASE_OR_POST   _IOW(PCIE_DRIVER_MAGIC, 18, uint32_t)

#define PCIE_DRIVER_DMA_ALLOC     _IOW(PCIE_DRIVER_MAGIC, 19, uint32_t)
#define PCIE_DRIVER_DMA_ALLOC_OOB _IOWR(PCIE_DRIVER_MAGIC, 19, struct pcie_driver_woa_oob)

// pread & pwrite to 0 stream with no aux info
#define PCIE_DRIVER_SRECV                _IOW(PCIE_DRIVER_MAGIC, 21, struct pcie_driver_srecv)
#define PCIE_DRIVER_SSEND                _IOW(PCIE_DRIVER_MAGIC, 22, struct pcie_driver_srecv)

// Request specific deriver ABI version
#define PCIE_DRIVER_CLAIM_VERSION     _IOW(PCIE_DRIVER_MAGIC, 23, uint32_t)

#endif
