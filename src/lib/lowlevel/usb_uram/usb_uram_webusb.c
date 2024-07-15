// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>

#include "usb_uram_generic.h"
#include "libusb_vidpid_map.h"
#include "webusb_generic.h"
#include "../../device/generic_usdr/generic_regs.h"
#include "../../ipblks/si2c.h"

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

static int webusb_uram_reg_out(struct webusb_device_ugen* dev, unsigned reg,
                            uint32_t outval)
{
    int res = libusb_websdr_io_write(dev, reg, 1, &outval);

    USDR_LOG("WEBU", USDR_LOG_DEBUG, "%s: Write [%04x] = %08x (%d)\n",
             "WebUsb", reg, outval, res);
    return res;
}

static int webusb_uram_reg_in(struct webusb_device_ugen* dev, unsigned reg,
                           uint32_t *pinval)
{
    uint32_t inval;
    int	res = libusb_websdr_io_read(dev, reg, 1, &inval);

    USDR_LOG("WEBU", USDR_LOG_DEBUG, "%s: Read  [%04x] = %08x (%d)\n",
             "WebUsb", reg, inval, res);
    *pinval = inval;
    return res;
}

static int webusb_uram_reg_out_n(struct webusb_device_ugen* dev, unsigned reg,
                              const uint32_t *outval, const unsigned dwcnt)
{
    int res = libusb_websdr_io_write(dev, reg, dwcnt, outval);
    USDR_LOG("WEBU", USDR_LOG_DEBUG, "%s: WriteArray [%04x + %d] (%d)\n",
             "WebUsb", reg, dwcnt, res);
    return res;
}

static int webusb_uram_reg_in_n(struct webusb_device_ugen* dev, unsigned reg,
                             uint32_t *pinval, const unsigned dwcnt)
{
    unsigned off = 0;
    unsigned rem = dwcnt;
    unsigned sz = rem;

    for (; rem != 0; ) {
        if (sz > 256 / 4)
            sz = 256 / 4;

        int	res = libusb_websdr_io_read(dev, reg + off, sz, pinval + off);
        USDR_LOG("WEBU", USDR_LOG_DEBUG, "%s: ReadArray [%04x + %d] (%d)\n",
                 "WebUsb", reg, sz, res);
        if (res)
            return res;

        off += sz;
        rem -= sz;
    }

    return 0;
}



static
    int webusb_uram_reg_op(struct webusb_device_ugen* d, unsigned ls_op_addr,
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
                res = webusb_uram_reg_out(d, d->db.idxreg_base[k],
                                       ls_op_addr - d->db.idxreg_virt_base[k] + i);
                if (res)
                    return res;

                if (i < memoutsz / 4) {
                    res = webusb_uram_reg_out(d, d->db.idxreg_base[k] + 1, outa[i]);
                    if (res)
                        return res;
                }

                if (i < meminsz / 4) {
                    res = webusb_uram_reg_in(d, d->db.idxreg_base[k] + 1, &ina[i]);
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
        res = webusb_uram_reg_out_n(d, ls_op_addr, outa, memoutsz / 4);
        if (res)
            return res;
    } else if (memoutsz == 4) {
        res = webusb_uram_reg_out(d, ls_op_addr, outa[0]);
        if (res)
            return res;
    }

    if (meminsz > 4) {
        res = webusb_uram_reg_in_n(d, ls_op_addr, ina, meminsz / 4);
        if (res)
            return res;
    } else if (meminsz == 4) {
        res = webusb_uram_reg_in(d, ls_op_addr, ina);
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
#if 0
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
#endif
    {
        uint32_t* ina = (uint32_t*)pin;
        const uint32_t* outa = (const uint32_t*)pout;

        return webusb_uram_reg_op(wbd, ls_op_addr, ina, meminsz, outa, memoutsz);
    }
    case USDR_LSOP_SPI:
        if (ls_op_addr >= wbd->db.spi_count)
            return -EINVAL;
        if (wbd->db.spi_core[ls_op_addr] != SPI_CORE_32W)
            return -EINVAL;

        if (((meminsz != 4) && (meminsz != 0)) || (memoutsz != 4))
            return -EINVAL;
        if (ls_op_addr >= MAX_SPI_BUS)
            return -EINVAL;
        if (wbd->db.spi_base[ls_op_addr] == BUS_INVALID)
            return -EINVAL;

        res = libusb_websdr_io_write(wbd, wbd->db.spi_base[ls_op_addr], memoutsz / 4,
                                     (const uint32_t*)pout);
        if (res)
            return res;

        res = webusb_await_event(wbd, wbd->event_spi[ls_op_addr],
                                 (meminsz != 0) ? (uint32_t*)pin : &dummy);
        if (res)
            return res;

        return 0;
    case USDR_LSOP_I2C_DEV: {
        uint32_t i2ccmd[2], data = 0;
        const uint8_t* dd = (const uint8_t*)pout;
        uint8_t* di = (uint8_t*)pin;
        uint8_t instance_no = LSOP_I2C_INSTANCE(ls_op_addr);
        uint8_t busno = LSOP_I2C_BUSNO(ls_op_addr);
        uint8_t i2caddr = LSOP_I2C_ADDR(ls_op_addr);
        unsigned lidx;

        if (instance_no >= wbd->db.i2c_count)
            return -EINVAL;
        if (wbd->db.i2c_core[instance_no] != I2C_CORE_AUTO_LUTUPD)
            return -EINVAL;

        if (busno >= 1)
            return -EINVAL;
#if 0
        if (wbd->base_i2c[busno] == BUS_INVALID)
            return -EINVAL;
        res = si2c_make_ctrl_reg(ls_op_addr, dd, memoutsz, meminsz, &i2ccmd);
        if (res)
            return res;
#endif

        lidx = si2c_update_lut_idx(&wbd->i2cc[4 * instance_no], i2caddr, busno);
        i2ccmd[0] = si2c_get_lut(&wbd->i2cc[4 * instance_no]);
        res = si2c_make_ctrl_reg(lidx, dd, memoutsz, meminsz, &i2ccmd[1]);
        if (res)
            return res;

        USDR_LOG("WEBU", USDR_LOG_DEBUG, "%s: I2C[%d.%d.%02x] LUT:CMD %08x.%08x\n",
                 "WebUsb", instance_no, busno, i2caddr, i2ccmd[0], i2ccmd[1]);

        res = libusb_websdr_io_write(wbd, wbd->db.i2c_base[instance_no] - 1, 2, i2ccmd);
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

#if 0
    // Init buses cores
    d->base_i2c[0] = M2PCI_REG_I2C;
    d->base_i2c[1] = BUS_INVALID;
    d->base_spi[0] = M2PCI_REG_SPI0;
    d->base_spi[1] = BUS_INVALID;
    d->base_virt[0] = M2PCI_REG_WR_BADDR;
    d->base_virt[1] = BUS_INVALID;
#endif

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

    res = device_bus_init(lldev->pdev, &d->db);
    if (res) {
        USDR_LOG("WEBU", USDR_LOG_ERROR,
                 "Unable to initialize bus parameters for the device %s!\n", "WebUsb");

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
