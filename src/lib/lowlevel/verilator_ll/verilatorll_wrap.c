// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <endian.h>
#include <semaphore.h>
#include <pthread.h>
#include <assert.h>

#include <usdr_logging.h>

#include <string.h>
#include <sys/un.h>

#include "../device/device.h"
#include "../device/device_bus.h"
#include "../device/device_names.h"
#include "../device/device_vfs.h"
#include "../device/device_cores.h"

#include "../ipblks/si2c.h"

#include "unix_vll.h"
#include "verilatorll_wrap.h"
#include "vpu.h"

enum {
    MAX_TAGS = 32,
    MAX_INTERRUPTS = 32,
    TO_IRQ_POLL = 250,

    MAX_PACKET_SZ = 256,

    MMAP_SIZE = 0x100000000ul
};

struct rbdata {
    uint32_t data[MAX_PACKET_SZ / 4];
};


struct verilator_protocol_unix {
    sem_t tags[MAX_TAGS];
    sem_t tags_avail;

    uint32_t tags_free_idx;

    pthread_mutex_t mtx;
    struct vll_chan vch;
    struct vll_mem  vchm;

    struct rbdata rb[MAX_TAGS];
};

typedef struct verilator_protocol_unix verilator_protocol_unix_t;

int vpu_init(verilator_protocol_unix_t* pvpu, const char* dev)
{
    int res;

    for (unsigned i = 0; i < MAX_INTERRUPTS; i++) {
        res = sem_init(&pvpu->tags[i], 0, 0);
        if (res)
            goto sem_tag_fail;
    }

    res = sem_init(&pvpu->tags_avail, 0, 1);
    if (res)
        goto sem_tag_avail_fail;

    pvpu->tags_free_idx =0;
    res = pthread_mutex_init(&pvpu->mtx, NULL);
    if (res)
        goto mtx_failed;

    res = vll_chan_connect(&pvpu->vch, dev);
    if (res) {
        USDR_LOG("VERI", USDR_LOG_CRITICAL_WARNING, "Connecton to verilator filed: error %d\n", res);
        goto conn_failed;
    }

    char mmapfile[256];
    snprintf(mmapfile, sizeof (mmapfile), "%s.mmap", dev);
    res = vll_mem_open(&pvpu->vchm, mmapfile, MMAP_SIZE);
    if (res) {
        USDR_LOG("VERI", USDR_LOG_CRITICAL_WARNING, "Openning MMAP area failed: error %d\n", res);
        goto conn_failed;
    }

    return 0;

conn_failed:

mtx_failed:

sem_tag_avail_fail:

sem_tag_fail:
    //Cleanup sem m
    return res;
}



#define MAX_STREAM_COUNT   16
struct verilator_dev
{
    struct lowlevel_ops* ops;
    pdevice_t udev;

    char name[128];
    char devid_str[36];
    device_id_t devid;
    device_bus_t db;

    sem_t interrupts[MAX_INTERRUPTS];

    verilator_protocol_unix_t proto;
    bool terminated;
    //
    unsigned spi_int_number[MAX_INTERRUPTS];
    unsigned i2c_int_number[MAX_INTERRUPTS];
    unsigned stream_int_number[MAX_STREAM_COUNT];
    unsigned stream_cap[MAX_STREAM_COUNT];


    pthread_t iothread;

    void* rx_ch[32];

    unsigned bno;

    unsigned ntfy_idx;

    uint32_t rb_event_data[MAX_INTERRUPTS];

    uint32_t stat_dma_rx[4 * 64];
    unsigned stat_dma_rx_wr;

    int delayed_ints;
};

typedef struct verilator_dev verilator_dev_t;

enum {
    PHYS_NTFY_BASE   = 0xfee00000,
    PHYS_RX_DMA_ST_0 = 0x10000000,
    PHYS_RX_DMA_OFF  = 0x01000000,
};

static int verilator_wrap_reg_out(verilator_dev_t* dev, unsigned reg,
                                uint32_t outval);
static int verilator_wrap_reg_in(verilator_dev_t* dev, unsigned reg,
                               uint32_t *pinval);
#define NEW_EVNT_ABI

int verilator_process_recv(verilator_dev_t* dev)
{
    uint32_t buffer[64];
    int res;
    struct pheader hdr;

    res = vpu_recv_pkt(&dev->proto.vch, &hdr, buffer, SIZEOF_ARRAY(buffer));
    if (res)
        return res;

    switch (hdr.type) {
    case VPT_MEMORY_READ_CPLD_LAST:
        if (hdr.tag > MAX_TAGS)
            return -EFAULT;

        if (hdr.size >= 12)
            USDR_LOG("VERI", USDR_LOG_TRACE, "Readback #%d => %08x%08x\n", hdr.tag, buffer[1], buffer[0]);
        else
            USDR_LOG("VERI", USDR_LOG_TRACE, "Readback #%d => %08x\n", hdr.tag, buffer[0]);

        memcpy(dev->proto.rb[hdr.tag].data, buffer, hdr.size - sizeof(hdr));
        res = sem_post(&dev->proto.tags[hdr.tag]);
        if (res)
            return res;

        break;

    case VPT_INTERRUPT_NOTIFICATON:
        if (hdr.tag > MAX_INTERRUPTS)
            return -EFAULT;
        USDR_LOG("VERI", USDR_LOG_TRACE, "Interrupt %d\n", hdr.tag);
#ifdef OLD_INTERRUPTS
        res = sem_post(&dev->interrupts[hdr.tag]);
        if (res)
            return res;
#else
        unsigned do_cnf = ~0u, i;
        uint32_t* bptr = (uint32_t*)((uint8_t*)dev->proto.vchm.addr + PHYS_NTFY_BASE);

        /*
        if (rand() > RAND_MAX - RAND_MAX/200) {
            USDR_LOG("VERI", USDR_LOG_ERROR, "Interrupt skipped!\n");
            return 0;
        }
        */

        // based on 128 bit notifications
        for (i = dev->ntfy_idx; i < dev->ntfy_idx + 256; i++) {
            uint32_t data[4];
            unsigned event_no, flags, j;

            for (j = 0; j < 4; j++)
                data[j] = ntohl(bptr[(4 * i + j) & 0x3ff]);

#ifndef NEW_EVNT_ABI
            event_no = data[3] >> 29;
            flags = (data[3] & (1u<<28)) ? 1 : 0;
#else
            flags = data[0] >> 31;
            event_no = data[0] & 0x3f;
#endif
            if (flags != ((i >> 8) & 1)) {
                break;
            }
            if (i % 32 == 31) {
                do_cnf = (i << 1) & 0x3ff;
            }

            USDR_LOG("VERI", USDR_LOG_TRACE, "BUCKET %d IRQ %d: Event %d Flag: %d; RPTR %d; Data: %08x_%08x_%08x_%08x\n",
                     i, hdr.tag, event_no, flags, dev->ntfy_idx, data[3], data[2], data[1], data[0]);

            {
                unsigned irq = event_no;

                if  (irq == 0) {
#ifndef NEW_EVNT_ABI
                    dev->stat_dma_rx[4 * dev->stat_dma_rx_wr + 0] = data[2];
                    dev->stat_dma_rx[4 * dev->stat_dma_rx_wr + 1] = data[1];
                    dev->stat_dma_rx[4 * dev->stat_dma_rx_wr + 2] = data[0];
#else
                    dev->stat_dma_rx[4 * dev->stat_dma_rx_wr + 0] = data[1];
                    dev->stat_dma_rx[4 * dev->stat_dma_rx_wr + 1] = data[2];
                    dev->stat_dma_rx[4 * dev->stat_dma_rx_wr + 2] = data[3];
#endif
                    if (++dev->stat_dma_rx_wr == 64)
                        dev->stat_dma_rx_wr = 0;
                }

                if (irq == 0) {
                    if (dev->ntfy_idx == 132) {
                        dev->delayed_ints++;
                        goto skip_interrupt;
                    }
                }

                for (;;) {
                    res = sem_post(&dev->interrupts[irq]);
                    if (res)
                        return res;
                    if (irq != 0)
                        break;
                    if (dev->delayed_ints == 0)
                        break;

                    dev->delayed_ints--;
                }
                skip_interrupt:;
            }

        }

        dev->ntfy_idx = i & 0x1ff;

        if (do_cnf != ~0u) {
            //Send confirmation pointer
            verilator_wrap_reg_out(dev, 9, (0 << 16) | do_cnf);
        }

#if 0
        uint32_t* ntfy_mem = (uint32_t*)((uint8_t*)dev->proto.vchm.addr + PHYS_NTFY_BASE) + ((2 * dev->ntfy_idx) % 1024);
        // 128 bit ntfy
        unsigned irq = (ntfy_mem[3] & 0xff) >> 5;
        dev->rb_event_data[irq] = ntohl(ntfy_mem[0]);

        USDR_LOG("VERI", USDR_LOG_TRACE, "Interrupt %d (from %d) idx=%d [Event %08x %08x %08x %08x]\n", hdr.tag,
                 irq, dev->ntfy_idx,
                 ntfy_mem[0], ntfy_mem[1], ntfy_mem[2], ntfy_mem[3]);

        dev->ntfy_idx += 2;
        if (dev->ntfy_idx >= 1024)
            dev->ntfy_idx = 0;

        // Confirm each 128 events
        if (dev->ntfy_idx % 128 == 124) {
            verilator_wrap_reg_out(dev, 9, dev->ntfy_idx);
        }

        if  (irq == 0) {
            dev->stat_dma_rx[4 * dev->stat_dma_rx_wr + 0] = htonl(ntfy_mem[2]);
            dev->stat_dma_rx[4 * dev->stat_dma_rx_wr + 1] = htonl(ntfy_mem[1]);
            dev->stat_dma_rx[4 * dev->stat_dma_rx_wr + 2] = htonl(ntfy_mem[0]);
            if (++dev->stat_dma_rx_wr == 64)
                dev->stat_dma_rx_wr = 0;
        }


        if (irq == 0) {
            if (dev->ntfy_idx == 132) {
                dev->delayed_ints++;
                return 0;
            }
        }

        for (;;) {
            res = sem_post(&dev->interrupts[irq]);
            if (res)
                return res;
            if (irq != 0)
                break;
            if (dev->delayed_ints == 0)
                break;

            dev->delayed_ints--;
        }
#endif

#endif
        break;
    default:
        return -EFAULT;
    }

    return 0;
}

static
int verilator_tag_alloc(verilator_protocol_unix_t* dev)
{
    int res, tag = -1;
    res = sem_wait(&dev->tags_avail);
    if (res)
        return res;


    for (unsigned i = 0; i < 32; i++) {
        if (~dev->tags_free_idx & (1u << i)) {
            //TODO atomic or
            tag = i;
            break;
        }
    }

    return tag;
}

static
int verilator_tag_release(verilator_protocol_unix_t* dev, unsigned tag)
{
    int res;
    res = sem_post(&dev->tags_avail);
    if (res)
        return res;

    return 0;
}

static
int verilator_in(verilator_dev_t* dev, unsigned addr, uint32_t *pinval, const unsigned dwcnt)
{
    int res, tag;
    tag = verilator_tag_alloc(&dev->proto);
    if (tag < 0)
        return tag;

    res = vpu_send_memrdreq32(&dev->proto.vch, addr, dwcnt, tag);
    if (res < 0)
        return res;

    res = sem_wait(&dev->proto.tags[tag]);
    if (res)
        return res;

    memcpy(pinval, dev->proto.rb[tag].data, dwcnt * 4);

    res = verilator_tag_release(&dev->proto, tag);
    if (res)
        return res;

    return 0;
}

void* thread_verilator(void* obj)
{
    verilator_dev_t* dev = (verilator_dev_t*)obj;
    int res;

    USDR_LOG("VERI", USDR_LOG_NOTE, "Verilator monitor thread started\n");

    while (!dev->terminated) {
        res = verilator_process_recv(dev);
        if (res == -EINTR)
            continue;
        else if (res < 0) {
            USDR_LOG("VERI", USDR_LOG_DEBUG, "Verilator monitor thread error: %d\n", res);
            return (void*)(intptr_t)res;
        }
    }

    USDR_LOG("VERI", USDR_LOG_NOTE, "Verilator monitor thread stopped\n");
    return NULL;
}

// Common operations
static
int verilator_wrap_generic_get(lldev_t dev, int generic_op, const char** pout)
{
    verilator_dev_t* d = (verilator_dev_t*)dev;

    switch (generic_op) {
    case LLGO_DEVICE_NAME: *pout = d->name; return 0;
    case LLGO_DEVICE_UUID: *pout = (const char*)d->devid.d; return 0;
    }

    return -EINVAL;
}


int verilator_wrap_reg_out(verilator_dev_t* dev, unsigned reg,
                                uint32_t outval)
{
    int res;
    res = vpu_send_memwr32(&dev->proto.vch, reg * 4, &outval, 1);
    USDR_LOG("VERI", USDR_LOG_DEBUG, "%s: Write [%04x] = %08x (%d)\n",
             dev->name, reg, outval, res);
    if (res < 0)
        return res;
    return 0;
}

int verilator_wrap_reg_in(verilator_dev_t* dev, unsigned reg,
                               uint32_t *pinval)
{
    int res;
    res = verilator_in(dev, reg * 4, pinval, 1);
    USDR_LOG("VERI", USDR_LOG_DEBUG, "%s: Read  [%04x] = %08x (%d)\n",
             dev->name, reg, *pinval, res);
    return res;
}



static int verilator_wrap_reg_out_n(verilator_dev_t* dev, unsigned reg,
                                  const uint32_t *outval, const unsigned dwcnt)
{
    int res;
    res = vpu_send_memwr32(&dev->proto.vch, reg * 4, outval, dwcnt);
    USDR_LOG("VERI", USDR_LOG_DEBUG, "%s: WriteArray [%04x + %d] (%d)\n",
             dev->name, reg, dwcnt, res);
    if (res < 0)
        return res;
    return 0;
}

static int verilator_wrap_reg_in_n(verilator_dev_t* dev, unsigned reg,
                                 uint32_t *pinval, const unsigned dwcnt)
{
    int res;
    res = verilator_in(dev, reg * 4, pinval, dwcnt);
    if (dwcnt == 2) {
        USDR_LOG("VERI", USDR_LOG_DEBUG, "%s: ReadArray [%04x + %d] = %04x%04x (%d)\n",
                 dev->name, reg, dwcnt, pinval[1], pinval[0], res);
    } else {
        USDR_LOG("VERI", USDR_LOG_DEBUG, "%s: ReadArray [%04x + %d] (%d)\n",
                 dev->name, reg, dwcnt, res);
    }
    return res;
}

static
int verilator_wrap_reg_op(verilator_dev_t* d, unsigned ls_op_addr,
                        uint32_t* ina, size_t meminsz, const uint32_t* outa, size_t memoutsz)
{
    unsigned i;
    int res;

    if ((meminsz % 4) || (memoutsz % 4))
        return -EINVAL;

    for (unsigned k = 0; k < d->db.idx_regsps; k++) {
        if (ls_op_addr >= d->db.idxreg_virt_base[k]) {
            // Indexed register operation
            unsigned amax = ((memoutsz > meminsz) ? memoutsz : meminsz) / 4;

            for (i = 0; i < amax; i++) {
                //Write address
                res = verilator_wrap_reg_out(d, d->db.idxreg_base[k],
                                           ls_op_addr - d->db.idxreg_virt_base[k] + i);
                if (res)
                    return res;

                if (i < memoutsz / 4) {
                    res = verilator_wrap_reg_out(d, d->db.idxreg_base[k] + 1, outa[i]);
                    if (res)
                        return res;
                }

                if (i < meminsz / 4) {
                    res = verilator_wrap_reg_in(d, d->db.idxreg_base[k] + 1, &ina[i]);
                    if (res)
                        return res;
                }
            }

            return 0;
        }
    }
#if 1
    // TODO Wrap to 128b
    if (memoutsz > 4) {
        res = verilator_wrap_reg_out_n(d, ls_op_addr, outa, memoutsz / 4);
        if (res)
            return res;
    } else if (memoutsz == 4) {
        res = verilator_wrap_reg_out(d, ls_op_addr, outa[0]);
        if (res)
            return res;
    }

    if (meminsz > 4) {
        res = verilator_wrap_reg_in_n(d, ls_op_addr, ina, meminsz / 4);
        if (res)
            return res;
    } else if (meminsz == 4) {
        res = verilator_wrap_reg_in(d, ls_op_addr, ina);
        if (res)
            return res;
    }
#else
    // Normal operation
    for (i = 0; i < memoutsz / 4; i++) {
        res = verilator_wrap_reg_out(d, ls_op_addr + i, outa[i]);
        if (res)
            return res;
    }
    for (i = 0; i < meminsz / 4; i++) {
        res = verilator_wrap_reg_in(d, ls_op_addr + i, &ina[i]);
        if (res)
            return res;
    }
#endif
    return 0;
}

static int verilator_wrap_wait_msi(verilator_dev_t* dev, unsigned i, int timeout_ms)
{
    int res;
    struct timespec ts;
    res = clock_gettime(CLOCK_REALTIME, &ts);
    if (res)
        return -EFAULT;

    if (timeout_ms > 0) {
        ts.tv_nsec += timeout_ms * 1000 * 1000;
        while (ts.tv_nsec > 1000 * 1000 * 1000) {
            ts.tv_nsec -= 1000 * 1000 * 1000;
            ts.tv_sec++;
        }
        res = sem_timedwait(&dev->interrupts[i], &ts);
    } else if (timeout_ms < 0) {
        res = sem_wait(&dev->interrupts[i]);
    } else {
        res = sem_trywait(&dev->interrupts[i]);
    }
    if (res) {
        // sem_* function on error returns -1, get proper error
        res = -errno;
    }
    return res;
}

static
int verilator_wrap_ls_op(lldev_t dev, subdev_t subdev,
                       unsigned ls_op, lsopaddr_t ls_op_addr,
                       size_t meminsz, void* pin,
                       size_t memoutsz, const void* pout)
{
    int res;
    verilator_dev_t* d = (verilator_dev_t*)dev;

    switch (ls_op) {
    case USDR_LSOP_HWREG: {
        uint32_t* ina = (uint32_t*)pin;
        const uint32_t* outa = (const uint32_t*)pout;

        return verilator_wrap_reg_op(d, ls_op_addr, ina, meminsz, outa, memoutsz);
    }
    case USDR_LSOP_SPI: {
        if (ls_op_addr > d->db.spi_count)
            return -EINVAL;

        if (((meminsz != 4) && (meminsz != 0)) || (memoutsz != 4))
            return -EINVAL;

        res = verilator_wrap_reg_out(d, d->db.spi_base[ls_op_addr], *(const uint32_t*)pout);
        if (res)
            return res;

        res = verilator_wrap_wait_msi(dev, d->spi_int_number[ls_op_addr], 10000);
        if (res) {
            USDR_LOG("VERI", USDR_LOG_ERROR, "%s: SPI%d MSI wait timed out!\n",
                     d->name, ls_op_addr);
            return res;
        }

        if (meminsz != 0) {
#ifdef OLD_INTERRUPTS
            res = verilator_wrap_reg_in(d, d->db.spi_base[ls_op_addr], (uint32_t*)pin);
            if (res)
                return res;
#else
            *(uint32_t*)pin = d->rb_event_data[d->spi_int_number[ls_op_addr]];
#endif
        }
        return 0;
    }
    case USDR_LSOP_I2C_DEV: {
        uint32_t i2ccmd, data = 0;
        const uint8_t* dd = (const uint8_t*)pout;
        uint8_t* di = (uint8_t*)pin;

        res = si2c_make_ctrl_reg(ls_op_addr, dd, memoutsz, meminsz, &i2ccmd);
        if (res)
            return res;

        res = verilator_wrap_reg_out(d, d->db.i2c_base[ls_op_addr >> 8], i2ccmd);
        if (res)
            return res;

        if (meminsz > 0) {
            res = verilator_wrap_wait_msi(dev, d->i2c_int_number[ls_op_addr >> 8], 1000);
            if (res) {
                USDR_LOG("VERI", USDR_LOG_ERROR, "%s: I2C%d MSI wait timed out!\n",
                         d->name, ls_op_addr);
                return res;
            }
#ifdef OLD_INTERRUPTS
            res = verilator_wrap_reg_in(d, d->db.i2c_base[ls_op_addr >> 8], &data);
            if (res)
                return res;
#else
            data = d->rb_event_data[d->i2c_int_number[ls_op_addr >> 8]];
#endif
            if (meminsz == 1) {
                di[0] = data;
            } else if (meminsz == 2) {
                di[0] = data;
                di[1] = data >> 8;
            } else if (meminsz == 3) {
                di[0] = data;
                di[1] = data >> 8;
                di[2] = data >> 16;
            } else {
                *(uint32_t*)pin = data;
            }
        }
        return 0;
    }
    }
    return -EOPNOTSUPP;
}


static
int verilator_wrap_stream_initialize(lldev_t dev, subdev_t subdev,
                                lowlevel_stream_params_t* params,
                                stream_t* channel)
{
#define PAGE_SIZE 4096
    verilator_dev_t* d = (verilator_dev_t*)dev;

    // configure core
    uint32_t base_addr = PHYS_RX_DMA_ST_0;
    uint32_t mmap_diff = PHYS_RX_DMA_OFF;
    unsigned sno = params->streamno;
    unsigned page_size =
         (params->block_size + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1));
    unsigned i;

    int res;

    if (sno >= d->db.srx_count)
        return -EINVAL;

#define STREAM_CAP_MINBUFS_OFF 0
#define STREAM_CAP_MAXBUFS_OFF 4
#define STREAM_CAP_MAXBUFSZ_OFF 8
#define STREAM_CAP_SZ_MSK 0xf

    unsigned min_bufs = 1u << ((d->stream_cap[sno] >> STREAM_CAP_MINBUFS_OFF) & STREAM_CAP_SZ_MSK);
    unsigned max_bufs = 1u << ((d->stream_cap[sno] >> STREAM_CAP_MAXBUFS_OFF) & STREAM_CAP_SZ_MSK);
    unsigned max_bufsz = 4096u << ((d->stream_cap[sno] >> STREAM_CAP_MAXBUFSZ_OFF) & STREAM_CAP_SZ_MSK);

    if (params->buffer_count < min_bufs || params->buffer_count > max_bufs || params->block_size > max_bufsz)
        return -EINVAL;

    if (d->db.srx_core[sno] == USDR_MAKE_COREID(USDR_CS_STREAM, USDR_SC_RXDMA_BRSTN)) {
        for (i = 0; i < params->buffer_count; i++) {
            uint32_t phys_addr = base_addr + i * mmap_diff;
            //res = verilator_wrap_reg_out(d, d->db.srx_cfg_base[sno] + i, phys_addr);
            res = verilator_wrap_reg_op(d, d->db.srx_cfg_base[sno] + i, NULL, 0, &phys_addr, 4);
            if (res)
                return res;

            res = vpu_send_mmap_req32(&d->proto.vch, phys_addr, page_size, PROT_WRITE);
            if (res)
                return res;

            res = vll_mem_protect(&d->proto.vchm, phys_addr, page_size, PROT_READ);
            if (res)
                return res;

            USDR_LOG("VERI", USDR_LOG_ERROR, "%s: Stream %d buffer %d mmaped to %08x + %06x\n",
                     d->name, sno, i, phys_addr, page_size);
        }
    } else {
        USDR_LOG("VERI", USDR_LOG_ERROR, "%s: Unknown core!\n",
                 d->name);
        return -EINVAL;
    }

    return 0;
}

static
int verilator_wrap_stream_deinitialize(lldev_t dev, subdev_t subdev, stream_t channel)
{
    return -EOPNOTSUPP;
}

static
int verilator_wrap_recv_dma_wait(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer,
                            void* oob_ptr, unsigned *oob_size, unsigned timeout)
{
    int res;
    verilator_dev_t* d = (verilator_dev_t*)dev;
    res = sem_wait(&d->interrupts[d->stream_int_number[0]]);
    if (res)
        return res;

    *buffer = d->proto.vchm.addr + PHYS_RX_DMA_ST_0 + (d->bno & 0x1f) * PHYS_RX_DMA_OFF;
    if (*oob_size >= 8) {
#ifdef OLD_INTERRUPTS
        res = verilator_wrap_reg_in_n(d, d->db.srx_base[0], ((uint32_t*)oob_size), 2);
        if (res)
            return res;

        *oob_size = 8;
#else
        unsigned j = (d->bno & 0x3f);
        ((uint32_t*)oob_size)[0] = d->stat_dma_rx[4 * j + 0];
        ((uint32_t*)oob_size)[1] = d->stat_dma_rx[4 * j + 1];
        uint32_t stat = d->stat_dma_rx[4 * j + 2];
        assert(((stat >> 24) & 0x3f) == j);
#endif
    }

    d->bno++;
    return 0;
}

static
int verilator_wrap_recv_dma_release(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer)
{
    int res;
    unsigned sno = 0;
    verilator_dev_t* d = (verilator_dev_t*)dev;
    unsigned bno = (buffer - d->proto.vchm.addr - PHYS_RX_DMA_ST_0) / PHYS_RX_DMA_OFF;
    uint32_t cnf = bno;

    res = verilator_wrap_reg_op(d, d->db.srx_base[sno], NULL, 0, &cnf, 4);
    if (res)
        return res;

    return 0;
}

static
int verilator_wrap_send_dma_get(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer, unsigned timeout)
{
    return -ENOTSUP;
}

static
int verilator_wrap_send_dma_commit(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer, unsigned sz, const void* oob_ptr, unsigned oob_size)
{
    return -ENOTSUP;
}

static
int verilator_wrap_await(lldev_t dev, subdev_t subdev, unsigned await_id, unsigned op, void** await_inout_aux_data, unsigned timeout)
{
    return -ENOTSUP;
}

static
int verilator_wrap_recv_buf(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer, unsigned *expected_sz, void* oob_ptr, unsigned *oob_size, unsigned timeout)
{
    return -ENOTSUP;
}

static
int verilator_wrap_send_buf(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer, unsigned sz, const void* oob_ptr, unsigned oob_size, unsigned timeout)
{
    return -ENOTSUP;
}


static
int verilator_wrap_destroy(lldev_t dev)
{
    verilator_dev_t* d = (verilator_dev_t*)dev;
    free(d);
    return 0;
}

// Device operations
static
struct lowlevel_ops s_verilator_wrap_ops = {
    verilator_wrap_generic_get,
    verilator_wrap_ls_op,
    verilator_wrap_stream_initialize,
    verilator_wrap_stream_deinitialize,
    verilator_wrap_recv_dma_wait,
    verilator_wrap_recv_dma_release,
    verilator_wrap_send_dma_get,
    verilator_wrap_send_dma_commit,
    verilator_wrap_recv_buf,
    verilator_wrap_send_buf,
    verilator_wrap_await,
    verilator_wrap_destroy,
};

// Factory functions
static
const char* verilator_wrap_plugin_info_str(unsigned iparam) {
    switch (iparam) {
    case LLPI_NAME_STR: return "verilator-pcie";
    case LLPI_DESCRIPTION_STR: return "Verilator bridge";
    }
    return NULL;
}


static
int verilator_wrap_plugin_discovery(unsigned pcount, const char** filterparams,
                                    const char** filtervals,
                                    unsigned maxcnt, char* outarray)
{
    // TODO discovery

    return -ENODEV;
}



static
int verilator_wrap_plugin_create(unsigned pcount, const char** devparam,
                               const char** devval, lldev_t* odev,
                               UNUSED unsigned vidpid, UNUSED void* webops, UNUSED uintptr_t param)
{
    verilator_dev_t* dev;
    const char* path = "verilator.sock";
    int res;
    device_id_t did;


    dev = (verilator_dev_t*)malloc(sizeof(verilator_dev_t));
    if (dev == NULL) {
        res = ENOMEM;
        goto init_fail;
    }


    res = vpu_init(&dev->proto, path);
    if (res)
        goto alloc_fail;

    struct pheader phvu;
    res = vpu_recv_pkt(&dev->proto.vch, &phvu, (uint32_t*)&did.d, sizeof(did) / sizeof(uint32_t));
    if (res)
        return res;

    if (phvu.size != sizeof(did) + sizeof (phvu))
        return -EFAULT;

    if (phvu.type != VPT_GET_UUID)
        return -EFAULT;

    USDR_LOG("VERI", USDR_LOG_NOTE, "Connected to verilator\n");

    dev->ops = &s_verilator_wrap_ops;
    snprintf(dev->name, sizeof(dev->name) - 1, "%s", path);

    dev->devid = did;
    strncpy(dev->devid_str, usdr_device_id_to_str(did), sizeof(dev->devid_str) - 1);

    for (unsigned i = 0; i < MAX_INTERRUPTS; i++) {
        res = sem_init(&dev->interrupts[i], 0, 0);
        if (res)
            goto alloc_fail;
    }

    // Start thread
    res = pthread_create(&dev->iothread, NULL, thread_verilator, dev);
    if (res)
        goto alloc_fail;

    res = usdr_device_create(dev, did, &dev->udev);
    if (res) {
        USDR_LOG("VERI", USDR_LOG_ERROR,
                 "Unable to find device spcec for %s, uuid %s! Update software!\n",
                 dev->name, usdr_device_id_to_str(did));

        goto remove_dev;
    }

    res = device_bus_init(dev->udev, &dev->db);
    if (res) {
        USDR_LOG("VERI", USDR_LOG_ERROR,
                 "Unable to initialize bus parameters for the device %s!\n", dev->name);

        goto remove_dev;
    }


    struct device_params {
        const char* path;
        unsigned *store;
        unsigned count;
    } bii[] = {
    { DNLLFP_IRQ(DN_BUS_SPI, "%d"), dev->spi_int_number, dev->db.spi_count },
    { DNLLFP_IRQ(DN_BUS_I2C, "%d"), dev->i2c_int_number, dev->db.i2c_count },
    { DNLLFP_IRQ(DN_SRX, "%d"), dev->stream_int_number, dev->db.srx_count },
    //{ DNLLFP_IRQ(DN_STX, "%d"), dl.stream_int_number + dev->db.srx_count, dev->db.stx_count },
    { DNLLFP_NAME(DN_SRX, "%d", DNP_DMACAP), dev->stream_cap, dev->db.srx_count },
    //{ DNLLFP_NAME(DN_STX, "%d", DNP_DMACAP), dl.stream_cap + dev->db.srx_count, dev->db.stx_count },
    };
    uint32_t int_mask = 0;
    for (unsigned i = 0; i < SIZEOF_ARRAY(bii); i++) {
        for (unsigned j = 0; j < bii[i].count; j++) {
            uint64_t tmp;
            char buffer[32];

            snprintf(buffer, sizeof(buffer), bii[i].path, j);
            res = usdr_device_vfs_obj_val_get_by_path(dev->udev, buffer, &tmp);
            if (res) {
                goto remove_dev;
            }

            bii[i].store[j] = (unsigned)tmp;
            int_mask |= (1u << tmp);
        }
    }

    // Initialize interrupts
#ifdef OLD_INTERRUPTS
    res = verilator_wrap_reg_out(dev, 0xf, int_mask);
#else
    //PHYS_NTFY_BASE

    // Initialize interrupts
    for (unsigned i = 0; i < 32; i++) {
        //res = verilator_wrap_reg_out(dev, 0xf,  i | (i << 8) | (0 << 16) | (7 << 20));
        res = verilator_wrap_reg_out(dev, 0xf,  i | (0 << 8) | (1 << 16) | (0 << 20));
    }
#define REG_WR_PNTFY_CFG 8
#define REG_WR_PNTFY_ACK 9

    res = verilator_wrap_reg_out(dev, REG_WR_PNTFY_CFG,  PHYS_NTFY_BASE);

    res = verilator_wrap_reg_out(dev, REG_WR_PNTFY_ACK, 0 << 16);
    res = verilator_wrap_reg_out(dev, REG_WR_PNTFY_ACK, 1 << 16);
    res = verilator_wrap_reg_out(dev, REG_WR_PNTFY_ACK, 2 << 16);
    res = verilator_wrap_reg_out(dev, REG_WR_PNTFY_ACK, 3 << 16);

    res = vpu_send_mmap_req32(&dev->proto.vch, PHYS_NTFY_BASE, 4096, PROT_WRITE);
    if (res)
        return res;

    res = vll_mem_protect(&dev->proto.vchm, PHYS_NTFY_BASE, 4096, PROT_WRITE);
    if (res)
        return res;

    memset(dev->proto.vchm.addr + PHYS_NTFY_BASE, -1, 4096);

    res = vll_mem_protect(&dev->proto.vchm, PHYS_NTFY_BASE, 4096, PROT_READ);
    if (res)
        return res;
#endif

    // Device initialization
    res = dev->udev->initialize(dev->udev);
    if (res) {
        USDR_LOG("VERI", USDR_LOG_ERROR,
                 "Unable to initialize device, error %d\n", res);
        goto remove_dev;
    }

    *odev = dev;
    return 0;

remove_dev:
    dev->terminated = true;
    for (unsigned i = 0; i < MAX_INTERRUPTS; i++) {
        sem_destroy(&dev->interrupts[i]);
    }
alloc_fail:
    free(dev);
init_fail:
    return res;
}

// Factory operations
static const
struct lowlevel_plugin s_verilator_wrap_plugin = {
    verilator_wrap_plugin_info_str,
    verilator_wrap_plugin_discovery,
    verilator_wrap_plugin_create,
};


const struct lowlevel_plugin *verilator_wrap_register()
{
    USDR_LOG("VERI", USDR_LOG_INFO, "Verilator bridge registered!\n");

    return &s_verilator_wrap_plugin;
}
