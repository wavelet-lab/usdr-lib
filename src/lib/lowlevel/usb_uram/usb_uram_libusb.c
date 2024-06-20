// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#define _GNU_SOURCE
#include <stdint.h>
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
#include <signal.h>
#include <assert.h>

#include "usb_uram_generic.h"

#include "../device/device.h"
#include "../device/device_bus.h"
#include "../device/device_names.h"
#include "../device/device_vfs.h"

#include "../ipblks/si2c.h"

#include "../libusb_generic.h"
#include "../libusb_vidpid_map.h"

// Write command                     [R/W ... L5:L0|A15...A0]
// Read number of bytes requested

enum {
    DEV_RX_STREAM_NO = 0,
    DEV_TX_STREAM_NO = 1,
};

enum {
    TXSTRM_META_SZ = 16,

    // TODO Get rid of duplication constant, use DMA caps to calculate actual size
    MAX_TX_BUFFER_SZ = 126976, // (4k * 31)
};

enum {
    MAX_INTERRUPTS = 32,
    MSI_USRB_COUNT = 2,
    TO_IRQ_POLL = 250,
};

#define DUMP_MAXBUF 512
#define MIN(x, y)  (((x) < (y)) ? (x) : (y))
#define dump_tochar(x) \
    (((x) <= 9) ? ((x) + '0') : (((x) - 10) + 'a'))
static
char* s_dump_buffer(const void* ibuf, unsigned count)
{
    const char* bbuf = (const char* )ibuf;
    static char binbuf[2 * DUMP_MAXBUF + 1];
    unsigned i;
    for (i = 0; i < MIN(DUMP_MAXBUF, count); i++) {
        binbuf[ 2 * i + 0 ] = dump_tochar(((unsigned char)bbuf[i]) >> 4);
        binbuf[ 2 * i + 1 ] = dump_tochar(bbuf[i] & 0xf);
    }
    binbuf[ 2 * i + 0 ] = 0;
    return binbuf;
}

enum {
    EP_OUT_REGISTER  = LIBUSB_ENDPOINT_OUT | 1,
    EP_OUT_FUSES     = LIBUSB_ENDPOINT_OUT | 2,     //USB configuration fuses / reset bits
    EP_OUT_DEFSTREAM = LIBUSB_ENDPOINT_OUT | 3,

    EP_IN_READBACK     = LIBUSB_ENDPOINT_IN | 1,
    EP_IN_NOTIFICATION = LIBUSB_ENDPOINT_IN | 2,
    EP_IN_DEFSTREAM    = LIBUSB_ENDPOINT_IN | 3,
};

enum {
    OUT_REGOUT_SIZE = 256,
    IN_NTFY_SIZE = 64, //256,
    IN_RB_SIZE = 256,

    MAX_NTFY_REQS = 1,
    MAX_RB_REQS = 1,

    MAX_REQUEST_RB_SIZE = 256,

    // Numbers of REGOUTs piplined
    MAX_REGOUT_REQS = 1,

    // Streams
    IN_STRM_SIZE     = 512,
    MAX_IN_STRM_REQS = 8,
    MAX_OUT_STRM_REQS = 32,

    RX_PKT_TRAILER = 8,
    RX_PKT_TRAILER_EX = 16,

    TX_PKT_HEADER = 16,
};

enum {
    STREAM_MAX_SLOTS = 64,
};

struct stream_params {
    unsigned channels;
    unsigned bits_per_all_chs;
};

struct usb_dev
{
    struct lowlevel_dev lld;
    struct lowlevel_ops ops;

    libusb_generic_dev_t gdev;
    device_bus_t db;

    sem_t interrupts[MAX_INTERRUPTS];
    unsigned spi_int_number[MAX_INTERRUPTS];
    unsigned i2c_int_number[MAX_INTERRUPTS];
    uint32_t rbvalue[MAX_INTERRUPTS];

    bool stop;
    sem_t tr_regout_a;
    sem_t tr_rb_a;
    sem_t rb_valid;

    struct libusb_transfer *transfer_regout[MAX_REGOUT_REQS];
    struct libusb_transfer *transfer_rb[MAX_RB_REQS];
    struct libusb_transfer *transfer_ntfy[MAX_NTFY_REQS];

    // Buffers
    unsigned char buffer_regout_n[OUT_REGOUT_SIZE * MAX_REGOUT_REQS];
    unsigned char buffer_rb_n[IN_RB_SIZE * MAX_RB_REQS];
    unsigned char buffer_ntfy_n[IN_NTFY_SIZE * MAX_NTFY_REQS];

    unsigned buffer_regout_flags[MAX_REGOUT_REQS];

    uint16_t ntfy_seqnum_exp;

    uint64_t stream_info[STREAM_MAX_SLOTS];
    unsigned stream_info_widx;

    struct buffers rx_strms[1];
    struct buffers tx_strms[1];
    struct stream_params tx_strms_params[1];

    bool rx_strms_extnty[1];
    unsigned app_drops[1];
    unsigned rx_buffer_missed[1];

};
typedef struct usb_dev usb_dev_t;

void LIBUSB_CALL libusb_transfer_regout(struct libusb_transfer *transfer);
void LIBUSB_CALL libusb_transfer_rb(struct libusb_transfer *transfer);
void LIBUSB_CALL libusb_transfer_ntfy(struct libusb_transfer *transfer);
void LIBUSB_CALL libusb_transfer_in_strm(struct libusb_transfer *transfer);
void LIBUSB_CALL libusb_transfer_out_strm(struct libusb_transfer *transfer);


///////////////////////////////////////////////////////////////////////////////

static
int usb_async_start(usb_dev_t* dev)
{
    int res;
    unsigned i;
    for (i = 0; i < MAX_INTERRUPTS; i++) {
        res = sem_init(&dev->interrupts[i], 0, 0);
    }

    res = sem_init(&dev->tr_regout_a, 0, MAX_REGOUT_REQS);
    res = sem_init(&dev->tr_rb_a, 0, MAX_RB_REQS);
    res = sem_init(&dev->rb_valid, 0, 0);

    // Prepare transfer queues
    res = libusb_generic_prepare_transfer(&dev->gdev, dev, EP_OUT_REGISTER, LIBUSB_TRANSFER_TYPE_BULK,
                                          dev->transfer_regout,
                                          dev->buffer_regout_n, OUT_REGOUT_SIZE, MAX_REGOUT_REQS,
                                          &libusb_transfer_regout);
    if (res) {
        goto failed_prepare;
    }

    res = libusb_generic_prepare_transfer(&dev->gdev, dev, EP_IN_READBACK, LIBUSB_TRANSFER_TYPE_BULK,
                                          dev->transfer_rb,
                                          dev->buffer_rb_n, IN_RB_SIZE, MAX_RB_REQS,
                                          &libusb_transfer_rb);
    if (res) {
        goto failed_prepare;
    }

    res = libusb_generic_prepare_transfer(&dev->gdev, dev, EP_IN_NOTIFICATION, LIBUSB_TRANSFER_TYPE_INTERRUPT,
                                          dev->transfer_ntfy,
                                          dev->buffer_ntfy_n, IN_NTFY_SIZE, MAX_NTFY_REQS,
                                          &libusb_transfer_ntfy);
    if (res) {
        goto failed_prepare;
    }
    for (unsigned i = 0; i < MAX_NTFY_REQS; i++) {
        dev->transfer_ntfy[i]->length = IN_NTFY_SIZE;
    }

    return libusb_generic_create_thread(&dev->gdev);

failed_prepare:
    return res;
}

static int usb_post_regout(usb_dev_t* dev, uint32_t *regoutbuffer, unsigned count_dw, int timeout)
{
    int res;
    unsigned i;
    unsigned databytes = 0;
    unsigned tot_reqlen_dw = 0;
    unsigned tot_rbs = 0;
    unsigned tot_wrs = 0;

    if (count_dw > OUT_REGOUT_SIZE / 4)
        return -EOVERFLOW;

    //Check sanity -- wait reply?
    for (i = 0; i < count_dw; i++) {
        if (databytes == 0) {
            uint32_t d = regoutbuffer[i];
            unsigned len = ((d >> 16) & 0x3f) + 1;

            if (d & 0x80000000) {
                databytes = 0;
                tot_reqlen_dw += len;
                tot_rbs++;
            } else {
                databytes = len;
                tot_wrs++;
            }
        } else {
            databytes--;
        }
    }
    if (databytes != 0) {
        return -EINVAL;
    }

    USDR_LOG("USBX", USDR_LOG_DEBUG, "%s: REGOUT %d DW [WRS:%d RBS:%d RB_TOT:%d DW]  RAW:%s\n",
             dev->gdev.name, count_dw, tot_wrs, tot_rbs, tot_reqlen_dw,
             s_dump_buffer(regoutbuffer, count_dw * 4));

    res = sem_wait(&dev->tr_regout_a);
    if (res) {
        res = -errno;
        return res;
    }

    // TODO obtain IDX
    unsigned idx = 0;
    struct libusb_transfer *transfer = dev->transfer_regout[idx];
    dev->buffer_regout_flags[idx] = tot_rbs;
    memcpy(&dev->buffer_regout_n[idx * OUT_REGOUT_SIZE], regoutbuffer, count_dw * 4);
    transfer->length = count_dw * 4;
    transfer->timeout = timeout;

    res = libusb_to_errno(libusb_submit_transfer(transfer));
    if (res) {
        USDR_LOG("USBX", USDR_LOG_ERROR, "FAILED to post REGOUT %d (%s)\n", res, strerror(-res));
        return res;
    }

    return 0;
}

static int usb_post_rb(usb_dev_t* dev, uint32_t* buffer, unsigned max_buffer_dw)
{
    int res;
    res = sem_wait(&dev->tr_rb_a);
    if (res) {
        res = -errno;
        return res;
    }

    // TODO obtain IDX
    unsigned idx = 0;
    struct libusb_transfer *transfer = dev->transfer_rb[idx];
    buffer[0] = 0;
    transfer->length = 4*max_buffer_dw - 4;
    transfer->buffer = (uint8_t*)(buffer + 1);

    res = libusb_to_errno(libusb_submit_transfer(transfer));
    if (res) {
        USDR_LOG("USBX", USDR_LOG_ERROR, "FAILED to post READBACK %d\n", res);
        return res;
    }

    return 0;
}

void LIBUSB_CALL libusb_transfer_regout(struct libusb_transfer *transfer)
{
    USDR_LOG("USBX", USDR_LOG_DEBUG, "REGOUT transfer %d\n", transfer->status);

    usb_dev_t* dev = (usb_dev_t* )transfer->user_data;
    if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
        USDR_LOG("USBX", USDR_LOG_ERROR, "FAILED REGOUT transfer %d\n", transfer->status);
        return;
    }

    sem_post(&dev->tr_regout_a);
}

void LIBUSB_CALL libusb_transfer_rb(struct libusb_transfer *transfer)
{
    usb_dev_t* dev = (usb_dev_t* )transfer->user_data;
    unsigned alen = transfer->actual_length;
    unsigned* arefptr = (unsigned*)(transfer->buffer - 4);

    USDR_LOG("USBX", USDR_LOG_DEBUG, "RB transfer %d / %d\n",
             transfer->status, transfer->actual_length);

    if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
        USDR_LOG("USBX", USDR_LOG_ERROR, "FAILED RB transfer %d / %d\n",
                 transfer->status, transfer->actual_length);
        return;
    }
    sem_post(&dev->tr_rb_a);

    //Signal reply ready

    // TODO assosiate with writing thread
    *arefptr = alen;
    sem_post(&dev->rb_valid);
}

void LIBUSB_CALL libusb_transfer_ntfy(struct libusb_transfer *transfer)
{
    usb_dev_t* dev = (usb_dev_t* )transfer->user_data;
    USDR_LOG("USBX", USDR_LOG_DEBUG, "NTFY transfer %d / %d\n",
             transfer->status, transfer->actual_length);

    if (dev->stop)
        return;

    if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
        USDR_LOG("USBX", USDR_LOG_ERROR, "FAILED NTFY transfer %d / %d\n",
                 transfer->status, transfer->actual_length);
        return;
    }

    //parse event
    uint32_t *buff = (uint32_t*)transfer->buffer;
    unsigned packet_len = transfer->actual_length;
    unsigned i;
    for (i = 0; i < packet_len / 4; i++) {
        uint32_t header = buff[i];
        unsigned seqnum = header >> 16;
        unsigned event = header & 0x3f;
        unsigned blen = ((header >> 12) & 0xf);

        if (dev->ntfy_seqnum_exp != seqnum) {
            USDR_LOG("USBX", USDR_LOG_ERROR, "Notification exp seqnum %04x, got %04x: %08x hdr\n",
                     dev->ntfy_seqnum_exp, seqnum, header);
            dev->ntfy_seqnum_exp = seqnum;
        }
        dev->ntfy_seqnum_exp++;
        if (event >= MAX_INTERRUPTS) {
            USDR_LOG("USBX", USDR_LOG_ERROR, "Broken notification, event %d: %08x hdr\n", event, header);
            i += blen + 1;
            continue;
        }

        if (blen == 0 && ((i + 1) < packet_len / 4)) {
            USDR_LOG("USBX", USDR_LOG_NOTE, "Got notification seq %04x event %d => %08x\n",
                     seqnum, event, buff[i + 1]);
            dev->rbvalue[event] = buff[++i];
            sem_post(&dev->interrupts[event]);
        } else if ((i + 1 + blen) < packet_len / 4) {
            i += blen + 1;

            //TODO packet notification
            USDR_LOG("USBX", USDR_LOG_ERROR, "TODO!!!!!!!!!!!!!!!\n");
        } else {
            //packet overflow!
            USDR_LOG("USBX", USDR_LOG_ERROR, "Notification seqnum %04x, %08x hdr -- truncated!\n",
                     seqnum, header);
            break;
        }
    }

    // Resubmit
    //libusb_submit_transfer(transfer);
}

///////////////////////////////////////////////////////////////////////////////

static int usb_async_regwrite32(usb_dev_t* dev, unsigned addr, const uint32_t* data, unsigned sizedw, int timeout)
{
    uint32_t odata[OUT_REGOUT_SIZE/4 + 1];

    if (addr > 0xffff)
        return -EINVAL;
    if (sizedw > 0x40 || sizedw == 0)
        return -EINVAL;

    odata[0] = (((sizedw - 1) & 0x3f) << 16) | (addr & 0xffff);
    memcpy(&odata[1], data, sizedw * 4);

    return usb_post_regout(dev, odata, sizedw + 1, timeout);
}

static int usb_async_regread32(usb_dev_t* dev, unsigned addr, uint32_t* data, unsigned sizedw, int timeout)
{
    uint32_t odata[MAX_REQUEST_RB_SIZE/4 + 1];
    uint32_t cmd = (((sizedw - 1) & 0x3f) << 16) | (addr & 0xffff) | (0xC0000000);
    int res = usb_post_regout(dev, &cmd, 1, timeout);
    //unsigned cnt_dw, off = 0;
    //unsigned rem = sizedw;
    if (res) {
        return res;
    }

    res = usb_post_rb(dev, odata, 1 + sizedw /*sizeof(odata)/sizeof(odata[0])*/);
    if (res) {
        return res;
    }
    res = sem_wait(&dev->rb_valid);
    if (res) {
        res = -errno;
        return res;
    }
    if (odata[0] != 4 * sizedw) {
        return -EIO;
    }

    memcpy(data, &odata[1], sizedw * 4);
    return 0;
}


// Common operations
static
int usb_uram_generic_get(lldev_t dev, int generic_op, const char** pout)
{
    usb_dev_t* d = (usb_dev_t*)dev;

    switch (generic_op) {
    case LLGO_DEVICE_NAME: *pout = d->gdev.name; return 0;
    case LLGO_DEVICE_UUID: *pout = (const char*)d->gdev.devid.d; return 0;
    case LLGO_DEVICE_SDR_TYPE: *pout = (const char*)d->gdev.sdrtype; return 0;
    }

    return -EINVAL;
}


static int usb_uram_reg_out(usb_dev_t* dev, unsigned reg,
                                uint32_t outval)
{
    int res = usb_async_regwrite32(dev, reg, &outval, 1, 20000);

    USDR_LOG("USBX", USDR_LOG_DEBUG, "%s: Write [%04x] = %08x (%d)\n",
             dev->gdev.name, reg, outval, res);
    return res;
}

static int usb_uram_reg_in(usb_dev_t* dev, unsigned reg,
                               uint32_t *pinval)
{
    uint32_t inval;
    int	res = usb_async_regread32(dev, reg, &inval, 1, 20000);

    USDR_LOG("USBX", USDR_LOG_DEBUG, "%s: Read  [%04x] = %08x (%d)\n",
             dev->gdev.name, reg, inval, res);
    *pinval = inval;
    return res;
}



static int usb_uram_reg_out_n(usb_dev_t* dev, unsigned reg,
                                  const uint32_t *outval, const unsigned dwcnt)
{
    int res = usb_async_regwrite32(dev, reg, outval, dwcnt, 20000);
    USDR_LOG("USBX", USDR_LOG_DEBUG, "%s: WriteArray [%04x + %d] (%d)\n",
             dev->gdev.name, reg, dwcnt, res);
    return res;
}

static int usb_uram_reg_in_n(usb_dev_t* dev, unsigned reg,
                                 uint32_t *pinval, const unsigned dwcnt)
{
    unsigned off = 0;
    unsigned rem = dwcnt;
    unsigned sz = rem;

    for (; rem != 0; ) {
        if (sz > 256 / 4)
            sz = 256 / 4;

        int	res = usb_async_regread32(dev, reg + off, pinval + off, sz, 20000);
        USDR_LOG("USBX", USDR_LOG_DEBUG, "%s: ReadArray [%04x + %d] (%d)\n",
                 dev->gdev.name, reg, sz, res);
        if (res)
            return res;

        off += sz;
        rem -= sz;
    }

    return 0;
}

static
int usb_uram_reg_op(usb_dev_t* d, unsigned ls_op_addr,
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
                res = usb_uram_reg_out(d, d->db.idxreg_base[k],
                                           ls_op_addr - d->db.idxreg_virt_base[k] + i);
                if (res)
                    return res;

                if (i < memoutsz / 4) {
                    res = usb_uram_reg_out(d, d->db.idxreg_base[k] + 1, outa[i]);
                    if (res)
                        return res;
                }

                if (i < meminsz / 4) {
                    res = usb_uram_reg_in(d, d->db.idxreg_base[k] + 1, &ina[i]);
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
        res = usb_uram_reg_out_n(d, ls_op_addr, outa, memoutsz / 4);
        if (res)
            return res;
    } else if (memoutsz == 4) {
        res = usb_uram_reg_out(d, ls_op_addr, outa[0]);
        if (res)
            return res;
    }

    if (meminsz > 4) {
        res = usb_uram_reg_in_n(d, ls_op_addr, ina, meminsz / 4);
        if (res)
            return res;
    } else if (meminsz == 4) {
        res = usb_uram_reg_in(d, ls_op_addr, ina);
        if (res)
            return res;
    }
#else
    // Normal operation
    for (i = 0; i < memoutsz / 4; i++) {
        res = usb_uram_reg_out(d, ls_op_addr + i, outa[i]);
        if (res)
            return res;
    }
    for (i = 0; i < meminsz / 4; i++) {
        res = usb_uram_reg_in(d, ls_op_addr + i, &ina[i]);
        if (res)
            return res;
    }
#endif
    return 0;
}


static int usb_uram_wait_msi(usb_dev_t* dev, unsigned i, int timeout_ms)
{
    return sem_wait_ex(&dev->interrupts[i], timeout_ms * 1000);
}

static
int usb_uram_ls_op(lldev_t dev, subdev_t subdev,
                       unsigned ls_op, lsopaddr_t ls_op_addr,
                       size_t meminsz, void* pin,
                       size_t memoutsz, const void* pout)
{
    int res;
    usb_dev_t* d = (usb_dev_t*)dev;

    switch (ls_op) {
    case USDR_LSOP_HWREG: {
        uint32_t* ina = (uint32_t*)pin;
        const uint32_t* outa = (const uint32_t*)pout;

        return usb_uram_reg_op(d, ls_op_addr, ina, meminsz, outa, memoutsz);
    }
    case USDR_LSOP_SPI: {
        if (ls_op_addr >= d->db.spi_count)
            return -EINVAL;

        if (((meminsz != 4) && (meminsz != 0)) || (memoutsz != 4))
            return -EINVAL;

        res = usb_uram_reg_out(d, d->db.spi_base[ls_op_addr], *(const uint32_t*)pout);
        if (res)
            return res;

        res = libusb_to_errno(libusb_submit_transfer(d->transfer_ntfy[0]));
        if (res)
            return res;

        res = usb_uram_wait_msi(d, d->spi_int_number[ls_op_addr], 1000);
        if (res) {
            USDR_LOG("USBX", USDR_LOG_ERROR, "%s: SPI%d MSI wait timed out!\n",
                     d->gdev.name, ls_op_addr);
            return res;
        }

        if (meminsz != 0) {
            *(uint32_t*)pin = d->rbvalue[d->spi_int_number[ls_op_addr]];

            //res = usb_uram_reg_in(d, d->db.spi_base[ls_op_addr], (uint32_t*)pin);
            //if (res)
            //    return res;
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

        res = usb_uram_reg_out(d, d->db.i2c_base[ls_op_addr >> 8], i2ccmd);
        if (res)
            return res;

        if (meminsz > 0) {
            res = libusb_to_errno(libusb_submit_transfer(d->transfer_ntfy[0]));
            if (res)
                return res;

            res = usb_uram_wait_msi(d, d->i2c_int_number[ls_op_addr >> 8], 1000);
            if (res) {
                USDR_LOG("USBX", USDR_LOG_ERROR, "%s: I2C%d MSI wait timed out!\n",
                         d->gdev.name, ls_op_addr);
                return res;
            }

            data = d->rbvalue[d->i2c_int_number[ls_op_addr >> 8]];
            //res = usb_uram_reg_in(d, d->db.i2c_base[ls_op_addr >> 8], &data);
            //if (res)
            //    return res;

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
    case USDR_LSOP_DRP: {
        return device_bus_drp_generic_op(dev, subdev, &d->db, ls_op_addr, meminsz, pin, memoutsz, pout);
    }
    }
    return -EOPNOTSUPP;
}

static
uint32_t *_get_trailer_bursts(struct buffer_discriptor *bd, bool ext_stat, uint32_t *bursts, uint32_t *skipped)
{
    void* tr_buffer = buffers_get_ptr(bd->b, bd->bno);
    unsigned trailer_sz = (ext_stat) ? RX_PKT_TRAILER_EX : RX_PKT_TRAILER;
    unsigned buffer_sz = bd->buffer_sz - trailer_sz;
    *bursts = *((uint32_t*)(tr_buffer + buffer_sz));
    *skipped = *((uint32_t*)(tr_buffer + buffer_sz + 4));

    return (uint32_t*)(tr_buffer + buffer_sz);
}

static
void _usb_uram_stream_on_buffer(void* param, struct buffer_discriptor *rxbd)
{
    usb_dev_t* d = (usb_dev_t*)param;
    struct buffers *rxb = rxbd->b;
    uint32_t bursts, skipped;
    uint32_t* tr = _get_trailer_bursts(rxbd, d->rx_strms_extnty[0], &bursts, &skipped);

    if (rxbd->bno < rxb->buf_max) {
        unsigned buffers_discarded = d->rx_buffer_missed[0];
        tr[0] += d->rx_buffer_missed[0];
        d->rx_buffer_missed[0] = 0;
        buffers_ready_post(rxb);

        if (buffers_discarded > 0) {
            USDR_LOG("USBX", USDR_LOG_WARNING, "%d buffers were discarded due to slow processing in the application\n", buffers_discarded);
        }
    } else {
        d->app_drops[0]++;
        d->rx_buffer_missed[0] += 1 + (skipped & 0xffffff);
    }
}

static
int _usb_uram_init_rxstream(usb_dev_t* d,
                            lowlevel_stream_params_t* params,
                            stream_t* channel)
{
    int res;
    struct buffers *prxb = &d->rx_strms[0];
    unsigned transfers = MAX_IN_STRM_REQS > params->buffer_count ? params->buffer_count : MAX_IN_STRM_REQS;
    bool eventtype = (params->flags & LLSF_NEED_FDPOLL) == LLSF_NEED_FDPOLL;
    bool extntfy = (params->flags & LLSF_EXT_STAT) == LLSF_EXT_STAT;
    unsigned trailer_sz = (extntfy) ? RX_PKT_TRAILER_EX : RX_PKT_TRAILER;

    res = buffers_usb_init(&d->gdev, prxb, transfers, params->buffer_count,
                           params->block_size + trailer_sz, EP_IN_DEFSTREAM, eventtype);

    d->rx_strms_extnty[0] = extntfy;
    prxb->auto_restart = true;
    d->rx_buffer_missed[0] = 0;
    d->app_drops[0] = 0;
    prxb->on_buffer = &_usb_uram_stream_on_buffer;
    prxb->on_buffer_param = d;

    for (unsigned t = 0; t < transfers; t++) {
        res = buffers_usb_transfer_post(prxb,
                                        _buffers_prod_get_nolock(prxb),
                                        prxb->allocsz_rounded,
                                        t);
        if (res)
            return res;
    }

    params->underlying_fd = (eventtype) ? prxb->fd_event : -1;
    params->out_mtu_size = params->block_size;
    USDR_LOG("USBX", USDR_LOG_ERROR, "Stream RX prepared sz = %d, URBs = %d, evfd = %d!\n",
             prxb->allocsz_rounded, transfers, eventtype);
    *channel = DEV_RX_STREAM_NO;

    return 0;
}

static
int _usb_uram_init_txstream(usb_dev_t* d,
                            lowlevel_stream_params_t* params,
                            stream_t* channel)
{
    int res;
    struct buffers *prxb = &d->tx_strms[0];
    struct stream_params *sp = &d->tx_strms_params[0];

    bool eventtype = (params->flags & LLSF_NEED_FDPOLL) == LLSF_NEED_FDPOLL;
    unsigned buffers_cnt = params->buffer_count;
    if (buffers_cnt > MAX_OUT_STRM_REQS)
        buffers_cnt = MAX_OUT_STRM_REQS;

    if (params->block_size > MAX_TX_BUFFER_SZ) {
        USDR_LOG("USBX", USDR_LOG_WARNING, "Requested blocksize %d bytes is too big, maximum is %d!",
                 params->block_size, MAX_TX_BUFFER_SZ);
        return -EINVAL;
    }

    if (params->block_size > 0 && params->block_size < 64) {
        USDR_LOG("USBX", USDR_LOG_WARNING, "Requested blocksize %d bytes is too small, looks line an error!",
                 params->block_size);
        return -EINVAL;
    }
    if (params->block_size == 0) {
        params->block_size = MAX_TX_BUFFER_SZ;
    }

    res = buffers_usb_init(&d->gdev, prxb, buffers_cnt, buffers_cnt,
                           params->block_size + TX_PKT_HEADER, EP_OUT_DEFSTREAM, eventtype);
    if (res)
        return res;

    params->underlying_fd = (eventtype) ? prxb->fd_event : -1;
    params->out_mtu_size = params->block_size;
    USDR_LOG("USBX", USDR_LOG_ERROR, "Stream TX prepared sz = %d, URBs = %d, evfd = %d!\n",
             prxb->allocsz_rounded, buffers_cnt, eventtype);
    *channel = DEV_TX_STREAM_NO;
    sp->channels = params->channels;
    sp->bits_per_all_chs = params->bits_per_sym;
    return 0;
}

static
int usb_uram_stream_initialize(lldev_t dev, subdev_t subdev,
                                lowlevel_stream_params_t* params,
                                stream_t* channel)
{
    usb_dev_t* d = (usb_dev_t*)dev;

    // Only RX for now
    if (params->streamno == DEV_RX_STREAM_NO)
        return _usb_uram_init_rxstream(d, params, channel);
    else if (params->streamno == DEV_TX_STREAM_NO)
        return _usb_uram_init_txstream(d, params, channel);

    return -EINVAL;
}

static
int usb_uram_stream_deinitialize(lldev_t dev, subdev_t subdev, stream_t channel)
{
    usb_dev_t* d = (usb_dev_t*)dev;
    if (channel == DEV_RX_STREAM_NO) {
        buffers_usb_free(&d->rx_strms[0]);
    } else if (channel == DEV_TX_STREAM_NO) {
        buffers_usb_free(&d->tx_strms[0]);
    } else {
        return -EINVAL;
    }
    return 0;
}

static
int usb_uram_recv_dma_wait(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer,
                            void* oob_ptr, unsigned *oob_size, unsigned timeout)
{
    usb_dev_t* d = (usb_dev_t*)dev;
    int res;
    if (channel != DEV_RX_STREAM_NO)
        return -EINVAL;

    static uint64_t cnt = 0;
    struct buffers *rxb = &d->rx_strms[0];
    bool ext_stat = d->rx_strms_extnty[0];

    res = buffers_ready_wait(rxb, timeout * 1000);
    if (res) {
        USDR_LOG("USBX", USDR_LOG_ERROR, "Buffer not read: %d (%s)\n", res, strerror(-res));
        return res;
    }

    unsigned bno = buffers_consume(rxb);
    struct buffer_discriptor *bd = &rxb->bd[bno];
    void* tr_buffer = buffers_get_ptr(rxb, bno);

    // Parse trailer
    unsigned trailer_sz = (ext_stat) ? RX_PKT_TRAILER_EX : RX_PKT_TRAILER;

    if(trailer_sz > bd->buffer_sz)
    {
        USDR_LOG("USBX", USDR_LOG_ERROR, "Invalid bus data, buffer_sz=%u\n", bd->buffer_sz);
        return -EIO;
    }

    unsigned buffer_sz = bd->buffer_sz - trailer_sz;
    uint32_t* trailer_ptr = ((uint32_t*)(tr_buffer + buffer_sz));
    uint32_t bursts = trailer_ptr[0];
    uint32_t skipped = trailer_ptr[1];

    USDR_LOG("USBX", USDR_LOG_DEBUG, "Buffer %d Trailer %08x.%08x.%08x.%08x\n",
             bd->buffer_sz, trailer_ptr[3], trailer_ptr[2], trailer_ptr[1], trailer_ptr[0]);

    USDR_LOG("USBX",
             (rxb->allocsz == bd->buffer_sz) ? USDR_LOG_DEBUG : USDR_LOG_ERROR,
             "Buffer %d / %08x %08x  TO=%d SEQ=%16ld\n",
             buffer_sz, bursts, skipped, timeout, cnt);

    if (oob_size && *oob_size >= 8) {
        // memset(oob_ptr, 0, *oob_size);
        uint32_t* oobp = (uint32_t*)oob_ptr;
        oobp[1] = skipped;
        oobp[0] = bursts;
        if (ext_stat && *oob_size >= 16) {
            uint64_t* oobp64 = (uint64_t*)oob_ptr;
            //uint64_t txstats = *((uint64_t*)(tr_buffer + buffer_sz + 8));
            uint64_t txstats = *((uint64_t*)(tr_buffer + bd->buffer_sz - 8));
            oobp64[1] = txstats;
            *oob_size = 16;
        } else {
            memset(oob_ptr + 8, 0, *oob_size - 8);
            *oob_size = 8;
        }
    }

    *buffer = tr_buffer;
    cnt++;
    return 0;
}

static
int usb_uram_recv_dma_release(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer)
{
    usb_dev_t* d = (usb_dev_t*)dev;
    if (channel != DEV_RX_STREAM_NO)
        return -EINVAL;

    struct buffers *prxb = &d->rx_strms[0];
    buffers_available_post(prxb);

    return 0;
}

static
int usb_uram_send_dma_get(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer, void* oob_ptr, unsigned *oob_size, unsigned timeout)
{
    usb_dev_t* d = (usb_dev_t*)dev;
    int res;
    if (channel != DEV_TX_STREAM_NO)
        return -EINVAL;

    static uint64_t cnt = 0;
    struct buffers *rxb = &d->tx_strms[0];
    res = buffers_ready_wait(rxb, timeout * 1000);
    if (res)
        return res;

    unsigned bno = buffers_produce(rxb);
    *buffer = buffers_get_ptr(rxb, bno) + TXSTRM_META_SZ;

    USDR_LOG("USBX", USDR_LOG_DEBUG, "TX Alloc BNO=%d %ld\n", bno, cnt);
    if (oob_size) {
        unsigned sz = *oob_size;
        if (sz > 16)
            sz = 16;

        res = lowlevel_reg_rdndw(dev, subdev, 28, oob_ptr, sz / 4);
        if (res)
            return res;

        *oob_size = (sz / 4) * 4;
    }

    cnt++;
    return 0;
}

static
int usb_uram_send_dma_commit(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer, unsigned sz, const void* oob_ptr, unsigned oob_size)
{
    usb_dev_t* d = (usb_dev_t*)dev;
    int res;
    int64_t timestamp = -1;
    if (channel != DEV_TX_STREAM_NO) {
        USDR_LOG("USBX", USDR_LOG_ERROR,"USB TX Commit incorrect stream number\n");
        return -EINVAL;
    }
    if (oob_size == 8) {
        timestamp = ((int64_t*)(oob_ptr))[0];
    } else if (oob_size != 0) {
        return -EINVAL;
    }

    struct buffers *rxb = &d->tx_strms[0];
    if (sz > rxb->allocsz) {
        USDR_LOG("USBX", USDR_LOG_ERROR,"USB TX burst size is too big\n");
        return -EINVAL;
    }

    unsigned bno = buffers_consume(rxb);
    void* bx = buffers_get_ptr(rxb, bno);

    if (buffer != bx + TXSTRM_META_SZ) {
        USDR_LOG("USBX", USDR_LOG_ERROR,"USB TX incorrect pointer supplied\n");
        return -EINVAL;
    }

    uint64_t rsamples = sz * 8 / d->tx_strms_params[0].bits_per_all_chs;
    unsigned samples = rsamples - 1;

    uint32_t* header = (uint32_t*)bx;
    header[0] = timestamp;
    header[1] = ((timestamp >> 32) & 0xffff) | ((samples & 0x7fff) << 16) | (timestamp < 0 ? 0x80000000 : 0);
    header[2] = 0; // (samples >> 15) & 0x3;
    header[3] = 0;

    USDR_LOG("USBX", USDR_LOG_DEBUG, "TX post buffer %d: %08x.%08x.%08x.%08x -> %d bytes\n",
             bno, header[0], header[1], header[2], header[3], sz);

    // Add to senq
    res = buffers_usb_transfer_post(rxb, bno, sz + 16, bno);
    if (res) {
        USDR_LOG("USBX", USDR_LOG_ERROR,"USB TX%d unable to post busrt to sendq (error %d)\n", channel, res);
        return res;
    }
    return 0;
}

static
int usb_uram_await(lldev_t dev, subdev_t subdev, unsigned await_id, unsigned op, void** await_inout_aux_data, unsigned timeout)
{
    return -ENOTSUP;
}

static
int usb_uram_recv_buf(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer, unsigned *expected_sz, void* oob_ptr, unsigned *oob_size, unsigned timeout)
{
    return -ENOTSUP;
}

static
int usb_uram_send_buf(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer, unsigned sz, const void* oob_ptr, unsigned oob_size, unsigned timeout)
{
    return -ENOTSUP;
}


static
int usb_uram_destroy(lldev_t dev)
{
    usb_dev_t* d = (usb_dev_t*)dev;

    // Deinit streams
    for (unsigned sno = 0; sno < 1 + 1; sno++) {
        usb_uram_stream_deinitialize(dev, 0, sno);
    }

    // TODO: Wait for outstanding IO


    // Destroy undelying dev
    if (dev->pdev) {
        dev->pdev->destroy(dev->pdev);
    }

    libusb_generic_stop_thread(&d->gdev);
    libusb_close(d->gdev.dh);

    for (unsigned i = 0; i < MAX_INTERRUPTS; i++)
        sem_destroy(&d->interrupts[i]);

    sem_destroy(&d->tr_regout_a);
    sem_destroy(&d->tr_rb_a);
    sem_destroy(&d->rb_valid);

    free(d);
    return 0;
}

// Device operations
const static
struct lowlevel_ops s_usb_uram_ops = {
    usb_uram_generic_get,
    usb_uram_ls_op,
    usb_uram_stream_initialize,
    usb_uram_stream_deinitialize,
    usb_uram_recv_dma_wait,
    usb_uram_recv_dma_release,
    usb_uram_send_dma_get,
    usb_uram_send_dma_commit,
    usb_uram_recv_buf,
    usb_uram_send_buf,
    usb_uram_await,
    usb_uram_destroy,
};

// Factory functions
static
const char* usb_uram_plugin_info_str(unsigned iparam) {
    switch (iparam) {
    case LLPI_NAME_STR: return "usb";
    case LLPI_DESCRIPTION_STR: return "Native USB bridge";
    }
    return NULL;
}



#define MAX_DEV 64

static
int usb_uram_plugin_discovery(unsigned pcount, const char** filterparams,
                              const char** filtervals,
                              unsigned maxbuf, char* outarray)
{
    return libusb_generic_plugin_discovery(pcount, filterparams, filtervals, maxbuf,
                                           outarray, s_known_devices, KNOWN_USB_DEVICES, "usb");
}


static
int usb_uram_plugin_create(unsigned pcount, const char** devparam,
                               const char** devval, lldev_t* odev,
                               UNUSED unsigned vidpid, UNUSED void* webops, UNUSED uintptr_t param)
{
    int res;
    uint32_t hwid;
    usb_dev_t* dev = (usb_dev_t*)malloc(sizeof(usb_dev_t));
    if (dev == NULL) {
        res = ENOMEM;
        goto usbinit_fail;
    }

    memset(dev, 0, sizeof(usb_dev_t));
    dev->rx_strms[0].fd_event = -101;
    dev->tx_strms[0].fd_event = -101;
    dev->lld.ops = &dev->ops;
    dev->ops = s_usb_uram_ops;

    res = libusb_generic_plugin_create(pcount, devparam, devval, s_known_devices, KNOWN_USB_DEVICES,
                                       "usb", &dev->gdev);
    if (res) {
        goto usballoc_fail;
    }

    for (unsigned i = 0; i < MAX_INTERRUPTS; i++) {
        res = sem_init(&dev->interrupts[i], 0, 0);
        if (res)
            goto usballoc_fail;
    }

    //////////////////////////////////////////////////////////////////////////
    // FIXME Clear flush buffer
    {
        unsigned char data[65536];
        int actual_length = 0;
        int r = libusb_bulk_transfer(dev->gdev.dh, EP_IN_DEFSTREAM, data, sizeof(data), &actual_length, 50);
        if (r > 0) {
            USDR_LOG("USBX", USDR_LOG_ERROR, "Spurious buffer %d bytes\n", r);
        }
    }

    res = usb_async_start(dev);
    if (res) {
        USDR_LOG("USBX", USDR_LOG_ERROR,
                 "%s: unable to start USB manager: %d",
                 dev->gdev.name, res);
        goto usb_astart_fail;
    }

    res = usdr_device_create(&dev->lld, dev->gdev.devid);
    if (res) {
        USDR_LOG("USBX", USDR_LOG_ERROR,
                 "Unable to find device spcec for %s, uuid %s! Update software!\n",
                 dev->gdev.name, usdr_device_id_to_str(dev->gdev.devid));

        goto remove_dev;
    }

    res = device_bus_init(dev->lld.pdev, &dev->db);
    if (res) {
        USDR_LOG("USBX", USDR_LOG_ERROR,
                 "Unable to initialize bus parameters for the device %s!\n", dev->gdev.name);

        goto remove_dev;
    }

    //   res = libusb_clear_halt(dev->dh, 0x84);
    if (res) {
        USDR_LOG("USBX", USDR_LOG_ERROR,
                 "Unable clear halt the device %s: error %d!\n", dev->gdev.name, res);

        goto remove_dev;
    }

    struct device_params {
        const char* path;
        unsigned *store;
        unsigned count;
    } bii[] = {
    { DNLLFP_IRQ(DN_BUS_SPI, "%d"), dev->spi_int_number, dev->db.spi_count },
    { DNLLFP_IRQ(DN_BUS_I2C, "%d"), dev->i2c_int_number, dev->db.i2c_count },
    //{ DNLLFP_IRQ(DN_SRX, "%d"), dl.stream_int_number, dev->db.srx_count },
    //{ DNLLFP_IRQ(DN_STX, "%d"), dl.stream_int_number + dev->db.srx_count, dev->db.stx_count },
    //{ DNLLFP_NAME(DN_SRX, "%d", DNP_DMACAP), dl.stream_cap, dev->db.srx_count },
    //{ DNLLFP_NAME(DN_STX, "%d", DNP_DMACAP), dl.stream_cap + dev->db.srx_count, dev->db.stx_count },
    };
    uint64_t tmp;
    uint32_t interrupt_count;
    uint32_t interrupt_base;
    uint32_t int_mask = 0;
    for (unsigned i = 0; i < SIZEOF_ARRAY(bii); i++) {
        for (unsigned j = 0; j < bii[i].count; j++) {
            uint64_t tmp;
            char buffer[32];

            snprintf(buffer, sizeof(buffer), bii[i].path, j);
            res = usdr_device_vfs_obj_val_get_u64(dev->lld.pdev, buffer, &tmp);
            if (res) {
                goto remove_dev;
            }

            bii[i].store[j] = (unsigned)tmp;
            int_mask |= (1u << tmp);
        }
    }

    // TODO move hwid to 0
    // IGPI / HWID
    res = usb_uram_reg_in(dev, 16+12/4, &hwid);
    if (res)
        goto remove_dev;
    if (hwid & 0x40000000) {
        //if (!getenv("USDR_BARE_DEV")) {

        // INIT Interrupts only if PCIe is down
        res = usb_uram_reg_out(dev, 0xf, int_mask);
        // Put stream to 1

        res = usdr_device_vfs_obj_val_get_u64(dev->lld.pdev, DNLL_IRQ_COUNT, &tmp);
        if (res)
            goto remove_dev;

        interrupt_count = tmp;

        res = usdr_device_vfs_obj_val_get_u64(dev->lld.pdev, DNLLFP_BASE(DNP_IRQ, "0"), &tmp);
        if (res)
            goto remove_dev;

        interrupt_base = tmp;

        //Do these in case of pure USB only

        const unsigned REG_WR_PNTFY_CFG = 8;
        const unsigned REG_WR_PNTFY_ACK = 9;
        usb_uram_reg_out(dev, REG_WR_PNTFY_CFG, 0xdeadb000);
        usb_uram_reg_out(dev, REG_WR_PNTFY_CFG, 0xeeadb001);
        usb_uram_reg_out(dev, REG_WR_PNTFY_CFG, 0xfeadb002);
        usb_uram_reg_out(dev, REG_WR_PNTFY_CFG, 0xaeadb003);
        usb_uram_reg_out(dev, REG_WR_PNTFY_CFG, 0xbeadb004);
        usb_uram_reg_out(dev, REG_WR_PNTFY_CFG, 0xceadb005);
        usb_uram_reg_out(dev, REG_WR_PNTFY_CFG, 0x9eadb006);
        usb_uram_reg_out(dev, REG_WR_PNTFY_CFG, 0x8eadb007);

        for (unsigned i = 0; i < interrupt_count; i++) {
            res = usb_uram_reg_out(dev, interrupt_base,  i | (i << 8) | (1 << 16) | (7 << 20));
            res = usb_uram_reg_out(dev, REG_WR_PNTFY_ACK, 0 | i << 16);
        }

        // IGPO_FRONT activates tran_usb_active to route interrupts to NTFY endpoint
        res = usb_uram_reg_out(dev, 0, (15u << 24) | (0x80));
        if (res) {
            USDR_LOG("USBX", USDR_LOG_ERROR,
                     "Unable to set stream routing, error %d\n", res);
            goto remove_dev;
        }

    //}
    } else {
        USDR_LOG("USBX", USDR_LOG_WARNING, "Omit interrupt initialization on USB+PCIE mode\n");
    }

    // Device initialization
    res = dev->lld.pdev->initialize(dev->lld.pdev, pcount, devparam, devval);
    if (res) {
        USDR_LOG("USBX", USDR_LOG_ERROR,
                 "Unable to initialize device, error %d\n", res);
        goto remove_dev;
    }

    USDR_LOG("USBX", USDR_LOG_INFO, "USB device %s{%s} created with %d Mbps link\n",
             dev->gdev.name, usdr_device_id_to_str(dev->gdev.devid), dev->gdev.usb_speed);
    *odev = &dev->lld;
    return 0;

remove_dev:
    //usb_async_stop(dev->mgr);
usb_astart_fail:
    for (unsigned i = 0; i < MAX_INTERRUPTS; i++) {
        sem_destroy(&dev->interrupts[i]);
    }
usballoc_fail:
    free(dev);
usbinit_fail:
    //usb_context_free(ctx);
    return res;
}

// Factory operations
static const
struct lowlevel_plugin s_usb_uram_plugin = {
    usb_uram_plugin_info_str,
    usb_uram_plugin_discovery,
    usb_uram_plugin_create,
};


const struct lowlevel_plugin *usb_uram_register()
{
    USDR_LOG("USBX", USDR_LOG_INFO, "USB2 Native support registered!\n");

    return &s_usb_uram_plugin;
}
