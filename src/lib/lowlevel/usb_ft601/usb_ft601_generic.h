// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef USB_FT601_H
#define USB_FT601_H

#include <stdint.h>
#include <string.h>

#include "../usdr_lowlevel.h"
#include <usdr_logging.h>
#include "../ipblks/lms64c_proto.h"
#include "../ipblks/lms64c_cmds.h"
#include "../device/device_bus.h"


#define LMS7002M_SPI_INDEX 0x10
#define ADF4002_SPI_INDEX  0x30

enum {
    DEV_RX_STREAM_NO = 0,
    DEV_TX_STREAM_NO = 1,
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

enum {
    EP_OUT_CONFIG      = 0 | 1,
    EP_OUT_CTRL        = 0 | 2,
    EP_OUT_DEFSTREAM   = 0 | 3,

    EP_IN_CONFIG       = 0x80 | 1,
    EP_IN_CTRL         = 0x80 | 2,
    EP_IN_DEFSTREAM    = 0x80 | 3,
};

struct ft601_device_info {
    unsigned firmware;
    unsigned device;
    unsigned protocol;
    unsigned hardware;
    unsigned expansion;
    uint64_t boardSerialNumber;
};
typedef struct ft601_device_info ft601_device_info_t;

typedef int (*io_write_fn_t)(lldev_t lld, uint8_t* wbuffer, int len, int* actual_len, int timeout);
typedef int (*io_ctrl_out_pkt_fn_t)(lldev_t lld, unsigned pkt_szb, unsigned timeout_ms);
typedef int (*io_ctrl_in_pkt_fn_t)(lldev_t lld, unsigned pkt_szb, unsigned timeout_ms);

typedef int (*io_lock_wait_t)(lldev_t lld, int64_t timeout);
typedef void (*io_lock_post_t)(lldev_t lld);

typedef int (*io_async_start_fn_t)(lldev_t lld);


struct usb_ft601_io_ops
{
    io_write_fn_t io_write_fn;
    io_ctrl_out_pkt_fn_t io_ctrl_out_pkt_fn;
    io_ctrl_in_pkt_fn_t io_ctrl_in_pkt_fn;

    io_lock_wait_t io_lock_wait;
    io_lock_post_t io_lock_post;

    io_async_start_fn_t io_async_start_fn;
};
typedef struct usb_ft601_io_ops usb_ft601_io_ops_t;

struct usb_ft601_generic
{
    device_bus_t db;
    uint32_t ft601_counter;
    proto_lms64c_t data_ctrl_out[MAX_CTRL_BURST];
    proto_lms64c_t data_ctrl_in[MAX_CTRL_BURST];

    usb_ft601_io_ops_t io_ops;
};
typedef struct usb_ft601_generic usb_ft601_generic_t;


static inline
void fill_ft601_cmd(uint8_t wbuffer[20],
                    uint32_t ft601_counter,
                    uint8_t ep,
                    uint8_t cmd,
                    uint32_t param)
{
    memset(wbuffer, 0, 20);

    wbuffer[0] = (ft601_counter) & 0xFF;
    wbuffer[1] = (ft601_counter >> 8) & 0xFF;
    wbuffer[2] = (ft601_counter >> 16) & 0xFF;
    wbuffer[3] = (ft601_counter >> 24) & 0xFF;
    wbuffer[4] = ep;
    wbuffer[5] = cmd;
    wbuffer[8] = (param) & 0xFF;
    wbuffer[9] = (param >> 8) & 0xFF;
    wbuffer[10] = (param >> 16) & 0xFF;
    wbuffer[11] = (param >> 24) & 0xFF;
}

int _ft601_cmd(lldev_t lld, uint8_t ep, uint8_t cmd, uint32_t param);

static inline
    int ft601_flush_pipe(lldev_t lld, unsigned char ep)
{
    return _ft601_cmd(lld, ep, 0x03, 0);
}

static inline
    int ft601_set_stream_pipe(lldev_t lld, unsigned char ep, size_t size)
{
    return _ft601_cmd(lld, ep, 0x02, size);
}

const struct lowlevel_plugin *usbft601_uram_register();


int _ft601_cmd(lldev_t lld, uint8_t ep, uint8_t cmd, uint32_t param);

int usbft601_ctrl_transfer(lldev_t lld, uint8_t cmd, const uint8_t* data_in, unsigned length_in,
                           uint8_t* data_out, unsigned length_out, unsigned timeout_ms);

int usbft601_uram_ls_op(lldev_t dev, subdev_t subdev,
                        unsigned ls_op, lsopaddr_t ls_op_addr,
                        size_t meminsz, void* pin,
                        size_t memoutsz, const void* pout);

int usbft601_uram_get_info(lldev_t dev, ft601_device_info_t* info);

int usbft601_uram_generic_create_and_init(lldev_t lld, unsigned pcount, const char** devparam,
                                          const char** devval, device_id_t* pdevid);

#endif
