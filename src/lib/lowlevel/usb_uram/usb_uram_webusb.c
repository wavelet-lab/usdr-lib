// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>

#include "usb_uram_generic.h"
#include "libusb_vidpid_map.h"
#include "webusb_generic.h"
#include "../../webusb/controller.h"
#include "../../device/generic_usdr/generic_regs.h"
#include "../../ipblks/si2c.h"
#include "../../device/mp_lm7_1_gps/xsdr_ctrl.h"

xsdr_dev_t* get_xsdr_dev(pdevice_t udev);

static
    const char* webusb_uram_plugin_info_str(unsigned iparam) {
    switch (iparam) {
    case LLPI_NAME_STR: return "webusb usdr/xsdr";
    case LLPI_DESCRIPTION_STR: return "WebUsb USDR/XSDR implementation for wasm";
    }
    return NULL;
}

static
    int webusb_await_event(struct webusb_device_ugen* wbd, int num, uint32_t* data)
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

        if (wbd->ntfy_seqnum_exp != seqnum) {
            USDR_LOG("USBX", USDR_LOG_ERROR, "Notification exp seqnum %04x, got %04x: %08x hdr\n",
                     wbd->ntfy_seqnum_exp, seqnum, header);
            wbd->ntfy_seqnum_exp = seqnum;
        }
        wbd->ntfy_seqnum_exp++;
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
    int libusb_websdr_io_write(struct webusb_device_ugen* dev, unsigned addr, unsigned dwcnt, const uint32_t* data)
{
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

int libusb_websdr_io_read(struct webusb_device_ugen* dev, unsigned addr, unsigned dwcnt, uint32_t *data)
{
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

// IO functions
static
    int webusb_ll_ls_op(lldev_t dev, subdev_t subdev,
                    unsigned ls_op, lsopaddr_t ls_op_addr,
                    size_t meminsz, void* pin, size_t memoutsz,
                    const void* pout)
{
    struct webusb_device_ugen* wbd = (struct webusb_device_ugen*)dev;
    int res;
    uint32_t dummy;

    switch (ls_op) {
    case USDR_LSOP_HWREG:
        if (meminsz) {
            res = libusb_websdr_io_read(wbd, ls_op_addr, meminsz / 4, (uint32_t*)pin);
            if (res)
                return res;
        }
        if (memoutsz) {
            unsigned vidx = ls_op_addr / BUS_VIRT_IDX;
            if (vidx > 0) {
                if (vidx > MAX_VIRT_BUS)
                    return -EINVAL;
                if (memoutsz != 4)
                    return -EINVAL;
                if (wbd->base_virt[vidx - 1] == BUS_INVALID)
                    return -EINVAL;

                uint32_t data[2] = {
                    ls_op_addr % BUS_VIRT_IDX,
                    *((const uint32_t*)pout),
                };
                res = libusb_websdr_io_write(wbd, wbd->base_virt[vidx - 1], 2, data);
            } else {
                res = libusb_websdr_io_write(wbd, ls_op_addr, memoutsz / 4, (uint32_t*)pout);
            }
            if (res)
                return res;
        }
        return 0;
    case USDR_LSOP_SPI:
        if (((meminsz != 4) && (meminsz != 0)) || (memoutsz != 4))
            return -EINVAL;
        if (ls_op_addr >= MAX_SPI_BUS)
            return -EINVAL;
        if (wbd->base_spi[ls_op_addr] == BUS_INVALID)
            return -EINVAL;

        res = libusb_websdr_io_write(wbd, wbd->base_spi[ls_op_addr], memoutsz / 4,
                                     (const uint32_t*)pout);
        if (res)
            return res;

        res = webusb_await_event(wbd, wbd->event_spi[ls_op_addr],
                                 (meminsz != 0) ? (uint32_t*)pin : &dummy);
        if (res)
            return res;

        return 0;
    case USDR_LSOP_I2C_DEV: {
        uint32_t i2ccmd, data = 0;
        const uint8_t* dd = (const uint8_t*)pout;
        uint8_t* di = (uint8_t*)pin;
        uint8_t busno = (ls_op_addr >> 8);

        if (busno >= MAX_I2C_BUS)
            return -EINVAL;
        if (wbd->base_i2c[busno] == BUS_INVALID)
            return -EINVAL;

        res = si2c_make_ctrl_reg(ls_op_addr, dd, memoutsz, meminsz, &i2ccmd);
        if (res)
            return res;

        res = libusb_websdr_io_write(wbd, wbd->base_i2c[busno], 1, &i2ccmd);
        if (res)
            return res;
        if (meminsz == 0)
            return 0;

        // Only when we need readback
        res = webusb_await_event(wbd, wbd->event_i2c[busno],
                                 &data);
        if (res)
            return res;

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

        return 0;
    }
    case USDR_LSOP_URAM:
        return -EINVAL;
    default:
        break;
    }

    return -EINVAL;
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
    struct webusb_device_ugen *dev = (struct webusb_device_ugen *)d;
    if (strcmp(entity, "/debug/hw/lms7002m/0/reg") != 0)
        return -EINVAL;
    if (dev->base.type_sdr != SDR_XSDR)
        return -EINVAL;

    unsigned chan = val >> 32;
    xsdr_dev_t* xsdrdev = get_xsdr_dev(dev->base.ll.pdev);
    int res = lms7002m_mac_set(&xsdrdev->base.lmsstate, chan);

    s_debug_lms7002m_last = ~0u;
    res = res ? res : lowlevel_spi_tr32(xsdrdev->base.lmsstate.dev, 0,
                                        SPI_LMS7, val & 0xffffffff, &s_debug_lms7002m_last);
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
    struct webusb_device_ugen *dev = (struct webusb_device_ugen *)d;
    if (dev->base.type_sdr != SDR_XSDR)
        return -EINVAL;

    if (strcmp(entity, "/debug/hw/lms7002m/0/reg") != 0)
        return -EINVAL;

    *oval = s_debug_lms7002m_last;
    return 0;
}

static
    int webusb_uram_plugin_create(unsigned pcount, const char** devparam,
                           const char** devval, lldev_t* odev,
                           unsigned vidpid, void* webops, uintptr_t param)
{
    int res = 0;
    lldev_t lldev = NULL;
    unsigned hwid = 0;

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
    struct webusb_device_ugen *d = (struct webusb_device_ugen *)malloc(sizeof(struct webusb_device_ugen));
    if(d == NULL) {
        return -ENOMEM;
    }

    memset(d, 0, sizeof(struct webusb_device_ugen));

    lldev = &d->base.ll;
    lldev->ops = &s_webusb_uram_ops;

    d->base.type_sdr = libusb_get_dev_sdrtype(dev_idx);
    d->base.param = param;
    d->base.ops = (webusb_ops_t*)webops;
    d->base.rpc_call = &generic_rpc_call;
    d->base.dif_set_uint = &_dif_set_uint;
    d->base.dif_get_uint = &_dif_get_uint;

    d->base.strms[0] = NULL;
    d->base.strms[1] = NULL;

    // Init buses cores
    d->base_i2c[0] = M2PCI_REG_I2C;
    d->base_i2c[1] = BUS_INVALID;
    d->base_spi[0] = M2PCI_REG_SPI0;
    d->base_spi[1] = BUS_INVALID;
    d->base_virt[0] = M2PCI_REG_WR_BADDR;
    d->base_virt[1] = BUS_INVALID;

    d->event_spi[0] = M2PCI_INT_SPI_0;
    d->event_spi[1] = 0;
    d->event_i2c[0] = M2PCI_INT_I2C_0;
    d->event_i2c[1] = 0;

    d->ntfy_seqnum_exp = 0;

    res = usdr_device_create(lldev, libusb_get_deviceid(dev_idx));
    if (res) {
        USDR_LOG("WEBU", USDR_LOG_ERROR, "Unable to create WebUsb device, error %d\n", res);
        goto usbinit_fail;
    }

    res = lowlevel_reg_rd32(lldev, 0, 16 + (IGPI_HWID / 4), &hwid);

    if (res)
        goto usbinit_fail;

    if (hwid & 0x40000000) {
        const unsigned REG_WR_PNTFY_CFG = 8;
        const unsigned REG_WR_PNTFY_ACK = 9;

        res = res ? res : lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_CFG, 0xdeadb000);
        res = res ? res : lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_CFG, 0xeeadb001);
        res = res ? res : lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_CFG, 0xfeadb002);
        res = res ? res : lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_CFG, 0xaeadb003);
        res = res ? res : lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_CFG, 0xbeadb004);
        res = res ? res : lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_CFG, 0xceadb005);
        res = res ? res : lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_CFG, 0x9eadb006);
        res = res ? res : lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_CFG, 0x8eadb007);

        for (unsigned i = 0; i < 8; i++) {
            res = res ? res : lowlevel_reg_wr32(lldev, 0, M2PCI_REG_INT,  i | (i << 8) | (1 << 16) | (7 << 20));
            res = res ? res : lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_ACK, 0 | i << 16);
        }

        // IGPO_FRONT activates tran_usb_active to route interrupts to NTFY endpoint
        res = res ? res : lowlevel_reg_wr32(lldev, 0, 0, (15u << 24) | (0x80));
        if (res) {
            USDR_LOG("WEBU", USDR_LOG_ERROR,
                     "Unable to set stream routing, error %d\n", res);
            goto usbinit_fail;
        }

        res = lowlevel_reg_wr32(lldev, 0, M2PCI_REG_INT, (1 << M2PCI_INT_SPI_0) | (1 << M2PCI_INT_I2C_0));
        if (res) {
            goto usbinit_fail;
        }
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
