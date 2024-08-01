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
#include "../device/device_names.h"
#include "../device/device_vfs.h"

#include "../libusb_generic.h"
#include "../libusb_vidpid_map.h"

#include "usb_ft601_generic.h"
#include "../libusb_generic.h"

struct usbft601_dev
{
    struct lowlevel_dev lld;
    struct lowlevel_ops ops;

    libusb_generic_dev_t gdev;
    usb_ft601_generic_t ft601_generic;

    sem_t tr_ctrl_out;
    sem_t tr_ctrl_rb;
    unsigned len_ctrl_in_rb;

    struct libusb_transfer *transfer_in_ctrl[MAX_IN_CTRL_REQS];
    struct libusb_transfer *transfer_out_ctrl[MAX_OUT_CTRL_REQS];

    struct buffers rx_strms[1];
    struct buffers tx_strms[1];

};
typedef struct usbft601_dev usbft601_dev_t;


usb_ft601_generic_t* get_ft601_generic(lldev_t dev)
{
    usbft601_dev_t* d = (usbft601_dev_t*)dev;
    return &d->ft601_generic;
}

static int libusb_io_write(lldev_t lld, uint8_t* wbuffer, int len, int* actual_len, int timeout)
{
    usbft601_dev_t* d = (usbft601_dev_t*)lld;
    return libusb_to_errno(libusb_bulk_transfer(d->gdev.dh, EP_OUT_CONFIG, wbuffer, 20, actual_len, timeout));
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
int usbft601_uram_ctrl_out_pkt(lldev_t lld, unsigned pkt_szb, unsigned timeout_ms)
{
    usbft601_dev_t* d = (usbft601_dev_t*)lld;
    int res;
    res = sem_wait_ex(&d->tr_ctrl_out, timeout_ms * 1000 * 1000);
    if (res) {
        return res;
    }

    unsigned idx = 0;
    struct libusb_transfer *transfer = d->transfer_out_ctrl[idx];

    transfer->length = pkt_szb;
    transfer->timeout = timeout_ms;

    res = libusb_to_errno(libusb_submit_transfer(transfer));
    if (res) {
        USDR_LOG("USBX", USDR_LOG_ERROR, "FAILED to post CTRL_OUT %d\n", res);
        sem_post(&d->tr_ctrl_out);
        return res;
    }

    return 0;
}

void LIBUSB_CALL libusb_transfer_ctrl_out_pkt(struct libusb_transfer *transfer)
{
    usbft601_dev_t* dev = (usbft601_dev_t* )transfer->user_data;
    const proto_lms64c_t* d = &dev->ft601_generic.data_ctrl_out[0];

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
int usbft601_uram_ctrl_in_pkt(lldev_t lld, unsigned pkt_szb, unsigned timeout_ms)
{
    usbft601_dev_t* d = (usbft601_dev_t*)lld;

    unsigned idx = 0;
    struct libusb_transfer *transfer = d->transfer_in_ctrl[idx];
    int res;

    transfer->length = pkt_szb;
    transfer->buffer = (uint8_t*)d->ft601_generic.data_ctrl_in;

    res = libusb_to_errno(libusb_submit_transfer(transfer));
    if (res) {
        USDR_LOG("USBX", USDR_LOG_ERROR, "FAILED to post READBACK %d\n", res);
        return res;
    }

    return 0;
}

static int usbft601_sem_ctrl_rb_wait(lldev_t lld, int64_t timeout)
{
    usbft601_dev_t* d = (usbft601_dev_t*)lld;
    return sem_wait_ex(&d->tr_ctrl_rb, timeout);
}

static void usbft601_sem_ctrl_out_post(lldev_t lld)
{
    usbft601_dev_t* d = (usbft601_dev_t*)lld;
    sem_post(&d->tr_ctrl_out);
}

void LIBUSB_CALL libusb_transfer_ctrl_rb(struct libusb_transfer *transfer)
{
    usbft601_dev_t* dev = (usbft601_dev_t*)transfer->user_data;
    dev->len_ctrl_in_rb = transfer->actual_length;
    const proto_lms64c_t* d = &dev->ft601_generic.data_ctrl_in[0];

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
int usbft601_uram_async_start(lldev_t lld)
{
    int res;
    usbft601_dev_t* dev = (usbft601_dev_t*)lld;

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
                                          (uint8_t*)dev->ft601_generic.data_ctrl_out,
                                          sizeof(dev->ft601_generic.data_ctrl_out) / MAX_OUT_CTRL_REQS,
                                          MAX_OUT_CTRL_REQS,
                                          &libusb_transfer_ctrl_out_pkt);
    if (res) {
        goto failed_prepare;
    }

    res = libusb_generic_prepare_transfer(&dev->gdev, dev, EP_IN_CTRL, LIBUSB_TRANSFER_TYPE_BULK,
                                          dev->transfer_in_ctrl,
                                          (uint8_t*)dev->ft601_generic.data_ctrl_in,
                                          sizeof(dev->ft601_generic.data_ctrl_in) / MAX_IN_CTRL_REQS,
                                          MAX_IN_CTRL_REQS,
                                          &libusb_transfer_ctrl_rb);
    if (res) {
        goto failed_prepare;
    }

    return libusb_generic_create_thread(&dev->gdev);

failed_prepare:
    return res;
}

const static
usb_ft601_io_ops_t s_io_ops = {
    libusb_io_write,
    usbft601_uram_ctrl_out_pkt,
    usbft601_uram_ctrl_in_pkt,
    usbft601_sem_ctrl_rb_wait,
    usbft601_sem_ctrl_out_post,
    usbft601_uram_async_start,
};

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
    dev->rx_strms[0].fd_event = -101;
    dev->tx_strms[0].fd_event = -101;
    dev->lld.ops = &dev->ops;
    dev->ops = s_usbft601_uram_ops;

    dev->ft601_generic.io_ops = s_io_ops;

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

    res = usbft601_uram_generic_create_and_init(&dev->lld, pcount, devparam, devval, &dev->gdev.devid);
    if(res) {
        goto usballoc_fail;
    }

    USDR_LOG("U601", USDR_LOG_INFO, "USB FT601 device %s{%s} created with %d Mbps link\n",
             dev->gdev.name, usdr_device_id_to_str(dev->gdev.devid), dev->gdev.usb_speed);

    *odev = &dev->lld;
    return 0;

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



