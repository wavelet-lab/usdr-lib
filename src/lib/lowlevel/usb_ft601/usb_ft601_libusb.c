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
#include <usdr_logging.h>

#include "../device/device.h"
#include "../device/device_bus.h"
#include "../device/device_names.h"
#include "../device/device_vfs.h"

#include "../libusb_generic.h"
#include "../libusb_vidpid_map.h"

#include "usb_ft601_generic.h"

#include "../libusb_generic.h"

enum {
    DEV_RX_STREAM_NO = 0,
    DEV_TX_STREAM_NO = 1,
};

enum {
    EP_OUT_CONFIG      = LIBUSB_ENDPOINT_OUT | 1,
    EP_OUT_CTRL        = LIBUSB_ENDPOINT_OUT | 2,     //USB configuration fuses / reset bits
    EP_OUT_DEFSTREAM   = LIBUSB_ENDPOINT_OUT | 3,

    EP_IN_CONFIG       = LIBUSB_ENDPOINT_IN | 1,
    EP_IN_CTRL         = LIBUSB_ENDPOINT_IN | 2,
    EP_IN_DEFSTREAM    = LIBUSB_ENDPOINT_IN | 3,
};

enum {
    DATA_PACKET_SIZE = 4096,
};

// ft601 control commands

enum {
    CTRL_SIZE = 64, // EP_OUT_CTRL & EP_IN_CTRL size

    MAX_CONFIG_REQS = 1,

    MAX_IN_CTRL_REQS = 1,
    MAX_OUT_CTRL_REQS = 1,

    MAX_IN_STRM_REQS = 16,
    MAX_OUT_STRM_REQS = 16,

    MAX_CTRL_BURST = 64,
};




struct usbft601_dev
{
    struct lowlevel_dev lld;
    struct lowlevel_ops ops;

    libusb_generic_dev_t gdev;

    device_bus_t db;
    uint32_t ft601_counter;

    sem_t tr_ctrl_out;
    sem_t tr_ctrl_rb;
    unsigned len_ctrl_in_rb;

    struct libusb_transfer *transfer_in_ctrl[MAX_IN_CTRL_REQS];
    struct libusb_transfer *transfer_out_ctrl[MAX_OUT_CTRL_REQS];

    proto_lms64c_t data_ctrl_out[MAX_CTRL_BURST];
    proto_lms64c_t data_ctrl_in[MAX_CTRL_BURST];


    struct buffers rx_strms[1];
    struct buffers tx_strms[1];

};
typedef struct usbft601_dev usbft601_dev_t;

int _ft601_cmd(lldev_t lld, uint8_t ep, uint8_t cmd, uint32_t param)
{
    uint8_t wbuffer[20];
    int res, actual;
    usbft601_dev_t* d = (usbft601_dev_t*)lld;

    fill_ft601_cmd(wbuffer, ++d->ft601_counter, ep, 0x00, 0);
    res = libusb_to_errno(libusb_bulk_transfer(d->gdev.dh, EP_OUT_CONFIG, wbuffer, 20, &actual, 1000));
    if (res || actual != 20)
        return -EIO;

    fill_ft601_cmd(wbuffer, ++d->ft601_counter, ep, cmd, param);
    res = libusb_to_errno(libusb_bulk_transfer(d->gdev.dh, EP_OUT_CONFIG, wbuffer, 20, &actual, 1000));
    if (res || actual != 20)
        return -EIO;

    return 0;
}

// Class operations
// Common operations
static
int usbft601_uram_generic_get(lldev_t dev, int generic_op, const char** pout)
{
    usbft601_dev_t* d = (usbft601_dev_t*)dev;

    switch (generic_op) {
    case LLGO_DEVICE_NAME: *pout = d->gdev.name; return 0;
    case LLGO_DEVICE_UUID: *pout = (const char*)d->gdev.devid.d; return 0;
    case LLGO_DEVICE_SDR_TYPE: *pout = (const char*)d->gdev.sdrtype; return 0;
    }

    return -EINVAL;
}

static
int usbft601_uram_ctrl_out_pkt(usbft601_dev_t* dev, unsigned pkt_szb, unsigned timeout_ms)
{
    int res;
    res = sem_wait_ex(&dev->tr_ctrl_out, timeout_ms * 1000 * 1000);
    if (res) {
        return res;
    }

    unsigned idx = 0;
    struct libusb_transfer *transfer = dev->transfer_out_ctrl[idx];

    transfer->length = pkt_szb;
    transfer->timeout = timeout_ms;

    res = libusb_to_errno(libusb_submit_transfer(transfer));
    if (res) {
        USDR_LOG("USBX", USDR_LOG_ERROR, "FAILED to post CTRL_OUT %d\n", res);
        sem_post(&dev->tr_ctrl_out);
        return res;
    }

    return 0;
}

void LIBUSB_CALL libusb_transfer_ctrl_out_pkt(struct libusb_transfer *transfer)
{
    usbft601_dev_t* dev = (usbft601_dev_t* )transfer->user_data;
    const proto_lms64c_t* d = &dev->data_ctrl_out[0];

    USDR_LOG("USBX", USDR_LOG_DEBUG, "WR transfer %d / %d => { %02x %02x %02x %02x  %02x %02x %02x %02x | %02x %02x %02x %02x  %02x %02x %02x %02x  ... }\n",
             transfer->status, transfer->actual_length,
             d->cmd, d->status, d->blockCount, d->periphID,
             d->reserved[0], d->reserved[1], d->reserved[2], d->reserved[3],
             d->data[0], d->data[1], d->data[2], d->data[3],
             d->data[4], d->data[5], d->data[6], d->data[7]);

    if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
        USDR_LOG("USBX", USDR_LOG_ERROR, "FAILED CTRL_OUT transfer %d\n", transfer->status);
        return;
    }
}

static
int usbft601_uram_ctrl_in_pkt(usbft601_dev_t* dev, unsigned pkt_szb, unsigned timeout_ms)
{
    unsigned idx = 0;
    struct libusb_transfer *transfer = dev->transfer_in_ctrl[idx];
    int res;

    transfer->length = pkt_szb;
    transfer->buffer = (uint8_t*)dev->data_ctrl_in;

    res = libusb_to_errno(libusb_submit_transfer(transfer));
    if (res) {
        USDR_LOG("USBX", USDR_LOG_ERROR, "FAILED to post READBACK %d\n", res);
        return res;
    }

    return 0;
}

void LIBUSB_CALL libusb_transfer_ctrl_rb(struct libusb_transfer *transfer)
{
    usbft601_dev_t* dev = (usbft601_dev_t*)transfer->user_data;
    dev->len_ctrl_in_rb = transfer->actual_length;
    const proto_lms64c_t* d = &dev->data_ctrl_in[0];

    USDR_LOG("USBX", USDR_LOG_DEBUG, "     RB transfer %d / %d <= { %02x %02x %02x %02x  %02x %02x %02x %02x | %02x %02x %02x %02x  %02x %02x %02x %02x  ... }\n",
             transfer->status, transfer->actual_length,
             d->cmd, d->status, d->blockCount, d->periphID,
             d->reserved[0], d->reserved[1], d->reserved[2], d->reserved[3],
             d->data[0], d->data[1], d->data[2], d->data[3],
             d->data[4], d->data[5], d->data[6], d->data[7]);

    if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
        USDR_LOG("USBX", USDR_LOG_ERROR, "FAILED RB transfer %d / %d\n",
                 transfer->status, transfer->actual_length);
        return;
    }

    sem_post(&dev->tr_ctrl_rb);
}


static
int usbft601_ctrl_transfer(usbft601_dev_t* dev, uint8_t cmd, const uint8_t* data_in, unsigned length_in,
                                uint8_t* data_out, unsigned length_out, unsigned timeout_ms)
{
    int cnt, res;
    unsigned pkt_szb;
    // Compose packet
    if (length_in == 0)
        length_in = 1;

    cnt = lms64c_fill_packet(cmd, 0, 0, data_in, length_in, dev->data_ctrl_out, MAX_CTRL_BURST);
    if (cnt < 0) {
        USDR_LOG("USBX", USDR_LOG_ERROR, "Too long command %d bytes; might be an error!\n", length_in);
        return -EINVAL;
    }
    pkt_szb = cnt * LMS64C_PKT_LENGTH;

    res = usbft601_uram_ctrl_out_pkt(dev, pkt_szb, timeout_ms);
    if (res) {
        return res;
    }

    res = usbft601_uram_ctrl_in_pkt(dev, pkt_szb, timeout_ms);
    if (res) {
        goto failed;
    }

    res = sem_wait_ex(&dev->tr_ctrl_rb, timeout_ms * 1000 * 1000);
    if (res) {
        goto failed;
    }

    // Parse result
    if (length_out) {
        res = lms64c_parse_packet(cmd, dev->data_ctrl_in, cnt, data_out, length_out);
    }

    // Return status of transfer
    switch (dev->data_ctrl_in[0].status) {
    case STATUS_COMPLETED_CMD: res = 0; break;
    case STATUS_UNKNOWN_CMD: res = -ENOENT; break;
    case STATUS_BUSY_CMD: res = -EBUSY; break;
    default: res = -EFAULT; break;
    }

failed:
    sem_post(&dev->tr_ctrl_out);
    return res;
}

struct ft601_device_info {
    unsigned firmware;
    unsigned device;
    unsigned protocol;
    unsigned hardware;
    unsigned expansion;
    uint64_t boardSerialNumber;
};
typedef struct ft601_device_info ft601_device_info_t;

static
int usbft601_uram_get_info(usbft601_dev_t* dev, ft601_device_info_t* info)
{
    uint8_t tmpdata[32];
    int res;

    res = usbft601_ctrl_transfer(dev, CMD_GET_INFO, NULL, 0, tmpdata, sizeof(tmpdata), 3000);
    if (res)
        return res;

    info->firmware = tmpdata[0];
    info->device = tmpdata[1];
    info->protocol = tmpdata[2];
    info->hardware = tmpdata[3];
    info->expansion = tmpdata[4];
    info->boardSerialNumber = 0;

    return 0;
}



static
int usbft601_uram_ls_op(lldev_t dev, subdev_t subdev,
                        unsigned ls_op, lsopaddr_t ls_op_addr,
                        size_t meminsz, void* pin,
                        size_t memoutsz, const void* pout)
{
    int res;
    usbft601_dev_t* d = (usbft601_dev_t*)dev;
    unsigned timeout_ms = 3000;
    uint16_t tmpbuf_out[2048];

    switch (ls_op) {
    case USDR_LSOP_HWREG:
         if (pout == NULL || memoutsz == 0) {
             if (meminsz > sizeof(tmpbuf_out))
                 return -E2BIG;

             // Register read
             for (unsigned i = 0; i < meminsz / 2; i++) {
                 tmpbuf_out[i] = ls_op_addr + i;
             }

             res = usbft601_ctrl_transfer(d, CMD_BRDSPI_RD, (const uint8_t* )tmpbuf_out, meminsz, pin, meminsz, timeout_ms);
         } else if (pin == NULL || meminsz == 0) {
             if (memoutsz * 2 > sizeof(tmpbuf_out))
                 return -E2BIG;

             // Rgister write
             const uint16_t* out_s = (const uint16_t* )pout;
             for (unsigned i = 0; i < memoutsz / 2; i++) {
                 tmpbuf_out[2 * i + 1] = ls_op_addr + i;
                 tmpbuf_out[2 * i + 0] = out_s[i];
             }

             res = usbft601_ctrl_transfer(d, CMD_BRDSPI_WR, (const uint8_t* )tmpbuf_out, memoutsz * 2, pin, meminsz, timeout_ms);
         } else {
             return -EINVAL;
         }
         break;

    case USDR_LSOP_SPI:
        // TODO split to RD / WR packets
        if (pin == NULL || meminsz == 0 || ((*(const uint32_t*)pout) & 0x80000000) != 0) {
            res = usbft601_ctrl_transfer(d, CMD_LMS7002_WR, pout, memoutsz, pin, meminsz, timeout_ms);
        } else {
            for (unsigned k = 0; k < memoutsz / 4; k++) {
                const uint32_t* pz = (const uint32_t*)(pout + 4 * k);
                tmpbuf_out[k] = *pz >> 16;
            }
            res = usbft601_ctrl_transfer(d, CMD_LMS7002_RD, (const uint8_t* )tmpbuf_out /*pout*/, memoutsz / 2, pin, meminsz, timeout_ms);
        }
        break;

    case USDR_LSOP_I2C_DEV:
        break;

    case USDR_LSOP_CUSTOM_CMD: {
        uint8_t cmd = LMS_RST_PULSE;
        res = usbft601_ctrl_transfer(d, CMD_LMS7002_RST, (const uint8_t* )&cmd, 1, NULL, 0, timeout_ms);
        break;
    }
    default:
        return -EOPNOTSUPP;
    }

    return res;
}

static
int usbft601_uram_stream_initialize(lldev_t dev, subdev_t subdev,
                                    lowlevel_stream_params_t* params,
                                    stream_t* channel)
{
    usbft601_dev_t* d = (usbft601_dev_t*)dev;
    struct buffers *prxb = (params->streamno == DEV_RX_STREAM_NO) ? &d->rx_strms[0] : &d->tx_strms[0];
    bool eventtype = (params->flags & LLSF_NEED_FDPOLL) == LLSF_NEED_FDPOLL;
    int res = 0;
    unsigned buffers_cnt = params->buffer_count;
    if (buffers_cnt > MAX_OUT_STRM_REQS)
        buffers_cnt = MAX_OUT_STRM_REQS;

    const unsigned data_endpoint = (params->streamno == DEV_RX_STREAM_NO) ? EP_IN_DEFSTREAM : EP_OUT_DEFSTREAM;

    res = res ? res : ft601_flush_pipe(dev, data_endpoint);
    res = res ? res : ft601_set_stream_pipe(dev, data_endpoint, DATA_PACKET_SIZE);
    res = res ? res : buffers_usb_init(&d->gdev, prxb, buffers_cnt, (params->streamno == DEV_RX_STREAM_NO) ? 2 * buffers_cnt : buffers_cnt,
                           params->block_size,
                           data_endpoint,
                           eventtype);
    if (res)
        return res;

    if (params->streamno == DEV_RX_STREAM_NO) {
        prxb->auto_restart = true;
    }

    if (params->streamno == DEV_RX_STREAM_NO) for (unsigned t = 0; t < buffers_cnt; t++) {
        res = buffers_usb_transfer_post(prxb,
                                        _buffers_prod_get_nolock(prxb),
                                        prxb->allocsz_rounded,
                                        t);
        if (res)
            return res;
    }

    params->underlying_fd = (eventtype) ? prxb->fd_event : -1;
    *channel = params->streamno;
    return 0;
}


static
int usbft601_uram_stream_deinitialize(lldev_t dev, subdev_t subdev, stream_t channel)
{
    usbft601_dev_t* d = (usbft601_dev_t*)dev;
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
int usbft601_uram_recv_dma_wait(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer,
                                void* oob_ptr, unsigned *oob_size, unsigned timeout)
{
    int res;
    usbft601_dev_t* d = (usbft601_dev_t*)dev;
    if (channel != DEV_RX_STREAM_NO)
        return -EINVAL;

    static uint64_t cnt = 0;
    struct buffers *rxb = &d->rx_strms[0];
    res = buffers_ready_wait(rxb, timeout * 1000);
    if (res) {
        return res;
    }

    unsigned bno = buffers_consume(rxb);
    struct buffer_discriptor *bd = &rxb->bd[bno];
    void* tr_buffer = buffers_get_ptr(rxb, bno);
    uint32_t* pkt = tr_buffer;

    USDR_LOG("USBX",
             (rxb->allocsz == bd->buffer_sz) ? USDR_LOG_DEBUG : USDR_LOG_ERROR,
             "Buffer %d / %08x %08x %08x %08x  TO=%d SEQ=%16ld\n",
             bd->buffer_sz, pkt[0], pkt[1], pkt[2], pkt[3], timeout, cnt);


    *buffer = tr_buffer;
    cnt++;
    return 0;
}

static
int usbft601_uram_recv_dma_release(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer)
{
    usbft601_dev_t* d = (usbft601_dev_t*)dev;

    if (channel != DEV_RX_STREAM_NO)
        return -EINVAL;

    struct buffers *prxb = &d->rx_strms[0];
    buffers_available_post(prxb);

    return 0;
}

static
int usbft601_uram_send_dma_get(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer, void* oob_ptr, unsigned *oob_size, unsigned timeout)
{
    int res;
    usbft601_dev_t* d = (usbft601_dev_t*)dev;

    if (channel != DEV_TX_STREAM_NO)
        return -EINVAL;

    static uint64_t cnt = 0;
    struct buffers *rxb = &d->tx_strms[0];
    res = buffers_ready_wait(rxb, timeout * 1000);
    if (res)
        return res;

    unsigned bno = buffers_produce(rxb);
    *buffer = buffers_get_ptr(rxb, bno);

    USDR_LOG("USBX", USDR_LOG_DEBUG, "TX Alloc BNO=%d %ld\n", bno, cnt);

    cnt++;
    return 0;
}

static
int usbft601_uram_send_dma_commit(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer, unsigned sz, const void* oob_ptr, unsigned oob_size)
{
    int res;
    usbft601_dev_t* d = (usbft601_dev_t*)dev;
    if (channel != DEV_TX_STREAM_NO) {
        USDR_LOG("USBX", USDR_LOG_ERROR,"USB TX Commit incorrect stream number\n");
        return -EINVAL;
    }

    struct buffers *rxb = &d->tx_strms[0];
    if (sz > rxb->allocsz) {
        USDR_LOG("USBX", USDR_LOG_ERROR,"USB TX burst size is too big\n");
        return -EINVAL;
    }

    unsigned bno = buffers_consume(rxb);
    void* bx = buffers_get_ptr(rxb, bno);

    if (buffer != bx) {
        USDR_LOG("USBX", USDR_LOG_ERROR,"USB TX incorrect pointer supplied\n");
        return -EINVAL;
    }

    // Add to senq
    res = buffers_usb_transfer_post(rxb, bno, sz, bno);
    if (res) {
        USDR_LOG("USBX", USDR_LOG_ERROR,"USB TX%d unable to post busrt to sendq (error %d)\n", channel, res);
        return res;
    }

    uint8_t header_0 = *(uint8_t*)buffer;
    uint16_t header_1 = *(uint16_t*)((uint8_t*)buffer + 1);
    uint64_t header_8 = *(uint64_t*)((uint8_t*)buffer + 8);

    USDR_LOG("USBX", USDR_LOG_NOTE, "TX post buffer %d: %02x.%04x %16lld -> %d bytes\n",
             bno, header_0, header_1, (long long)header_8, sz);

    return 0;
}

static
int usbft601_uram_await(lldev_t dev, subdev_t subdev, unsigned await_id, unsigned op, void** await_inout_aux_data, unsigned timeout)
{
    return -ENOTSUP;
}

static
int usbft601_uram_recv_buf(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer, unsigned *expected_sz, void* oob_ptr, unsigned *oob_size, unsigned timeout)
{
    return -ENOTSUP;
}

static
int usbft601_uram_send_buf(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer, unsigned sz, const void* oob_ptr, unsigned oob_size, unsigned timeout)
{
    return -ENOTSUP;
}


static
int usbft601_uram_destroy(lldev_t dev)
{
    usbft601_dev_t* d = (usbft601_dev_t*)dev;


    // Deinit streams
    for (unsigned sno = 0; sno < 1 + 1; sno++) {
        usbft601_uram_stream_deinitialize(dev, 0, sno);
    }

    // TODO: Wait for outstanding IO


    // Destroy undelying dev
    if (dev->pdev) {
        dev->pdev->destroy(dev->pdev);
    }

    libusb_generic_stop_thread(&d->gdev);
    libusb_close(d->gdev.dh);

    /////


    free(d);
    return 0;

}


// Device operations
const static
struct lowlevel_ops s_usbft601_uram_ops = {
    usbft601_uram_generic_get,
    usbft601_uram_ls_op,
    usbft601_uram_stream_initialize,
    usbft601_uram_stream_deinitialize,
    usbft601_uram_recv_dma_wait,
    usbft601_uram_recv_dma_release,
    usbft601_uram_send_dma_get,
    usbft601_uram_send_dma_commit,
    usbft601_uram_recv_buf,
    usbft601_uram_send_buf,
    usbft601_uram_await,
    usbft601_uram_destroy,
};

static
const char* usbft601_uram_plugin_info_str(unsigned iparam) {
    switch (iparam) {
    case LLPI_NAME_STR: return "usbft601";
    case LLPI_DESCRIPTION_STR: return "FT601 LMS64C bridge";
    }
    return NULL;
}

static
int usbft601_uram_plugin_discovery(unsigned pcount, const char** filterparams,
                                   const char** filtervals,
                                   unsigned maxbuf, char* outarray)
{
    return libusb_generic_plugin_discovery(pcount, filterparams, filtervals, maxbuf,
                                           outarray, s_known_devices, KNOWN_USB_DEVICES, "usbft601");
}


static
int usbft601_uram_async_start(usbft601_dev_t* dev)
{
    int res;

    res = sem_init(&dev->tr_ctrl_out, 0, MAX_OUT_CTRL_REQS);
    if (res) {
        goto failed_prepare;
    }

    res = sem_init(&dev->tr_ctrl_rb, 0, 0);
    if (res) {
        goto failed_prepare;
    }

    // Prepare transfer queues
    res = libusb_generic_prepare_transfer(&dev->gdev, dev, EP_OUT_CTRL, LIBUSB_TRANSFER_TYPE_BULK,
                                          dev->transfer_out_ctrl,
                                          (uint8_t*)dev->data_ctrl_out,
                                          sizeof(dev->data_ctrl_out) / MAX_OUT_CTRL_REQS,
                                          MAX_OUT_CTRL_REQS,
                                          &libusb_transfer_ctrl_out_pkt);
    if (res) {
        goto failed_prepare;
    }

    res = libusb_generic_prepare_transfer(&dev->gdev, dev, EP_IN_CTRL, LIBUSB_TRANSFER_TYPE_BULK,
                                          dev->transfer_in_ctrl,
                                          (uint8_t*)dev->data_ctrl_in,
                                          sizeof(dev->data_ctrl_in) / MAX_IN_CTRL_REQS,
                                          MAX_IN_CTRL_REQS,
                                          &libusb_transfer_ctrl_rb);
    if (res) {
        goto failed_prepare;
    }

    return libusb_generic_create_thread(&dev->gdev);

failed_prepare:
    return res;
}



static
int usbft601_uram_plugin_create(unsigned pcount, const char** devparam,
                                const char** devval, lldev_t* odev,
                                UNUSED unsigned vidpid, UNUSED void* webops, UNUSED uintptr_t param)
{
    int res;
    usbft601_dev_t* dev = (usbft601_dev_t*)malloc(sizeof(usbft601_dev_t));
    if (dev == NULL) {
        res = ENOMEM;
        goto usballoc_fail;
    }

    memset(dev, 0, sizeof(usbft601_dev_t));
    dev->lld.ops = &dev->ops;
    dev->ops = s_usbft601_uram_ops;

    res = libusb_generic_plugin_create(pcount, devparam, devval, s_known_devices, KNOWN_USB_DEVICES,
                                       "usbft601", &dev->gdev);
    if (res) {
        goto usballoc_fail;
    }

    res = libusb_to_errno(libusb_claim_interface(dev->gdev.dh, 1));
    if (res) {
        goto usballoc_fail;
    }

    res = libusb_to_errno(libusb_reset_device(dev->gdev.dh));
    if (res) {
        goto usballoc_fail;
    }

    // FT601 initialization
    res = res ? res : ft601_flush_pipe(&dev->lld, EP_IN_CTRL);  //clear ctrl ep rx buffer
    res = res ? res : ft601_set_stream_pipe(&dev->lld, EP_IN_CTRL, CTRL_SIZE);
    res = res ? res : ft601_set_stream_pipe(&dev->lld, EP_OUT_CTRL, CTRL_SIZE);
    if (res) {
        goto usballoc_fail;
    }

    res = usbft601_uram_async_start(dev);
    if (res) {
        goto usballoc_fail;
    }

    ft601_device_info_t nfo;
    res = usbft601_uram_get_info(dev, &nfo);
    if (res) {
        goto stop_async;
    }

    USDR_LOG("USBX", USDR_LOG_INFO, "Firmware: %d, Dev: %d, Proto: %d, HW: %d, Serial: %lld\n",
             nfo.firmware, nfo.device, nfo.protocol, nfo.hardware,
             (long long int)nfo.boardSerialNumber);

    if (nfo.device != LMS_DEV_LIMESDRMINI_V2) {
        USDR_LOG("USBX", USDR_LOG_INFO, "Unsupported device!\n");
        goto stop_async;
    }

    // Device initialization
    res = usdr_device_create(&dev->lld, dev->gdev.devid);
    if (res) {
        USDR_LOG("USBX", USDR_LOG_ERROR,
                 "Unable to find device spcec for %s, uuid %s! Update software!\n",
                 dev->gdev.name, usdr_device_id_to_str(dev->gdev.devid));

        goto stop_async;
    }

    res = device_bus_init(dev->lld.pdev, &dev->db);
    if (res) {
        USDR_LOG("USBX", USDR_LOG_ERROR,
                 "Unable to initialize bus parameters for the device %s!\n", dev->gdev.name);

        goto remove_dev;
    }

    // Register operations are now available



    res = dev->lld.pdev->initialize(dev->lld.pdev, pcount, devparam, devval);
    if (res) {
        USDR_LOG("U601", USDR_LOG_ERROR,
                 "Unable to initialize device, error %d\n", res);
        goto remove_dev;
    }

    USDR_LOG("U601", USDR_LOG_INFO, "USB FT601 device %s{%s} created with %d Mbps link\n",
             dev->gdev.name, usdr_device_id_to_str(dev->gdev.devid), dev->gdev.usb_speed);

    *odev = &dev->lld;
    return 0;

remove_dev:

stop_async:

usballoc_fail:
    free(dev);
    return res;
}


// Factory operations
static const
struct lowlevel_plugin s_usbft601_uram_plugin = {
    usbft601_uram_plugin_info_str,
    usbft601_uram_plugin_discovery,
    usbft601_uram_plugin_create,
};


const struct lowlevel_plugin *usbft601_uram_register()
{
    USDR_LOG("USBX", USDR_LOG_INFO, "FT601 LMS64C support registered!\n");

    return &s_usbft601_uram_plugin;
}



