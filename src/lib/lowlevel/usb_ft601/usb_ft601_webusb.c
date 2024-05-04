// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>

#include "usb_ft601_generic.h"
#include "libusb_vidpid_map.h"
#include "../../device/u3_limesdr/limesdr_ctrl.h"
#include "webusb_generic.h"
#include "../../webusb/controller.h"

limesdr_dev_t* get_limesdr_dev(pdevice_t udev);

enum {
    EP_OUT_CONFIG      = 0 | 1,
    EP_OUT_CTRL        = 0 | 2,
    EP_OUT_DEFSTREAM   = 0 | 3,

    EP_IN_CONFIG       = 0x80 | 1,
    EP_IN_CTRL         = 0x80 | 2,
    EP_IN_DEFSTREAM    = 0x80 | 3,
};

enum {
    CTRL_SIZE = 64,
    MAX_CTRL_BURST = 1,
};

int _ft601_cmd(lldev_t lld, uint8_t ep, uint8_t cmd, uint32_t param)
{
    uint8_t wbuffer[20];
    int res;
    webusb_device_lime_t* d = (webusb_device_lime_t*)lld;

    fill_ft601_cmd(wbuffer, ++d->ft601_counter, ep, 0x00, 0);
    res = d->base.ops->write_raw_ep(d->base.param, EP_OUT_CONFIG, 20, wbuffer);
    if (res < 0 || res != 20)
        return -EIO;

    fill_ft601_cmd(wbuffer, ++d->ft601_counter, ep, cmd, param);
    res = d->base.ops->write_raw_ep(d->base.param, EP_OUT_CONFIG, 20, wbuffer);
    if (res < 0 || res != 20)
        return -EIO;

    return 0;
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

static
    int usbft601_ctrl_out_pkt(struct webusb_device_lime* dev, unsigned pkt_szb, unsigned timeout_ms)
{
    int res = dev->base.ops->write_raw_ep(dev->base.param, EP_OUT_CTRL, pkt_szb, dev->data_ctrl_out);
    if (res < 0 || res != pkt_szb)
        return -EIO;

    _ctrl_pkt_dump(dev->data_ctrl_out, false);
    return 0;
}

static
    int usbft601_ctrl_in_pkt(struct webusb_device_lime* dev, unsigned pkt_szb, unsigned timeout_ms)
{
    int res = dev->base.ops->read_raw_ep(dev->base.param, EP_IN_CTRL, pkt_szb, dev->data_ctrl_in);
    if (res < 0 || res != pkt_szb)
        return -EIO;

    _ctrl_pkt_dump(dev->data_ctrl_in, true);
    return 0;
}

int usbft601_ctrl_transfer(struct webusb_device_lime* dev,
                           uint8_t cmd, const uint8_t* data_in, unsigned length_in,
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

    res = usbft601_ctrl_out_pkt(dev, pkt_szb, timeout_ms);
    if (res) {
        return res;
    }

    res = usbft601_ctrl_in_pkt(dev, pkt_szb, timeout_ms);
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
    return res;
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
    int webusb_ll_ls_op(lldev_t dev, subdev_t subdev,
                    unsigned ls_op, lsopaddr_t ls_op_addr,
                    size_t meminsz, void* pin, size_t memoutsz,
                    const void* pout)
{
    int res = 0;
    webusb_device_lime_t* d = (webusb_device_lime_t*)dev;
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

            res = usbft601_ctrl_transfer(d, CMD_BRDSPI_RD, (const uint8_t* )tmpbuf_out, meminsz, (uint8_t*)pin, meminsz, timeout_ms);
        } else if (pin == NULL || meminsz == 0) {
            if (memoutsz * 2 > sizeof(tmpbuf_out))
                return -E2BIG;

            // Rgister write
            const uint16_t* out_s = (const uint16_t* )pout;
            for (unsigned i = 0; i < memoutsz / 2; i++) {
                tmpbuf_out[2 * i + 1] = ls_op_addr + i;
                tmpbuf_out[2 * i + 0] = out_s[i];
            }

            res = usbft601_ctrl_transfer(d, CMD_BRDSPI_WR, (const uint8_t* )tmpbuf_out, memoutsz * 2, (uint8_t*)pin, meminsz, timeout_ms);
        } else {
            return -EINVAL;
        }
        break;

    case USDR_LSOP_SPI:
        // TODO split to RD / WR packets
        if (pin == NULL || meminsz == 0 || ((*(const uint32_t*)pout) & 0x80000000) != 0) {
            res = usbft601_ctrl_transfer(d, CMD_LMS7002_WR, (const uint8_t*)pout, memoutsz, (uint8_t*)pin, meminsz, timeout_ms);
        } else {
            for (unsigned k = 0; k < memoutsz / 4; k++) {
                const uint32_t* pz = (const uint32_t*)((uint8_t*)pout + 4 * k);
                tmpbuf_out[k] = *pz >> 16;
            }
            res = usbft601_ctrl_transfer(d, CMD_LMS7002_RD, (const uint8_t* )tmpbuf_out /*pout*/, memoutsz / 2, (uint8_t*)pin, meminsz, timeout_ms);
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
    struct lowlevel_ops s_webusb_ft601_ops = {
        &webusb_ll_generic_get,
        &webusb_ll_ls_op,
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

    d->base.type_sdr = libusb_get_dev_sdrtype(dev_idx);
    d->base.param = param;
    d->base.ops = (webusb_ops_t*)webops;
    d->base.rpc_call = &generic_rpc_call;
    d->base.dif_set_uint = &_dif_set_uint;
    d->base.dif_get_uint = &_dif_get_uint;

    d->base.strms[0] = NULL;
    d->base.strms[1] = NULL;

    res = res ? res : ft601_flush_pipe(lldev, EP_IN_CTRL);  //clear ctrl ep rx buffer
    res = res ? res : ft601_set_stream_pipe(lldev, EP_IN_CTRL, CTRL_SIZE);
    res = res ? res : ft601_set_stream_pipe(lldev, EP_OUT_CTRL, CTRL_SIZE);

    if (res) {
        goto usbinit_fail;
    }

    res = usdr_device_create(lldev, libusb_get_deviceid(dev_idx));
    if (res) {
        USDR_LOG("WEBU", USDR_LOG_ERROR, "Unable to create WebUsb device, error %d\n", res);
        goto usbinit_fail;
    }

    // Device initialization
    res = lldev->pdev->initialize(lldev->pdev, pcount, devparam, devval);
    if (res) {
        USDR_LOG("WEBU", USDR_LOG_ERROR,
                 "Unable to initialize device, error %d\n", res);
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
