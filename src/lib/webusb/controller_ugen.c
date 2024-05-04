// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "controller.h"
#define NO_IGPO

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "base64.h"

#include "../device/generic_usdr/generic_regs.h"

#if 0
#include "../ipblks/streams/dma_rx_32.h"
#include "../ipblks/streams/sfe_rx_4.h"
#include "../ipblks/streams/dma_tx_32.h"
#include "../ipblks/streams/sfe_tx_4.h"
#endif

#include "../ipblks/streams/streams.h"
#include "../ipblks/espi_flash.h"
#include "../ipblks/xlnx_bitstream.h"

#if 0
// TODO: move away core configuration
static const unsigned fe_base = VIRT_CFG_SFX_BASE + 256;
static const unsigned fe_fifobsz = 0x10000;
static const unsigned sx_cfg_base = VIRT_CFG_SFX_BASE;
static const unsigned sx_base = M2PCI_REG_WR_RXDMA_CONFIRM;

static const unsigned REG_WR_PNTFY_CFG = 8;
static const unsigned REG_WR_PNTFY_ACK = 9;
#endif

#if 0
int ctrl_sdr_rxstreaming_prepare(
        lldev_t lldev,
        unsigned chmask,
        unsigned pktsyms,
        const char* wirefmt,
        unsigned* block_size,
        unsigned* burstcnt,
        unsigned flags)
{
    int res;
    if (wirefmt == NULL) {
        wirefmt = SFMT_CI16;
    }
    if (pktsyms == 0) {
        pktsyms = 4096;
    }
    if (chmask == 0) {
        chmask = 0x1;
    }

    res = dma_rx32_reset(lldev, 0, sx_base, sx_cfg_base);
    if (res)
        return res;

    // FE & DMA enngine configuration
    struct stream_config sc;
    struct fifo_config fc;

    sc.burstspblk = 0;
    sc.chmsk = chmask;
    sc.sfmt = wirefmt;
    sc.spburst = pktsyms;

    res = sfe_rx4_check_format(&sc);
    if (res) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "Unsupported wire format '%s' by the core\n",
                 sc.sfmt);
        return res;
    }

    res = sfe_rx4_configure(lldev, 0, fe_base, fe_fifobsz, &sc, &fc);
    if (res)
        return res;

    res = dma_rx32_configure(lldev, 0, sx_cfg_base, &fc, (CTLXSDR_RXEXTSTATTX & flags) ? ENABLE_TX_STATS : 0);
    if (res)
        return res;

    *block_size = fc.bpb * fc.burstspblk;
    *burstcnt = fc.burstspblk;
    return 0;
}
#endif
#if 0
int ctrl_sdr_rxstreaming_startstop(lldev_t lldev, bool start)
{
    int res;

    // RX
    res = lowlevel_reg_wr32(lldev, 0,
                            sx_base + 1, start ? 1 : 0);
    if (res)
        return res;

    res = sfe_rx4_startstop(lldev, 0,
                            fe_base, 0, start);
    if (res)
        return res;


    // TX (SISO)
    res = lowlevel_reg_wr32(lldev, 0,
                            M2PCI_REG_WR_TXDMA_COMB, start ? ((1<<3) | 3) : ((1<<7)) | 0);
    if (res)
        return res;


    return 0;
}
#endif

static inline int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, 0, 16 + (bank / 4), data);
}

#if 0
static inline int dev_gpo_set(lldev_t dev, unsigned bank, unsigned data)
{
    return lowlevel_reg_wr32(dev, 0, 0, ((bank & 0x7f) << 24) | (data & 0xff));
}
#endif

static unsigned s_op_flash_length = 0;
static unsigned s_op_flash_offset = 0;
static unsigned s_op_flash_golden = 0;
static unsigned s_op_flash_validated = 0;
static uint8_t  s_op_flash_header[256];

static int check_firmware_header(lldev_t dev, const char* data, bool update_golden)
{
    int res;
    uint32_t rev;
    bool mp;
    xlnx_image_params_t file;
    xlnx_image_params_t image;
    xlnx_image_params_t image_master;
    uint32_t image_master_data[256/4];
    uint32_t image_golden_data[256/4];

    res = espi_flash_read(dev, 0, 10, 512, 0, 256, (uint8_t* )image_golden_data);
    if (res) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "Failed to read current golden image header! res=%d\n", res);
        return -EINVAL;
    }
    res = espi_flash_read(dev, 0, 10, 512, MASTER_IMAGE_OFF, 256, (uint8_t* )image_master_data);
    if (res) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "Failed to read current master image header! res=%d\n", res);
        return -EINVAL;
    }
    res = xlnx_btstrm_parse_header(image_golden_data, 256/4, &image);
    if (res) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "It looks like the FPGA G image is corrupted! res=%d\n", res);
        return -EINVAL;
    }
    res = xlnx_btstrm_parse_header(image_master_data, 256/4, &image_master);
    if (res) {
        USDR_LOG("DSTR", USDR_LOG_WARNING, "It looks like the FPGA M image is corrupted! res=%d\n", res);
    } else {
        mp = true;
    }
    res = xlnx_btstrm_parse_header((const uint32_t *)data, 256/4, &file);
    if (res) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "It looks like the file is corrupted! res=%d\n", res);
        return -EINVAL;
    }
    res = xlnx_btstrm_iprgcheck(&image, &file, MASTER_IMAGE_OFF, update_golden);
    if (res) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "Image check failed! res=%d\n", res);
        return -EINVAL;
    }
    res = dev_gpi_get32(dev, 0, &rev);
    if (res)
        return res;

    USDR_LOG("DSTR", USDR_LOG_INFO, "Writing %d bytes; old(G/M) = %08x/%08x [%d] new = %08x current = %08x!\n",
            s_op_flash_length, image.usr_access2, image_master.usr_access2, mp, file.usr_access2, rev);

    return 0;
}



int general_call(pdm_dev_t dmdev, const struct sdr_call *pcall,
                unsigned outbufsz, char *outbuffer, const char *inbuffer,
                fn_callback_t func, void* obj)
{
    lldev_t lldev = dmdev->lldev;

    switch (pcall->call_type) {
    case SDR_FLASH_READ: {
        char buf[256];
        int res, k;
        unsigned offset = (pcall->params.parameters_type[SDRC_OFFSET] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_OFFSET] : 0;
        unsigned param = (pcall->params.parameters_type[SDRC_PARAM] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_PARAM] : 0;
        bool read_golden = (param == 1337);
        uint32_t base = (read_golden) ? 0 : MASTER_IMAGE_OFF;

        res = espi_flash_read(lldev, 0, M2PCI_REG_QSPI_FLASH,
                              512, base + offset, 256, (uint8_t*)buf);
        if (res) {
            snprintf(outbuffer, outbufsz, "{\"result\":%d}", res);
            return 0;
        }
        k = snprintf(outbuffer, outbufsz,
                     "{\"result\":0,\"details\":{\"data-length\":256,\"data\":\"");

        // TODO check size
        k += base64_encode(buf, 256, outbuffer + k);
        snprintf(outbuffer + k, outbufsz - k, "\"}}");
        return 0;
    }
    case SDR_FLASH_WRITE_SECTOR: {
        char buf[256 + 3];
        int res, k;
        unsigned offset = (pcall->params.parameters_type[SDRC_OFFSET] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_OFFSET] : 0;
        unsigned checksum = (pcall->params.parameters_type[SDRC_CHECKSUM] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_CHECKSUM] : 0;
        unsigned param = (pcall->params.parameters_type[SDRC_PARAM] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_PARAM] : 0;

        if (pcall->call_data_ptr == 0)
            return -EINVAL;
        if (pcall->call_data_size != 344)
            return -EINVAL;

        res = base64_decode(inbuffer + pcall->call_data_ptr, pcall->call_data_size, buf);
        if (res != 256) {
            USDR_LOG("DSTR", USDR_LOG_ERROR, "Flash sector incorrect length %d\n", res);
            return -EINVAL;
        }

        unsigned vcheck = 0;
        for (k = 0; k < 256; k++) {
            vcheck += ((unsigned char*)buf)[k];
        }

        if (vcheck != checksum) {
            USDR_LOG("DSTR", USDR_LOG_ERROR, "Incorrect checksum; calculated %d != %d provided\n",
                     vcheck, vcheck);
            return -EINVAL;
        }

        if (offset >= s_op_flash_offset + s_op_flash_length) {
            USDR_LOG("DSTR", USDR_LOG_ERROR, "Incorrect state\n");
            return -EINVAL;
        }

        if (s_op_flash_offset == offset) {
            bool update_golden = (param == 1337);

            if (s_op_flash_length == 0)
                return -EINVAL;

            res = check_firmware_header(lldev, buf, update_golden);
            if (update_golden) {
                // For golden image write boot sector with trampoline first, then erase the whole flash
                res = (res) ? res : espi_flash_erase(lldev, 0, M2PCI_REG_QSPI_FLASH, 4096, 0);
                res = (res) ? res : espi_flash_write(lldev, 0, M2PCI_REG_QSPI_FLASH, 512,
                                                     (const uint8_t*)buf, 256, 0, ESPI_FLASH_DONT_ERASE);
                res = (res) ? res : espi_flash_erase(lldev, 0, M2PCI_REG_QSPI_FLASH,
                                                     (s_op_flash_length - 4096 + 4095) & 0xfffff000,
                                                     0 + 4096);
            } else {
                res = (res) ? res : espi_flash_erase(lldev, 0, M2PCI_REG_QSPI_FLASH,
                                                     (s_op_flash_length + 4095) & 0xfffff000,
                                                     MASTER_IMAGE_OFF);
            }
            if (res) {
                s_op_flash_offset = 0;
                s_op_flash_length = 0;
                return res;
            }

            USDR_LOG("DSTR", USDR_LOG_INFO, "Flash blanked\n");
            s_op_flash_golden = update_golden;
            s_op_flash_validated = 1;

            memcpy(s_op_flash_header, buf, 256);
        } if (!s_op_flash_validated) {
            return -ENOEXEC;
        }

        if (offset + 256 >= s_op_flash_offset + s_op_flash_length) {
            USDR_LOG("DSTR", USDR_LOG_INFO, "Flash write reached end!\n");
            s_op_flash_offset = s_op_flash_length = 0;
        }

        uint32_t base = (s_op_flash_golden) ? 0 : MASTER_IMAGE_OFF;
        if (offset != 0) {
            res = espi_flash_write(lldev, 0, M2PCI_REG_QSPI_FLASH, 512,
                                   (const uint8_t*)buf, 256, offset + base, ESPI_FLASH_DONT_ERASE);
            if (res)
                return res;
        }

        if (!s_op_flash_golden && s_op_flash_length == 0 && s_op_flash_offset == 0) {
            //Write header of non-golden image
            USDR_LOG("DSTR", USDR_LOG_INFO, "Flash write header of non-golden!\n");

            res = espi_flash_write(lldev, 0, M2PCI_REG_QSPI_FLASH, 512,
                                   s_op_flash_header, 256, 0 + base, ESPI_FLASH_DONT_ERASE);
            if (res)
                return res;
        }

        snprintf(outbuffer, outbufsz, "{\"result\":0}");
        return 0;
    }
    case SDR_FLASH_ERASE: {
        unsigned offset = (pcall->params.parameters_type[SDRC_OFFSET] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_OFFSET] : 0;
        unsigned length = (pcall->params.parameters_type[SDRC_LENGTH] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_LENGTH] : 0;

        s_op_flash_length = 0;
        s_op_flash_offset = 0;
        s_op_flash_golden = 0;
        s_op_flash_validated = 0;

        if (offset + length > 2 * 1024 * 1024)
            return -EINVAL;
        if (length % 256)
            return -EINVAL;
        if (length == 0)
            return -EINVAL;

        s_op_flash_length = length;
        s_op_flash_offset = offset;

        USDR_LOG("DSTR", USDR_LOG_INFO, "Flash erase commited from %d to %d\n",
                 s_op_flash_offset, s_op_flash_length);
        snprintf(outbuffer, outbufsz, "{\"result\":0}");
        return 0;
    }
        /*
    case SDR_GET_REVISION: {
        uint32_t rev;
        res = dev_gpi_get32(lldev, IGPI_USR_ACCESS2, &rev);
        if (res)
            return res;

        unsigned day = (rev >> 27) & 0x1f;
        unsigned month = (rev >> 23) & 0x0f;
        unsigned year = (rev >> 17) & 0x3f;
        unsigned hour = (rev >> 12) & 0x1f;
        unsigned min = (rev >> 6) & 0x3f;
        unsigned sec = (rev >> 0) & 0x3f;

        res = dev_gpi_get32(lldev, IGPI_HWID, &rev);
        if (res)
            return res;

        unsigned devrev = (rev >> 8) & 0xff;
        unsigned devidf = (rev >> 16) & 0xff;
        unsigned devid = (devidf << 24) | (devrev << 4);

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"revision\":\"%04d%02d%02d%02d%02d%02d\",\"devid\":\"%d\",\"devrev\":\"%d\",\"device\":\"xsdr\"}",
                 2000 + year, month, day, hour, min, sec, devid, devrev);
        return 0;
    }
        */

#if 0
    case SDR_CRTL_STREAMING: {
        unsigned samplerate = (pcall->params.parameters_type[SDRC_SAMPLERATE] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_SAMPLERATE] : 0;
        unsigned param = (pcall->params.parameters_type[SDRC_PARAM] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_PARAM] : 0;
        unsigned throttleon = (pcall->params.parameters_type[SDRC_THROTTLE_ON] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_THROTTLE_ON] : 0;

        bool stream_restart = (param & 16) ? true : false;
        bool stream_start = (param & 8) ? false : true;
        bool dont_touch_tm = (param == 42) ? true : false;
        unsigned sync_type = 7 - (param & 7);

        if (stream_restart && !dont_touch_tm) {
            res = ctrl_sdr_rxstreaming_startstop(lldev, false);
            if (res)
                return res;

            res = lowlevel_reg_wr32(lldev, 0, 14, (1u << 31) | (0 << 16));
            if (res) {
                return res;
            }
        }

        if ((stream_start || dont_touch_tm) && samplerate != 0) {
            res = func(obj, SDR_PC_SET_SAMPLERATE, samplerate); //xsdr_set_samplerate(dev, samplerate, samplerate, 0, 0);

            if (res)
                return res;

            if (throttleon > 0 && samplerate > throttleon) {
                unsigned skip = (samplerate + throttleon - 1) / throttleon;
                res = sfe_rx4_throttle(lldev, 0, CSR_RFE4_BASE, true, 1, 2 * skip - 1);
            } else {
                res = sfe_rx4_throttle(lldev, 0, CSR_RFE4_BASE, false, 0, 0);
            }
        }

        if (!dont_touch_tm) {
            res = ctrl_sdr_rxstreaming_startstop(lldev, stream_start);
            if (res)
                return res;

            // Free running timer
            res = lowlevel_reg_wr32(lldev, 0, 14, (1u << 31) | (sync_type << 16));
            if (res) {
                return res;
            }
        }

        snprintf(outbuffer, outbufsz, "{\"result\":0}");
        return 0;
    }
#endif
#if 0
    case SDR_DEBUG_DUMP: {
        uint32_t txdump[4];
        int res;

        for (unsigned i = 0; i < 4; i++) {
            res = lowlevel_reg_rd32(lldev, 0, M2PCI_REG_RD_TXDMA_STAT + i, &txdump[i]);
            if (res)
                    return res;
        }

        USDR_LOG("DSTR", USDR_LOG_INFO, "DUMP - %08x - %08x - %08x - %08x\n",
                 txdump[0], txdump[1], txdump[2], txdump[3]);

        snprintf(outbuffer, outbufsz, "{\"result\":0}");
        return 0;
    }
#endif
    }
    return -EINVAL;
}

#if 0
int webusb_create_ugen(struct webusb_ops* ctx,
                       uintptr_t param,
                       struct webusb_device** dev)
{
    struct webusb_device_ugen *d = (struct webusb_device_ugen *)malloc(sizeof(struct webusb_device_ugen));

    d->base.param = param;
    d->base.ops = ctx;
    d->base.rpc_call = xsdr_usdr_generic_rpc_call_fn;//&generic_rpc_call;
    d->base.dif_set_uint = xsdr_usdr_dif_set_uint_fn;//&_dif_set_uint;
    d->base.dif_get_uint = xsdr_usdr_dif_get_uint_fn;//&_dif_get_uint;

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

    *dev = &d->base;

    return 0;
}
#endif
#if 0
int webusb_create_ugen(struct webusb_ops* ctx,
                       uintptr_t param,
                       unsigned loglevel,
                       struct webusb_device** dev,
                       lldev_t lldev)
{
    struct webusb_device_ugen *d = (struct webusb_device_ugen *)malloc(sizeof(struct webusb_device_ugen));

    d->base.param = param;
    d->base.loglevel = loglevel;
    d->base.ops = ctx;
    d->base.rpc_call = &generic_rpc_call;
    d->base.dif_set_uint = &_dif_set_uint;
    d->base.dif_get_uint = &_dif_get_uint;

    //d->base.ll.pdev = lldev->pdev;
    //d->base.ll.ops = &s_ll_ops;
    d->base.ll = *lldev;

    d->base.dmdev.lldev = &d->base.ll;
    d->base.dmdev.debug_obj = NULL;
    usdr_dmo_init(&d->base.dmdev.obj_head, NULL);

    d->base.strms[0] = NULL;
    d->base.strms[1] = NULL;

    //??
    //TODO - это здесь ваще надо?..
    //
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
    //

    *dev = &d->base;
#if 0
    //unsigned hwid;

    res = type_xsdr ? xsdr_ctor(lldev, &d->device.xsdr) : usdr_ctor(lldev, 0, &d->device.usdr);
    if (res) {
        return res;
    }

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

    //res = dev_gpi_get32(dev, IGPI_HWID, &hwid);
    res = lowlevel_reg_rd32(lldev, 0, 16 + (IGPI_HWID / 4), &hwid);
    if (res)
        return res;

    // Init USB
    if (hwid & 0x40000000) {
//        const unsigned REG_WR_PNTFY_CFG = 8;
//        const unsigned REG_WR_PNTFY_ACK = 9;

        lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_CFG, 0xdeadb000);
        lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_CFG, 0xeeadb001);
        lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_CFG, 0xfeadb002);
        lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_CFG, 0xaeadb003);
        lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_CFG, 0xbeadb004);
        lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_CFG, 0xceadb005);
        lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_CFG, 0x9eadb006);
        lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_CFG, 0x8eadb007);

        for (unsigned i = 0; i < 8; i++) {
            res = lowlevel_reg_wr32(lldev, 0, M2PCI_REG_INT,  i | (i << 8) | (1 << 16) | (7 << 20));
            res = lowlevel_reg_wr32(lldev, 0, REG_WR_PNTFY_ACK, 0 | i << 16);
        }

        // IGPO_FRONT activates tran_usb_active to route interrupts to NTFY endpoint
        res = lowlevel_reg_wr32(lldev, 0, 0, (15u << 24) | (0x80));
        if (res) {
            USDR_LOG("USBX", USDR_LOG_ERROR,
                     "Unable to set stream routing, error %d\n", res);
            return res;
        }

        res = lowlevel_reg_wr32(lldev, 0, M2PCI_REG_INT, (1 << M2PCI_INT_SPI_0) | (1 << M2PCI_INT_I2C_0));
        if (res) {
            return res;
        }
    }

    res = type_xsdr ? xsdr_init(&d->device.xsdr) : usdr_init(&d->device.usdr, -1, 0);
    if (res) {
        return res;
    }

    //////////////////////////////////////////////////////////////////
#endif
    return 0;
}
#endif




