// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef _WEBUSB_GENERIC_H
#define _WEBUSB_GENERIC_H

#include <errno.h>
#include "usdr_lowlevel.h"
#include "controller.h"


struct webusb_device;
typedef struct webusb_device webusb_device_t;

typedef int (*dif_set_uint_fn)(webusb_device_t* dev, const char* entity, uint64_t val);
typedef int (*dif_get_uint_fn)(webusb_device_t* dev, const char* entity, uint64_t *oval);

struct webusb_device {
    struct lowlevel_dev ll;
    struct webusb_ops* ops;
    uintptr_t param;

    device_id_t devid;
    sdr_type_t type_sdr;

    rpc_call_fn rpc_call;
    dif_set_uint_fn dif_set_uint;
    dif_get_uint_fn  dif_get_uint;

    pusdr_dms_t strms[2]; // 0 - RX, 1 - TX
};

struct webusb_ops {
    int (*read_raw_ep)(uintptr_t param, uint8_t ep, unsigned maxbytes, void *data);
    int (*write_raw_ep)(uintptr_t param, uint8_t ep, unsigned bytes, const void *data);

    int (*log)(uintptr_t param, unsigned sevirity, const char* log);
};
typedef struct webusb_ops webusb_ops_t;


static inline
    int webusb_generic_plugin_discovery(unsigned pcount, const char** filterparams,
                                 const char** filtervals,
                                 unsigned maxbuf, char* outarray)
{
    return -ENOTSUP;
}

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

#endif  //_WEBUSB_GENERIC_H
