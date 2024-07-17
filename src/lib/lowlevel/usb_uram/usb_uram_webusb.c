// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>

#include "usb_uram_generic.h"
#include "libusb_vidpid_map.h"
#include "webusb_generic.h"
#include "../../ipblks/si2c.h"

struct webusb_device_uram
{
    struct webusb_device base;
    usb_uram_generic_t uram_generic;
};

static
    const char* webusb_uram_plugin_info_str(unsigned iparam) {
    switch (iparam) {
    case LLPI_NAME_STR: return "webusb usdr/xsdr";
    case LLPI_DESCRIPTION_STR: return "WebUsb USDR/XSDR implementation for wasm";
    }
    return NULL;
}

static
    int webusb_await_event(struct webusb_device_uram* wbd, int num, uint32_t* data)
{
    uint32_t buffer[16];
    int res;

    //res = wbd->base.ops->await_event(wbd->base.param, 16, buffer);
    res = wbd->base.ops->read_raw_ep(wbd->base.param, EP_CSR_NTFY | 0x80, 16 * 4, buffer);
    if (res < 0) {
        USDR_LOG("USBX", USDR_LOG_ERROR, "await_event returned %d\n", res);
        return res;
    }

    unsigned i;
    unsigned packet_cnt = res / 4;
    res = -ENOENT;

    for (i = 0; i < packet_cnt; i++) {
        uint32_t header = buffer[i];
        unsigned seqnum = header >> 16;
        unsigned event = header & 0x3f;
        unsigned blen = ((header >> 12) & 0xf);

        if (wbd->uram_generic.ntfy_seqnum_exp != seqnum) {
            USDR_LOG("USBX", USDR_LOG_ERROR, "Notification exp seqnum %04x, got %04x: %08x hdr\n",
                     wbd->uram_generic.ntfy_seqnum_exp, seqnum, header);
            wbd->uram_generic.ntfy_seqnum_exp = seqnum;
        }
        wbd->uram_generic.ntfy_seqnum_exp++;
        if (event >= MAX_INTERRUPTS) {
            USDR_LOG("USBX", USDR_LOG_ERROR, "Broken notification, event %d: %08x hdr\n", event, header);
            i += blen + 1;
            continue;
        }

        if (blen == 0 && ((i + 1) < packet_cnt)) {
            USDR_LOG("USBX", USDR_LOG_INFO, "Got notification seq %04x event %d => %08x\n",
                     seqnum, event, buffer[i + 1]);

            if (num == event) {
                *data = buffer[++i];
                res = 0;
            }
        } else {
            USDR_LOG("USBX", USDR_LOG_ERROR, "TODO!!!!!!!!!!!!!!!\n");
        }
    }

    return res;
}

static
    int libusb_websdr_io_write(lldev_t d, unsigned addr, const uint32_t* data, unsigned dwcnt, UNUSED int timeout)
{
    struct webusb_device_uram* dev = (struct webusb_device_uram*)d;
    uint32_t pkt[65]; // header + 256b payload
    int res;

    //fprintf(stderr, "libusb_websdr_io_write[%06x+%d,%08x]:\n", addr, dwcnt, *data);

    if (dwcnt == 0 || dwcnt >= 64)
        return -EINVAL;

    pkt[0] = (((dwcnt - 1) & 0x3f) << 16) | (addr & 0xffff);
    memcpy(&pkt[1], data, dwcnt * 4);

    res = dev->base.ops->write_raw_ep(dev->base.param,
                                      EP_CSR_OUT | 0x00,
                                      (dwcnt + 1) * 4,
                                      (unsigned char*)&pkt);
    if (res < 0)
        return res;
    if (res != (dwcnt + 1) * 4)
        return -EIO;

    return 0;
}

static
    int libusb_websdr_io_read(lldev_t d, unsigned addr, uint32_t *data, unsigned dwcnt, UNUSED int timeout)
{
    struct webusb_device_uram* dev = (struct webusb_device_uram*)d;
    int res;
    uint32_t cmd = (((dwcnt - 1) & 0x3f) << 16) | (addr & 0xffff) | (0xC0000000);

    //fprintf(stderr, "libusb_websdr_io_read[%06x]:\n", addr);

    res = dev->base.ops->write_raw_ep(dev->base.param,
                                      EP_CSR_OUT | 0x00,
                                      4,
                                      (unsigned char*)&cmd);


    if (res < 0)
        return res;
    if (res != 4)
        return -EIO;

    res =  dev->base.ops->read_raw_ep(dev->base.param,
                                     EP_CSR_IN | 0x80,
                                     4 * dwcnt,
                                     (unsigned char*)data);

    if (res < 0)
        return res;
    if (res != 4 * dwcnt)
        return -EIO;

    return 0;
}

static int libusb_websdr_read_bus(lldev_t dev, unsigned interrupt_number, UNUSED unsigned reg, size_t meminsz, void* pin)
{
    uint32_t dummy;
    struct webusb_device_uram* d = (struct webusb_device_uram*)dev;

    return webusb_await_event(d, interrupt_number, (meminsz != 0) ? (uint32_t*)pin : &dummy);
}

static
    int webusb_ll_stream_initialize(lldev_t dev, subdev_t subdev, lowlevel_stream_params_t* params, stream_t* channel)
{
    *channel = params->streamno;
    return 0;
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
struct lowlevel_ops s_webusb_uram_ops = {
    &webusb_ll_generic_get,
    &usb_uram_ls_op,
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
    int _dif_set_uint(struct webusb_device* d, const char* entity,
                  uint64_t val)
{
    return -EINVAL;
}

static
    int _dif_get_uint(struct webusb_device* d, const char* entity,
                  uint64_t *oval)
{
    return -EINVAL;
}


static struct usb_uram_io_ops s_io_ops = {
    libusb_websdr_io_read,
    libusb_websdr_io_write,
    libusb_websdr_read_bus
};

static const char* get_dev_name(lldev_t dev)
{
    const char* name;
    int res = webusb_ll_generic_get(dev, LLGO_DEVICE_NAME, &name);
    if(res)
        return "unknown";
    return name;
}

static device_id_t get_dev_id(lldev_t dev)
{
    struct webusb_device_uram* d = (struct webusb_device_uram*)dev;
    return d->base.devid;
}

const static
usb_uram_dev_ops_t s_dev_ops = {
    get_dev_name,
    get_dev_id
};

static
    int webusb_uram_plugin_create(unsigned pcount, const char** devparam,
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
    const char* bus = "usb";

    int dev_idx = libusb_find_dev_index_ex(bus, pid, vid, s_known_devices, KNOWN_USB_DEVICES);
    if(dev_idx < 0) {
        USDR_LOG("WEBU", USDR_LOG_DEBUG, "WebUsb USDR/XSDR device bus=%s vidpid=0x%08x not found/not supported!\n", bus, vidpid);
        return -ENOTSUP;
   }

    assert( *odev == NULL );
    struct webusb_device_uram *d = (struct webusb_device_uram *)malloc(sizeof(struct webusb_device_uram));
    if(d == NULL) {
        return -ENOMEM;
    }

    memset(d, 0, sizeof(struct webusb_device_uram));

    lldev = &d->base.ll;
    lldev->ops = &s_webusb_uram_ops;

    d->base.type_sdr = libusb_get_dev_sdrtype(dev_idx);
    d->base.devid = libusb_get_deviceid(dev_idx);
    d->base.param = param;
    d->base.ops = (webusb_ops_t*)webops;
    d->base.rpc_call = &generic_rpc_call;
    d->base.dif_set_uint = &_dif_set_uint;
    d->base.dif_get_uint = &_dif_get_uint;

    d->base.strms[0] = NULL;
    d->base.strms[1] = NULL;

    d->uram_generic.ntfy_seqnum_exp = 0;
    d->uram_generic.io_ops = s_io_ops;
    d->uram_generic.dev_ops = s_dev_ops;

    res = usb_uram_generic_create_and_init(lldev, pcount, devparam, devval);
    if(res)
        goto usbinit_fail;

    *odev = (lldev_t)d;
    return 0;

usbinit_fail:
    free(d);
    return res;
}

usb_uram_generic_t* get_uram_generic(lldev_t dev)
{
    struct webusb_device_uram* d = (struct webusb_device_uram*)dev;
    return &d->uram_generic;
}

// Factory operations
static const
    struct lowlevel_plugin s_webusb_uram_plugin = {
        webusb_uram_plugin_info_str,
        webusb_generic_plugin_discovery,
        webusb_uram_plugin_create,
};

const struct lowlevel_plugin *usb_uram_register()
{
    USDR_LOG("WEBU", USDR_LOG_INFO, "WebUsb USDR/XSDR Native support registered!\n");

    return &s_webusb_uram_plugin;
}
