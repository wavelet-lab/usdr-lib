// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "pcie_uram_main.h"

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
#include <dirent.h>

#include "../device/device.h"
#include "../device/device_cores.h"
#include "../device/device_bus.h"
#include "../device/device_names.h"
#include "../device/device_vfs.h"

#include "pcie_uram_driver_if.h"

#include "../ipblks/si2c.h"
#include "../ipblks/spiext.h"
#include "../ipblks/streams/sfe_tx_4.h"

// ABI version should be synced with the driver
// Since version 3:  check SPI/I2C core compatibility
#define USDR_DRIVER_ABI_VERSION 3

struct stream_cache_data {
    unsigned flags;
    unsigned bufavail;
    unsigned bno;
    unsigned bmsk;

    unsigned cfg_bufsize;
    unsigned cfg_totbuf;

    void** mmaped_area;
    size_t mmaped_length;

    uint64_t oob_cache[96];
    unsigned oob_size;
    unsigned oob_idx;

    off_t vma_pgoff;
    size_t vma_length;

    uint64_t seq;

    //
    void* param;
    soft_tx_commit_fn_t soft_tx_fn;
};

struct pcie_uram_dev
{
    struct lowlevel_dev ll;
    uint32_t *mmaped_io;

    int fd;

    char name[128];
    char devid_str[36];
    device_id_t devid;
    device_bus_t db;

    struct stream_cache_data scache[DBMAX_SRX + DBMAX_STX];
};
typedef struct pcie_uram_dev pcie_uram_dev_t;


// Common PCIE operations
static
int pcie_uram_generic_get(lldev_t dev, int generic_op, const char** pout)
{
    pcie_uram_dev_t* d = (pcie_uram_dev_t*)dev;

    switch (generic_op) {
    case LLGO_DEVICE_NAME: *pout = d->name; return 0;
    case LLGO_DEVICE_UUID: *pout = (const char*)d->devid.d; return 0;
    }

    return -EINVAL;
}

// Device operations
static
int pcie_reg_write32_ioctl(pcie_uram_dev_t* dev, unsigned dwoff, unsigned data)
{
    //ssize_t res = pwrite(dev->fd, &data, sizeof(data), dwoff);
    struct pcie_driver_hwreg32 rop = { dwoff, data };
    int res = ioctl(dev->fd, PCIE_DRIVER_HWREG_WR32, &rop);
    if (res == -1) {
        int err = -errno;
        USDR_LOG("PCIE", USDR_LOG_ERROR,
                 "pcie:%s unable to write register %d, error %d\n", dev->name, dwoff, err);
        return err;
    }

    USDR_LOG("PCIE", USDR_LOG_TRACE, "Write[%d] <= %08x\n",
             dwoff, data);
    return 0;
}


static
int pcie_reg_read32_ioctl(pcie_uram_dev_t* dev, unsigned dwoff, unsigned* data)
{
    //ssize_t res = pread(dev->fd, data, sizeof(*data), dwoff);
    struct pcie_driver_hwreg32 rop = { dwoff, ~0u };
    int res = ioctl(dev->fd, PCIE_DRIVER_HWREG_RD32, &rop);
    if (res == -1) {
        int err = -errno;
        USDR_LOG("PCIE", USDR_LOG_ERROR,
                 "pcie:%s unable to read register %d, error %d\n", dev->name, dwoff, err);
        return err;
    }

    USDR_LOG("PCIE", USDR_LOG_TRACE, "Read [%d] => %08x\n",
             dwoff, rop.value);
    *data = rop.value;
    return 0;
}

static
int pcie_reg_op_ioctl(struct pcie_uram_dev* d, unsigned ls_op_addr,
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
                res = pcie_reg_write32_ioctl(d, d->db.idxreg_base[k],
                                       ls_op_addr - d->db.idxreg_virt_base[k] + i);
                if (res)
                    return res;

                if (i < memoutsz / 4) {
                    res = pcie_reg_write32_ioctl(d, d->db.idxreg_base[k] + 1, outa[i]);
                    if (res)
                        return res;
                }

                if (i < meminsz / 4) {
                    res = pcie_reg_read32_ioctl(d, d->db.idxreg_base[k] + 1, &ina[i]);
                    if (res)
                        return res;
                }
            }

            return 0;
        }
    }

    // Normal operation
    for (i = 0; i < memoutsz / 4; i++) {
        res = pcie_reg_write32_ioctl(d, ls_op_addr + i, outa[i]);
        if (res)
            return res;
    }
    for (i = 0; i < meminsz / 4; i++) {
        res = pcie_reg_read32_ioctl(d, ls_op_addr + i, &ina[i]);
        if (res)
            return res;
    }
    return 0;
}

static
int pcie_reg_op_iommap(struct pcie_uram_dev* d, unsigned ls_op_addr,
                       uint32_t* ina, size_t meminsz, const uint32_t* outa, size_t memoutsz)
{
    unsigned i;
    if ((meminsz % 4) || (memoutsz % 4))
        return -EINVAL;

    for (unsigned k = 0; k < d->db.idx_regsps; k++) {
        if (ls_op_addr >= d->db.idxreg_virt_base[k]) {
            // Indexed register operation
            unsigned amax = ((memoutsz > meminsz) ? memoutsz : meminsz) / 4;
            for (i = 0; i < amax; i++) {
                //Write address
                d->mmaped_io[d->db.idxreg_base[k]] = htobe32(ls_op_addr - d->db.idxreg_virt_base[k] + i);
                if (i < memoutsz / 4) {
                    d->mmaped_io[d->db.idxreg_base[k] + 1] = htobe32(outa[i]);\
                    USDR_LOG("PCIE", USDR_LOG_TRACE, "Write[%d+%d -> %d] <= %08x\n",
                             d->db.idxreg_virt_base[k],
                             ls_op_addr - d->db.idxreg_virt_base[k] + i,
                             d->db.idxreg_base[k] + 1, outa[i]);
                }
                if (i < meminsz / 4) {
                    ina[i] = be32toh(d->mmaped_io[d->db.idxreg_base[k] + 1]);
                    USDR_LOG("PCIE", USDR_LOG_TRACE, "Read [%d+%d -> %d] => %08x\n",
                             d->db.idxreg_virt_base[k],
                             ls_op_addr - d->db.idxreg_virt_base[k] + i,
                             d->db.idxreg_base[k] + 1, ina[i]);
                }
            }
            return 0;
        }
    }

    unsigned outdwsz = memoutsz / 4;
    unsigned indwsz = meminsz / 4;

    // Normal operation
    for (i = 0; i < outdwsz; i++) {
        if (((ls_op_addr + i) % 2 == 0) && (outdwsz - i > 1)) {
            uint64_t outb = htobe32(outa[i]) | (((uint64_t)htobe32(outa[i + 1])) << 32);
            *((uint64_t*)&d->mmaped_io[ls_op_addr + i]) = outb;

            USDR_LOG("PCIE", USDR_LOG_TRACE, "Write64[%d] <= %08x%08x\n",
                     ls_op_addr + i, outa[i], outa[i + 1]);
            i++;
        } else {
            d->mmaped_io[ls_op_addr + i] = htobe32(outa[i]);
            USDR_LOG("PCIE", USDR_LOG_TRACE, "Write32[%d] <= %08x\n",
                     ls_op_addr + i, outa[i]);
        }
    }
    for (i = 0; i < indwsz; i++) {
        ina[i] = be32toh(d->mmaped_io[ls_op_addr + i]);
        USDR_LOG("PCIE", USDR_LOG_TRACE, "Read [%d] => %08x\n",
                 ls_op_addr + i, ina[i]);
    }
    return 0;
}


#if 0
static
int pcie_i2c_read(pcie_uram_dev_t* dev, unsigned reg_base, unsigned control, unsigned* out)
{
    int res = pcie_reg_write32(dev, reg_base, control);
    if (res)
        return res;

    unsigned i, stat;
    for (i = 0; i < 100; i++) {
        usleep(1000);
        res = pcie_reg_read32(dev, M2PCI_REG_STAT_CTRL, &stat);
        if (res)
            return res;

        if (stat & (1u << STATUS_READY_I2C))
            break;
    }
    if (i == 100) {
        USDR_LOG("PCIE", USDR_LOG_ERROR, "i2c: data not ready, status = %08x!\n", stat);
        return -EIO;
    }

    return pcie_reg_read32(dev, reg_base, out);
}

static
int pcie_spi_transact(pcie_uram_dev_t* dev, unsigned bus, unsigned in, unsigned *pout)
{
    unsigned i, out;
    int res;
    res = pcie_reg_write32(dev, dev->db.spi_base[bus], in);
    if (res)
        return res;

    for (i = 0; i < 100; i++) {
        usleep(1000);
        res = pcie_reg_read32(dev, M2PCI_REG_STAT_CTRL, &out);
        if (res)
            return res;

        if (out & (1u << (STATUS_READY_SPI0 + (bus & 1))))
            break;
    }
    if (i == 100) {
        USDR_LOG("PCIE", USDR_LOG_ERROR, "spi: data not ready, status = %08x!\n", out);
        return -EIO;
    }

    res = pcie_reg_read32(dev, dev->db.spi_base[bus], &out);
    if (res)
        return res;

    *pout = out;
    return 0;
}
#endif

#define MAX_DUMP_BUFFER 256
static char* _dump_buffer(size_t cnt, const void* pin)
{
    static char lbuffer[MAX_DUMP_BUFFER * 2 + 1];
    unsigned i;

    for (i = 0; i < (cnt < MAX_DUMP_BUFFER ? cnt : MAX_DUMP_BUFFER); i++) {
        uint8_t c = ((const uint8_t*)pin)[i];
        uint8_t ph = c >> 4;
        uint8_t pl = c & 0xf;
        lbuffer[2 * i + 0] = (ph < 9) ? '0' + ph : 'a' + ph - 10;
        lbuffer[2 * i + 1] = (pl < 9) ? '0' + pl : 'a' + pl - 10;
    }
    lbuffer[i] = 0;
    return lbuffer;
}

static
int pcie_uram_ls_op(lldev_t dev, subdev_t subdev,
                    unsigned ls_op, lsopaddr_t ls_op_addr,
                    size_t meminsz, void* pin,
                    size_t memoutsz, const void* pout)
{
    int res;
    struct pcie_uram_dev* d = (struct pcie_uram_dev*)dev;
    device_bus_t* pdb = &d->db;

    switch (ls_op) {
    case USDR_LSOP_HWREG: {
        uint32_t* ina = (uint32_t*)pin;
        const uint32_t* outa = (const uint32_t*)pout;

        return (d->mmaped_io) ?
                    pcie_reg_op_iommap(d, ls_op_addr, ina, meminsz, outa, memoutsz) :
                    pcie_reg_op_ioctl(d, ls_op_addr, ina, meminsz, outa, memoutsz);
    }
    case USDR_LSOP_SPI: {
        uint32_t spi_param;
        if (SPIEXT_LSOP_GET_BUS(ls_op_addr) >= d->db.spi_count)
            return -EINVAL;

        // TODO: check if we need this sanity check, since evertyhing is checked in PCI
        // driver
        if (pdb->spi_core[SPIEXT_LSOP_GET_BUS(ls_op_addr)] == SPI_CORE_32W) {
            if (((meminsz != 4) && (meminsz != 0)) || (memoutsz != 4))
                return -EINVAL;

            spi_param = *(const uint32_t*)pout;
        } else if (pdb->spi_core[SPIEXT_LSOP_GET_BUS(ls_op_addr)] == SPI_CORE_CFGW_CS8) {
            spi_param = spiext_make_data_reg(memoutsz, pout);
        } else {
            return -EINVAL;
        }

        struct pcie_driver_spi32 iospi = { ls_op_addr, spi_param };
        res = ioctl(d->fd, PCIE_DRIVER_SPI32_TRANSACT, &iospi);
        if (res)
            return -errno;

        USDR_LOG("PCIE", USDR_LOG_NOTE, "SPI%d: DW=%08x => %08x\n", SPIEXT_LSOP_GET_BUS(ls_op_addr), *(const uint32_t*)pout, iospi.dw_io);

        if (meminsz) {
            if (pdb->spi_core[SPIEXT_LSOP_GET_BUS(ls_op_addr)] == SPI_CORE_32W) {
                *(uint32_t*)pin = iospi.dw_io;
            } else {
                spiext_parse_data_reg(iospi.dw_io, meminsz, pin);
            }
        }
        return 0;
    }
    case USDR_LSOP_I2C_DEV: {
        struct pcie_driver_si2c ioi2c;
        ioi2c.addr = ls_op_addr;
        ioi2c.wcnt = memoutsz;
        ioi2c.rcnt = meminsz;
        ioi2c.rdb_p = NULL;
        ioi2c.wrb_p = NULL;

        if (memoutsz > sizeof(ioi2c.wrb)) {
            ioi2c.wrb_p = (void*)pout;
        } else {
            memcpy(ioi2c.wrb, pout, memoutsz);
        }

        if (meminsz > sizeof(ioi2c.rdb)) {
            ioi2c.rdb_p = pin;
        }

        usleep(1000);

        USDR_LOG("PCIE", USDR_LOG_NOTE, "I2C%d.%d.%d: W=%d R=%d OUT=%s\n",
                 LSOP_I2C_INSTANCE(ls_op_addr), LSOP_I2C_BUSNO(ls_op_addr),
                 LSOP_I2C_ADDR(ls_op_addr), ioi2c.wcnt, ioi2c.rcnt, _dump_buffer(memoutsz, pout));

        res = ioctl(d->fd, PCIE_DRIVER_SI2C_TRANSACT, &ioi2c);
        if (res)
            return -errno;

        if (meminsz <= sizeof(ioi2c.rdb)) {
            memcpy(pin, ioi2c.rdb, meminsz);
        }
        USDR_LOG("PCIE", USDR_LOG_NOTE, "I2C%d.%d.%d:         => %s\n",
                 LSOP_I2C_INSTANCE(ls_op_addr), LSOP_I2C_BUSNO(ls_op_addr),
                 LSOP_I2C_ADDR(ls_op_addr), _dump_buffer(meminsz, pin));

        return 0;
    }
    case USDR_LSOP_DRP: {
        return device_bus_drp_generic_op(dev, subdev, &d->db, ls_op_addr, meminsz, pin, memoutsz, pout);
    }
    case USDR_LSOP_GPI: {
        return device_bus_gpi_generic_op(dev, subdev, &d->db, ls_op_addr, meminsz, pin, memoutsz, pout);
    }
    case USDR_LSOP_GPO: {
        return device_bus_gpo_generic_op(dev, subdev, &d->db, ls_op_addr, meminsz, pin, memoutsz, pout);
    }
    }
    return -EOPNOTSUPP;
}

static
int pcie_uram_stream_initialize(lldev_t dev, subdev_t subdev,
                                lowlevel_stream_params_t* params,
                                stream_t* channel)
{
    struct pcie_uram_dev* d = (struct pcie_uram_dev*)dev;
    struct pcie_driver_sdma_conf pdsc;
    struct stream_cache_data* sc;
    unsigned i;
    int res;

    pdsc.type = STREAM_MMAPED;
    pdsc.sno = params->streamno;

    pdsc.dma_bufs = params->buffer_count;
    pdsc.dma_buf_sz = params->block_size;

    res = ioctl(d->fd, PCIE_DRIVER_DMA_CONF, &pdsc);
    if (res) {
        res = -errno;
        USDR_LOG("PCIE", USDR_LOG_ERROR, "Unable to initialize driver DMA configuration, error %d\n", res);
        return res;
    }
    if (pdsc.sno >= SIZEOF_ARRAY(d->scache)) {
        USDR_LOG("PCIE", USDR_LOG_ERROR, "ioctl(PCIE_DRIVER_DMA_CONF) returned incorrect stream index! idx=%d\n", pdsc.sno);
        return -EINVAL;
    }

    *channel = pdsc.sno;
    sc = &d->scache[pdsc.sno];
    sc->bufavail = 0;
    sc->bno = 0;
    sc->oob_size = 0;
    sc->cfg_bufsize = pdsc.dma_buf_sz;
    sc->cfg_totbuf = pdsc.dma_bufs;
    sc->vma_pgoff = pdsc.out_vma_off;
    sc->vma_length = pdsc.out_vma_length;
    sc->seq = 0;
    if (pdsc.type == STREAM_MMAPED) {
        sc->mmaped_area = malloc(sizeof(void*) * pdsc.dma_bufs);
        if (!sc->mmaped_area) {
            res = -ENOMEM;
            goto fail_alloc;
        }

        sc->mmaped_length = sc->vma_length / pdsc.dma_bufs;
        for (i = 0; i < pdsc.dma_bufs; i++) {
            sc->mmaped_area[i] = mmap(NULL, sc->mmaped_length, PROT_READ | PROT_WRITE,
                                      MAP_SHARED, d->fd, pdsc.out_vma_off + i * sc->vma_length / pdsc.dma_bufs);
            if (sc->mmaped_area[i] == MAP_FAILED) {
                res = -errno;
                goto fail_mmap;
            }
        }

    } else {
        sc->mmaped_area = NULL;
    }
    sc->param = params->param;
    sc->soft_tx_fn = params->soft_tx_commit;

    //d->channels[pdsc.sno] = params->channels;
    //d->bit_per_all_sym[pdsc.sno] = params->bits_per_sym;
    params->underlying_fd = d->fd;
    params->out_mtu_size = pdsc.dma_buf_sz;
    USDR_LOG("PCIE", USDR_LOG_INFO, "Configured stream%d: %d X %d (vma_off=%08lx vma_len=%08lx)\n",
             pdsc.sno, pdsc.dma_buf_sz, pdsc.dma_bufs, pdsc.out_vma_off, pdsc.out_vma_length);
    return 0;

fail_mmap:
    for (; i != 0; i--) {
        munmap(sc->mmaped_area[i - 1], sc->mmaped_length);
    }
    free(sc->mmaped_area);
    sc->mmaped_area = NULL;
fail_alloc:
    ioctl(d->fd, PCIE_DRIVER_DMA_UNCONF, pdsc.sno);
    return res;
}

static
int pcie_uram_stream_deinitialize(lldev_t dev, subdev_t subdev, stream_t channel)
{
    unsigned i;
    struct stream_cache_data* sc;
    struct pcie_uram_dev* d = (struct pcie_uram_dev*)dev;
    int res;

    if (channel > DBMAX_SRX + DBMAX_STX)
        return -EINVAL;
    sc = &d->scache[channel];
    if (sc->cfg_totbuf == 0)
        return -EINVAL;

    if (sc->mmaped_area) {
        for (i = 0; i < sc->cfg_totbuf; i++) {
            munmap(sc->mmaped_area[i], sc->mmaped_length);
        }
        free(sc->mmaped_area);
        sc->mmaped_area = 0;
    }

    sc->cfg_totbuf = 0;
    sc->cfg_bufsize = 0;
    sc->vma_length = 0;

    res = ioctl(d->fd, PCIE_DRIVER_DMA_UNCONF, channel);
    if (res)
        return -errno;

    return 0;
}


static
int pcie_uram_dma_wait_or_alloc(struct pcie_uram_dev* d, bool rx, stream_t channel, void** buffer,
                                void* oob_ptr, unsigned *oob_size, unsigned timeout)
{
    int res;
    unsigned long ctl_param = ((timeout) << 8) | channel;
    struct stream_cache_data* sc = &d->scache[channel];
    void* addr;

    if (channel > DBMAX_SRX + DBMAX_STX)
        return -EINVAL;
    if (sc->cfg_totbuf == 0)
        return -EINVAL;

    if (sc->bufavail == 0) {
        if (oob_ptr == NULL) {
            res = ioctl(d->fd, rx ? PCIE_DRIVER_DMA_WAIT : PCIE_DRIVER_DMA_ALLOC, ctl_param);
            sc->oob_size = 0;
            sc->oob_idx = 0;
        } else {
            struct pcie_driver_woa_oob data;
            data.streamnoto = ctl_param;
            data.oobdata = sc->oob_cache;
            data.ooblength = sizeof(sc->oob_cache);
            res = ioctl(d->fd, rx ? PCIE_DRIVER_DMA_WAIT_OOB : PCIE_DRIVER_DMA_ALLOC_OOB, &data);
            sc->oob_size = data.ooblength;
            sc->oob_idx = 0;

            if (res * 16 != data.ooblength) {
                USDR_LOG("PCIE", USDR_LOG_CRITICAL_WARNING, " RES %d != %d OOBLEN\n", res, sc->oob_size);
            }
        }
        if (res < 0) {
            res = -errno;
            if (res != -ETIMEDOUT) {
                USDR_LOG("PCIE", USDR_LOG_CRITICAL_WARNING, "STR[%d]: PCIe %s dma buffer alloc error: %d!\n",
                         channel, rx ? "recv" : "send", res);
            } else if (rx) {
                unsigned stat[4];
                // TODO: Remove hardcoded address to upper layer
                pcie_reg_op_iommap(d, 4, &stat[0], 12, NULL, 0);
                USDR_LOG("PCIE", USDR_LOG_NOTE, "STR[%d]: PCIe recv dma buffer alloc timed out stat=%08x:%08x:%08x %08x!\n",
                         channel, stat[0], stat[1], stat[2], stat[3]);

                uint32_t* oob32 = (uint32_t*)oob_ptr;
                oob32[0] = stat[0];
                oob32[1] = stat[1];
                oob32[2] = stat[2];
            } else {
                unsigned stat[4];
                // TODO: Remove hardcoded address to upper layer
                pcie_reg_op_iommap(d, 28, &stat[0], 16, NULL, 0);
                USDR_LOG("PCIE", USDR_LOG_NOTE, "STR[%d]: PCIe send dma buffer alloc timed out stat=%08x:%08x:%08x %08x!\n",
                         channel, stat[0], stat[1], stat[2], stat[3]);
            }
            return res;
            //goto restart;
        }

        sc->bufavail = res;
        USDR_LOG("PCIE", (res > 1) ? USDR_LOG_NOTE : USDR_LOG_DEBUG, "STR[%d]: Alloced %d buffs, BNO=%d (%016lx) seq=%16ld OOB_sz=%d\n",
                 channel, res, sc->bno, (oob_ptr) ? (*(uint64_t*)sc->oob_cache) : 0, sc->seq, sc->oob_size);
    }

    addr = sc->mmaped_area[sc->bno];

    sc->bno = (sc->bno + 1) & (sc->cfg_totbuf - 1);
    sc->bufavail--;
    sc->seq++;

    *buffer = addr;
    if (oob_ptr && oob_size && *oob_size >= 2 * sizeof(uint64_t)) {
        uint64_t* oob64 = (uint64_t*)oob_ptr;
        if (sc->oob_size > sc->oob_idx * 16) {
            oob64[0] = sc->oob_cache[2 * sc->oob_idx + 0];
            oob64[1] = sc->oob_cache[2 * sc->oob_idx + 1];
            *oob_size = 2 * sizeof(uint64_t);
            sc->oob_idx++;
        } else {
            USDR_LOG("PCIE", USDR_LOG_CRITICAL_WARNING, "No OOB data available for %d idx (%d size)!\n",
                     sc->oob_idx, sc->oob_size);

            sc->oob_idx++;
            oob64[0] = oob64[1] = 0;
            *oob_size = 0;
        }
    }

    return sc->bufavail;
}


static
int pcie_uram_recv_dma_wait(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer,
                            void* oob_ptr, unsigned *oob_size, unsigned timeout)
{
    return pcie_uram_dma_wait_or_alloc((struct pcie_uram_dev*)dev,
                                       true, channel, buffer, oob_ptr, oob_size, timeout);
}

static
int pcie_uram_recv_dma_release(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer)
{
    int res;
    struct pcie_uram_dev* d = (struct pcie_uram_dev*)dev;

    if (channel > DBMAX_SRX + DBMAX_STX)
        return -EINVAL;

    // TODO: don't call ioctl() on mmaped cache coherent interface
    res = ioctl(d->fd, PCIE_DRIVER_DMA_RELEASE, channel);
    if (res) {
        res = -errno;
        if (res != -EAGAIN) {
            USDR_LOG("PCIE", USDR_LOG_CRITICAL_WARNING, "PCIe recv dma buffer release error: %d!\n", res);
        }
        return res;
    }

    return 0;
}

static
int pcie_uram_send_dma_get(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer,
                           void* oob_ptr, unsigned *oob_size, unsigned timeout)
{
    return pcie_uram_dma_wait_or_alloc((struct pcie_uram_dev*)dev,
                                       false, channel, buffer, oob_ptr, oob_size, timeout);
}

static
int pcie_uram_send_dma_commit(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer, unsigned sz, const void* oob_ptr, unsigned oob_size)
{
    struct pcie_uram_dev* d = (struct pcie_uram_dev*)dev;
    struct stream_cache_data* sc = &d->scache[channel];

    if (channel > DBMAX_SRX + DBMAX_STX)
        return -EINVAL;

    if (sc->cfg_bufsize < sz) {
        USDR_LOG("PCIE", USDR_LOG_CRITICAL_WARNING, "Stream was configured with %d DMA buffer but tried to write %d!\n",
                 d->scache[channel].cfg_bufsize, sz);
        return -EINVAL;
    }

    // TODO: Call to flush DMA buffers on non-coherent systems
    return sc->soft_tx_fn(sc->param, sz, oob_ptr, oob_size);
}

static
int pcie_uram_await(lldev_t dev, subdev_t subdev, unsigned await_id, unsigned op, void** await_inout_aux_data, unsigned timeout)
{
    return -ENOTSUP;
}

static
int pcie_recv_buf(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer, unsigned *expected_sz, void* oob_ptr, unsigned *oob_size, unsigned timeout)
{
    return -ENOTSUP;
}

static
int pcie_send_buf(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer, unsigned sz, const void* oob_ptr, unsigned oob_size, unsigned timeout)
{
    return -ENOTSUP;
}


static
int pcie_uram_destroy(lldev_t dev)
{
    struct pcie_uram_dev* d = (struct pcie_uram_dev*)dev;

    // Deinit DMA
    for (unsigned sno = 0; sno < DBMAX_SRX + DBMAX_STX; sno++) {
        pcie_uram_stream_deinitialize(dev, 0, sno);
    }

    // Destroy undelying dev
    if (dev->pdev) {
        dev->pdev->destroy(dev->pdev);
    }

    //
    if (d->mmaped_io) {
        munmap(d->mmaped_io, 4096);
        d->mmaped_io = NULL;
    }

    //Destroy transport
    close(d->fd);
    d->fd = -1;

    USDR_LOG("PCIE", USDR_LOG_INFO, "Device %s destroyed!\n", d->name);

    free(d);
    return 0;
}

// Device operations
static
struct lowlevel_ops s_pcie_uram_ops = {
    pcie_uram_generic_get,
    pcie_uram_ls_op,
    pcie_uram_stream_initialize,
    pcie_uram_stream_deinitialize,
    pcie_uram_recv_dma_wait,
    pcie_uram_recv_dma_release,
    pcie_uram_send_dma_get,
    pcie_uram_send_dma_commit,
    pcie_recv_buf,
    pcie_send_buf,
    pcie_uram_await,
    pcie_uram_destroy,
};

// Factory functions
static
const char* pcie_uram_plugin_info_str(unsigned iparam) {
    switch (iparam) {
    case LLPI_NAME_STR: return "pcie";
    case LLPI_DESCRIPTION_STR: return "PCIe URAM linux driver";
    }
    return NULL;
}

struct pci_filtering_params {
    const char* dev;
};

static int pcie_filtering_params_parse(unsigned pcount, const char** filterparams,
                                       const char** filtervals, struct pci_filtering_params* pp)
{
    pp->dev = NULL;

    for (unsigned k = 0; k < pcount; k++) {
        const char* val = filtervals[k];
        if (strcmp(filterparams[k], "bus") == 0) {
            unsigned j;

            // Filter by BUS
            if (strncmp(val, "/dev/", 5) == 0) {
                j = 0;
            } else if (strncmp(val, "pci", 3) == 0) {
                j = 3;
            } else {
                // Non-compatible bus
                USDR_LOG("USBX", USDR_LOG_TRACE, "`%s` ignored by PCI driver\n", val);
                return -ENODEV;
            }

            for (;;j++) {
                char v = val[j];
                if (v == 0) {
                    break;
                } else if (v == '/') {
                    pp->dev = &val[j + 1];
                }
            }

        } else if (strcmp(filterparams[k], "dev") == 0 || strcmp(filterparams[k], "device") == 0) {
            pp->dev = filtervals[k];
        }
    }

    if ((pcount == 1) && (filtervals[0] == 0) && (strncmp(filterparams[0], "/dev/usdr", 9) == 0)) {
        pp->dev = filterparams[0] + 5;
    }

    return 0;
}

static
int pcie_uram_plugin_discovery(unsigned pcount, const char** filterparams, const char** filtervals, unsigned maxbuf, char* outarray)
{
    struct pci_filtering_params pf;
    int res = pcie_filtering_params_parse(pcount, filterparams, filtervals, &pf);
    if (res) {
        return res;
    }

    DIR *d;
    struct dirent *dir;
    unsigned i, off;

    d = opendir("/sys/class/usdr/");
    if (!d) {
        USDR_LOG("PCIE", USDR_LOG_WARNING, "uSDR PCIe kernel module isn't loaded!\n");
        return -ENODEV;
    }

    res = -ENODEV;
    for (i = 0, off = 0; ; ) {
#ifdef USE_READDIR_R
        struct dirent local_dir;
        int rr = readdir_r(d, &local_dir, &dir);
        if (rr || !dir)
            break;
#else
        dir = readdir(d);
        if (!dir)
            break;
#endif

        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
            continue;

        if (pf.dev != NULL && strcmp(dir->d_name, pf.dev) != 0)
            continue;

        int cap = maxbuf - off;
        int l = snprintf(outarray + off, cap, "bus=pci,device=%s\n", dir->d_name);
        if (l < 0 || l > cap) {
            outarray[off] = 0;
            res = i;
            break;
        }

        off += l;
        i++;
        res = i;
    }

    closedir(d);
    return res;
}

static
int pcie_uram_plugin_create(unsigned pcount, const char** devparam, const char** devval, lldev_t* odev,
                            unsigned vidpid, void* webops, uintptr_t param)
{
    struct pci_filtering_params pf;
    int err = pcie_filtering_params_parse(pcount, devparam, devval, &pf);
    if (err) {
        return err;
    }

    if (pf.dev == NULL) {
        pf.dev = "usdr0";
    }

    // TODO class
    int fd;
    bool mmapedio = true;
    unsigned iospacesz = 4096;
    char devname[128];
    snprintf(devname, sizeof(devname), "/dev/%s", pf.dev);

    for (unsigned k = 0; k < pcount; k++) {
        if (strcmp(devparam[k], "mmapio") == 0) {
            mmapedio = (devval[k][0] == '1' || devval[k][0] == 'o') ? true : false;

            USDR_LOG("PCIE", USDR_LOG_INFO, "mmaped IO is %s\n",
                     mmapedio ? "enabled" : "disabled");
        }
    }

#if 0
    for (i = 0; i < pcount; i++) {
        if (strcmp(LL_DEVICE_PARAM, devparam[i]) == 0) {
            devname = devval[i];
        } else {
            USDR_LOG("PCIE", USDR_LOG_NOTE,
                     "PCIe device create unknown parameter `%s=%s'\n",
                     devparam[i], devval[i]);
        }
    }
#endif


    fd = open(devname, O_RDWR);
    if (fd == -1) {
        err = errno;

        USDR_LOG("PCIE", USDR_LOG_CRITICAL_WARNING,
                 "Unable to open device %s, error: %d\n",
                 devname, err);
        return -err;
    }

    pcie_uram_dev_t* dev;
    dev = (pcie_uram_dev_t*)malloc(sizeof(pcie_uram_dev_t));
    if (dev == NULL) {
        err = ENOMEM; goto close_fd;
    }
    memset(dev, 0, sizeof(*dev));

    dev->ll.ops = &s_pcie_uram_ops;
    dev->fd = fd;

    snprintf(dev->name, sizeof(dev->name), "%s", devname);

    // Get UUID
    device_id_t did;
    err = ioctl(fd, PCIE_DRIVER_GET_UUID, &did);
    if (err) {
        err = -errno;
        USDR_LOG("PCIE", USDR_LOG_ERROR,
                 "Unable to get device uuid, error %d\n", err);
        goto remove_dev;
    }

    err = ioctl(fd, PCIE_DRIVER_CLAIM_VERSION, USDR_DRIVER_ABI_VERSION);
    if (err) {
        err = -errno;
        USDR_LOG("PCIE", USDR_LOG_ERROR,
                 "ABI verification failed %d, you need to update the driver or host libraries!\n", err);
        goto remove_dev;
    }

    dev->devid = did;
    strncpy(dev->devid_str, usdr_device_id_to_str(did), sizeof(dev->devid_str) - 1);

    err = usdr_device_create(&dev->ll, did);
    if (err) {
        USDR_LOG("PCIE", USDR_LOG_ERROR,
                 "Unable to find device spcec for %s, uuid %s! Update software!\n",
                 dev->name,
                 usdr_device_id_to_str(did));

        goto remove_dev;
    }

    err = device_bus_init(dev->ll.pdev, &dev->db);
    if (err) {
        USDR_LOG("PCIE", USDR_LOG_ERROR,
                 "Unable to initialize bus parameters for the device %s!\n", dev->name);

        goto remove_dev;
    }

    // Device driver initialization
    uint64_t tmp;
    struct pcie_driver_devlayout dl;
    memset(&dl, 0, sizeof(dl));

    dl.spi_cnt = dev->db.spi_count;
    dl.i2c_cnt = dev->db.i2c_count;
    dl.idx_regsp_cnt = dev->db.idx_regsps;
    dl.streams_count = dev->db.srx_count + dev->db.stx_count;
    if ((dl.spi_cnt > MAX_SPI_COUNT) ||
            (dl.i2c_cnt > MAX_I2C_COUNT) ||
            (dl.idx_regsp_cnt > MAX_INDEXED_SPACES) ||
            (dl.streams_count > MAX_STREAM_COUNT)) {
        err = -ENOSPC;
        goto remove_dev;
    }
    if (dev->db.bucket_count != 1) {
        USDR_LOG("PCIE", USDR_LOG_ERROR, "Broken device description: no bucket!\n");
        err = -ENOSPC;
        goto remove_dev;
    }

    memcpy(dl.idx_regsp_base, dev->db.idxreg_base, sizeof(dl.idx_regsp_base));
    memcpy(dl.idx_regsp_vbase, dev->db.idxreg_virt_base, sizeof(dl.idx_regsp_base));
    memcpy(dl.spi_base, dev->db.spi_base, sizeof(dl.spi_base));
    memcpy(dl.i2c_base, dev->db.i2c_base, sizeof(dl.i2c_base));
    memcpy(dl.spi_core, dev->db.spi_core, sizeof(dl.spi_core));
    memcpy(dl.i2c_core, dev->db.i2c_core, sizeof(dl.i2c_core));

    memcpy(dl.stream_cnf_base, dev->db.srx_base, dev->db.srx_count * sizeof(dl.stream_cnf_base[0]));
    memcpy(dl.stream_cnf_base + dev->db.srx_count, dev->db.stx_base, dev->db.stx_count * sizeof(dl.stream_cnf_base[0]));
    memcpy(dl.stream_cfg_base, dev->db.srx_cfg_base, dev->db.srx_count * sizeof(dl.stream_cfg_base[0]));
    memcpy(dl.stream_cfg_base + dev->db.srx_count, dev->db.stx_cfg_base, dev->db.stx_count * sizeof(dl.stream_cfg_base[0]));
    memcpy(dl.stream_core, dev->db.srx_core, dev->db.srx_count * sizeof(dl.stream_core[0]));
    memcpy(dl.stream_core + dev->db.srx_count, dev->db.stx_core, dev->db.stx_count * sizeof(dl.stream_core[0]));

    err = usdr_device_vfs_obj_val_get_u64(dev->ll.pdev, DNLL_IRQ_COUNT, &tmp);
    if (err)
        goto remove_dev;

    dl.interrupt_count = tmp;

    err = usdr_device_vfs_obj_val_get_u64(dev->ll.pdev, DNLLFP_BASE(DNP_IRQ, "0"), &tmp);
    if (err)
        goto remove_dev;

    dl.interrupt_base = tmp;

    dl.poll_event_rd = dev->db.poll_event_rd;
    dl.poll_event_wr = dev->db.poll_event_wr;

    struct device_params {
        const char* path;
        unsigned *store;
        unsigned count;
    } bii[] = {
    { DNLLFP_IRQ(DN_BUS_SPI, "%d"), dl.spi_int_number, dl.spi_cnt },
    { DNLLFP_IRQ(DN_BUS_I2C, "%d"), dl.i2c_int_number, dl.i2c_cnt },
    { DNLLFP_IRQ(DN_SRX, "%d"), dl.stream_int_number, dev->db.srx_count },
    { DNLLFP_IRQ(DN_STX, "%d"), dl.stream_int_number + dev->db.srx_count, dev->db.stx_count },
    { DNLLFP_NAME(DN_SRX, "%d", DNP_DMACAP), dl.stream_cap, dev->db.srx_count },
    { DNLLFP_NAME(DN_STX, "%d", DNP_DMACAP), dl.stream_cap + dev->db.srx_count, dev->db.stx_count },
    };
    char buffer[32];

    for (unsigned i = 0; i < SIZEOF_ARRAY(bii); i++) {
        for (unsigned j = 0; j < bii[i].count; j++) {
            snprintf(buffer, sizeof(buffer), bii[i].path, j);
            err = usdr_device_vfs_obj_val_get_u64(dev->ll.pdev, buffer, &tmp);
            if (err) {
                goto remove_dev;
            }

            bii[i].store[j] = (unsigned)tmp;
        }
    }

    dl.bucket_base = dev->db.bucket_base[0];
    dl.bucket_core = dev->db.bucket_core[0];
    dl.bucket_count = dev->db.bucket_count;

    err = err ? err : ioctl(fd, PCIE_DRIVER_SET_DEVLAYOUT, &dl);
    if (err) {
        err = -errno;
        USDR_LOG("PCIE", USDR_LOG_ERROR,
                 "Unable to set device driver layout, error %d\n", err);
        goto remove_dev;
    }

    if (mmapedio) {
        dev->mmaped_io = mmap(NULL, iospacesz, PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, 0);
        if (dev->mmaped_io == MAP_FAILED) {
            USDR_LOG("PCIE", USDR_LOG_CRITICAL_WARNING, "Unable to use MMAPed IO, falling back to ioctl(), error: %d",
                     errno);

            dev->mmaped_io = NULL;
        }
    }

    // Device initialization
    err = err ? err : dev->ll.pdev->initialize(dev->ll.pdev, pcount, devparam, devval);
    if (err) {
        USDR_LOG("PCIE", USDR_LOG_ERROR,
                 "Unable to initialize device, error %d\n", err);
        goto clear_map;
    }

    *odev = &dev->ll;
    return 0;

clear_map:
    if (dev->mmaped_io) {
        munmap(dev->mmaped_io, iospacesz);
    }
remove_dev:
    free(dev);
close_fd:
    close(fd);

    return err;
}

// Factory operations
static const
struct lowlevel_plugin s_pcie_uram_plugin = {
    pcie_uram_plugin_info_str,
    pcie_uram_plugin_discovery,
    pcie_uram_plugin_create,
};


const struct lowlevel_plugin *pcie_uram_register()
{
    return &s_pcie_uram_plugin;
}
