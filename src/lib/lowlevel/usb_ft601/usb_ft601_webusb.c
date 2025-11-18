// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>

#include "usb_ft601_generic.h"
#include "libusb_vidpid_map.h"
#include "../../device/u3_limesdr/limesdr_ctrl.h"
#include "webusb_generic.h"

limesdr_dev_t* get_limesdr_dev(pdevice_t udev);

struct webusb_device_lime
{
    struct webusb_device base;
    usb_ft601_generic_t ft601_generic;
};

typedef struct webusb_device_lime webusb_device_lime_t;

usb_ft601_generic_t* get_ft601_generic(lldev_t dev)
{
    webusb_device_lime_t* d = (webusb_device_lime_t*)dev;
    return &d->ft601_generic;
}

static
    const char* webusb_lime_plugin_info_str(unsigned iparam) {
    switch (iparam) {
    case LLPI_NAME_STR: return "webusb lime";
    case LLPI_DESCRIPTION_STR: return "WebUsb Lime implementation for wasm";
    }
    return NULL;
}

static
    void _ctrl_pkt_dump(proto_lms64c_t* d, bool dir_rd)
{
    USDR_LOG("USBX", USDR_LOG_DEBUG, "%s { %02x %02x %02x %02x  %02x %02x %02x %02x | %02x %02x %02x %02x  %02x %02x %02x %02x  ... }\n",
             dir_rd ? "RD <= " : "WR => ",
             d->cmd, d->status, d->blockCount, d->periphID,
             d->reserved[0], d->reserved[1], d->reserved[2], d->reserved[3],
             d->data[0], d->data[1], d->data[2], d->data[3],
             d->data[4], d->data[5], d->data[6], d->data[7]);

}

static int webusb_io_write(lldev_t lld, uint8_t* wbuffer, int len, int* actual_len, int timeout)
{
    webusb_device_lime_t* d = (webusb_device_lime_t*)lld;
    int res = d->base.ops->write_raw_ep(d->base.param, EP_OUT_CONFIG, len, wbuffer);
    if(res < 0)
        return -EIO;

    *actual_len = res;
    return 0;
}

static
    int webusbft601_ctrl_out_pkt(lldev_t lld, unsigned pkt_szb, unsigned timeout_ms)
{
    webusb_device_lime_t* d = (webusb_device_lime_t*)lld;

    int res = d->base.ops->write_raw_ep(d->base.param, EP_OUT_CTRL, pkt_szb, d->ft601_generic.data_ctrl_out);
    if (res < 0 || res != pkt_szb)
        return -EIO;

    _ctrl_pkt_dump(d->ft601_generic.data_ctrl_out, false);
    return 0;
}

static
    int webusbft601_ctrl_in_pkt(lldev_t lld, unsigned pkt_szb, unsigned timeout_ms)
{
    webusb_device_lime_t* d = (webusb_device_lime_t*)lld;

    int res = d->base.ops->read_raw_ep(d->base.param, EP_IN_CTRL, pkt_szb, d->ft601_generic.data_ctrl_in);
    if (res < 0 || res != pkt_szb)
        return -EIO;

    _ctrl_pkt_dump(d->ft601_generic.data_ctrl_in, true);
    return 0;
}

#if 0
static
int usbft601_uram_rfic_reset(struct webusb_device_lime* dev)
{
    uint8_t cmd = LMS_RST_PULSE;
    return usbft601_uram_ctrl_transfer(dev, CMD_LMS7002_RST, &cmd, 1, NULL, 0, 3000);
}
#endif

static
int webusbft601_lock_wait(UNUSED lldev_t lld, UNUSED int64_t timeout)
{
    return 0;
}

static
void webusbft601_lock_post(UNUSED lldev_t lld)
{
}

static
int webusbft601_uram_async_start(UNUSED lldev_t lld)
{
    return 0;
}

const static
usb_ft601_io_ops_t s_io_ops = {
    //NULL,
    webusb_io_write,
    webusbft601_ctrl_out_pkt,
    webusbft601_ctrl_in_pkt,
    webusbft601_lock_wait,
    webusbft601_lock_post,
    webusbft601_uram_async_start,
};

static
    int webusb_ll_stream_initialize(lldev_t dev, subdev_t subdev, lowlevel_stream_params_t* params, stream_t* channel)
{
    int res = 0;
    const unsigned data_endpoint = (params->streamno == DEV_RX_STREAM_NO) ? EP_IN_DEFSTREAM : EP_OUT_DEFSTREAM;

    res = res ? res : ft601_flush_pipe(dev, data_endpoint);
    res = res ? res : ft601_set_stream_pipe(dev, data_endpoint, DATA_PACKET_SIZE);

    if (res)
        return res;

    USDR_LOG("USBX", USDR_LOG_ERROR, "webusb_ll_stream_initialize(streamno=%d)\n", params->streamno);
    *channel = params->streamno;
    return res;
}

static
    int webusb_ll_stream_deinitialize(lldev_t dev, subdev_t subdev, stream_t channel)
{
    return 0;
}

static
    int webusb_ll_destroy(lldev_t dev)
{
    // Destroy undelying dev
    if (dev->pdev) {
        dev->pdev->destroy(dev->pdev);
    }

    free(dev);
    return 0;
}

static
    struct lowlevel_ops s_webusb_ft601_ops = {
        &webusb_ll_generic_get,
        &usbft601_uram_ls_op,
        &webusb_ll_stream_initialize,
        &webusb_ll_stream_deinitialize,
        NULL,                       //recv_dma_wait,
        NULL,                       //recv_dma_release,
        NULL,                       //send_dma_get,
        NULL,                       //send_dma_commit,
        NULL,                       //recv_buf,
        NULL,                       //send_buf,
        NULL,                       //await,
        &webusb_ll_destroy
};

static
    uint32_t s_debug_lms7002m_last;

static
    int _dif_set_uint(struct webusb_device* d, const char* entity,
                  uint64_t val)
{
    webusb_device_lime_t *dev = (webusb_device_lime_t *)d;
    if (strcmp(entity, "/debug/hw/lms7002m/0/reg") != 0)
        return -EINVAL;

    limesdr_dev_t* lime_dev = get_limesdr_dev(dev->base.ll.pdev);

    unsigned chan = val >> 32;
    int res = lms7002m_mac_set(&lime_dev->base.lmsstate, chan);

    s_debug_lms7002m_last = ~0u;
    res = res ? res : lowlevel_spi_tr32(lime_dev->base.lmsstate.dev, 0,
                                        0, val & 0xffffffff, &s_debug_lms7002m_last);

    if(!res)
        USDR_LOG("XDEV", USDR_LOG_WARNING, "Debug LMS7/%d REG %08x => %08x\n",
                 chan, (unsigned)val,
                 s_debug_lms7002m_last);

    return res;
}

static
    int _dif_get_uint(struct webusb_device* d, const char* entity,
                  uint64_t *oval)
{
    if (strcmp(entity, "/debug/hw/lms7002m/0/reg") != 0)
        return -EINVAL;

    *oval = s_debug_lms7002m_last;
    return 0;
}


static
    int webusb_lime_plugin_create(unsigned pcount, const char** devparam,
                           const char** devval, lldev_t* odev,
                           unsigned vidpid, void* webops, uintptr_t param)
{
    int res = 0;
    lldev_t lldev = NULL;

    if(!vidpid) {
        return -EINVAL;
    }

    const uint16_t pid = 0x00000000FFFFFFFF & vidpid;
    const uint16_t vid = 0x00000000FFFFFFFF & (vidpid >> 16);
    const char* bus = "usbft601";

    int dev_idx = libusb_find_dev_index_ex(bus, pid, vid, s_known_devices, KNOWN_USB_DEVICES);
    if(dev_idx < 0) {
        USDR_LOG("WEBU", USDR_LOG_DEBUG, "WebUsb LIME device bus=%s vidpid=0x%08x not found/not supported!\n", bus, vidpid);
        return -ENOTSUP;
   }

    assert( *odev == NULL );
    webusb_device_lime_t *d = (webusb_device_lime_t *)malloc(sizeof(webusb_device_lime_t));
    if(d == NULL) {
        return -ENOMEM;
    }

    memset(d, 0, sizeof(webusb_device_lime_t));

    lldev = &d->base.ll;
    lldev->ops = &s_webusb_ft601_ops;

    d->ft601_generic.io_ops = s_io_ops;

    d->base.type_sdr = libusb_get_dev_sdrtype(dev_idx);
    d->base.devid = libusb_get_deviceid(dev_idx);
    d->base.param = param;
    d->base.ops = (webusb_ops_t*)webops;
    d->base.rpc_call = &generic_rpc_call;
    d->base.dif_set_uint = &_dif_set_uint;
    d->base.dif_get_uint = &_dif_get_uint;

    d->base.strms[0] = NULL;
    d->base.strms[1] = NULL;

    res = usbft601_uram_generic_create_and_init(lldev, pcount, devparam, devval, &d->base.devid);
    if(res) {
        goto usbinit_fail;
    }

    *odev = (lldev_t)d;
    return 0;

usbinit_fail:
    free(d);
    return res;
}

// Factory operations
static const
    struct lowlevel_plugin s_webusb_lime_plugin = {
        webusb_lime_plugin_info_str,
        webusb_generic_plugin_discovery,
        webusb_lime_plugin_create,
};

const struct lowlevel_plugin *usbft601_uram_register()
{
    USDR_LOG("WEBU", USDR_LOG_INFO, "WebUsb LIME Native support registered!\n");

    return &s_webusb_lime_plugin;
}
