// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef WEBUSB_H
#define WEBUSB_H

#include <stdint.h>
#include "controller.h"
#include "../ipblks/lms64c_proto.h"
#include "../device/device_bus.h"
#include "../ipblks/si2c.h"

struct sdr_call;

struct webusb_device;
typedef struct webusb_device webusb_device_t;

typedef int (*dif_set_uint_fn)(webusb_device_t* dev, const char* entity, uint64_t val);
typedef int (*dif_get_uint_fn)(webusb_device_t* dev, const char* entity, uint64_t *oval);

struct webusb_device {
    struct lowlevel_dev ll;
    struct webusb_ops* ops;
    uintptr_t param;
    sdr_type_t type_sdr;

    rpc_call_fn rpc_call;
    dif_set_uint_fn dif_set_uint;
    dif_get_uint_fn  dif_get_uint;

    pusdr_dms_t strms[2]; // 0 - RX, 1 - TX
};

#define MAX_SPI_BUS    2
#define MAX_I2C_BUS    2
#define MAX_VIRT_BUS   2

#define BUS_VIRT_IDX   0x10000
#define BUS_INVALID    0x0ffff

#define MAX_INTERRUPTS 32

struct webusb_device_ugen {
    struct webusb_device base;
#if 0
    // General config
    uint16_t base_spi[MAX_SPI_BUS];
    uint16_t base_i2c[MAX_I2C_BUS];
    uint16_t base_virt[MAX_VIRT_BUS];
#endif
    uint8_t event_spi[MAX_SPI_BUS];
    uint8_t event_i2c[MAX_I2C_BUS];

    device_bus_t db;

    struct i2c_cache i2cc[4 * DBMAX_I2C_BUSES];

    uint32_t ntfy_seqnum_exp;
};

struct webusb_device_lime {
    struct webusb_device base;

    uint32_t ft601_counter;

    proto_lms64c_t data_ctrl_out[1];
    proto_lms64c_t data_ctrl_in[1];
};
typedef struct webusb_device_lime webusb_device_lime_t;

typedef int (*flush_pipe_fn)(webusb_device_lime_t* d, unsigned char ep);
typedef int (*set_stream_pipe_fn)(webusb_device_lime_t* d, unsigned char ep, size_t size);

struct webusb_ops {
    int (*read_raw_ep)(uintptr_t param, uint8_t ep, unsigned maxbytes, void *data);
    int (*write_raw_ep)(uintptr_t param, uint8_t ep, unsigned bytes, const void *data);

    int (*log)(uintptr_t param, unsigned sevirity, const char* log);
};
typedef struct webusb_ops webusb_ops_t;

static inline
    int webusb_ll_generic_get(lldev_t dev, int generic_op, const char** pout)
{
    webusb_device_t* d = (webusb_device_t*)dev;

    switch(generic_op)
    {
    case LLGO_DEVICE_NAME: *pout = "webusb"; return 0;
    case LLGO_DEVICE_SDR_TYPE: *pout = (const char*)d->type_sdr; return 0;
    }

    return -EINVAL;
}

//// interface

int webusb_create(
        struct webusb_ops* ctx,
        uintptr_t param,
        unsigned loglevel,
        unsigned vidpid,
        pdm_dev_t* dmdev);

int webusb_process_rpc(pdm_dev_t dmdev,
        char* request,
        char* response,
        unsigned response_maxlen);

int webusb_debug_rpc(pdm_dev_t dmdev,
        char* request,
        char* response,
        unsigned response_maxlen);

int webusb_destroy(
        pdm_dev_t dmdev);

#endif
