// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "controller.h"
#include "controller_xsdr.h"
#include "usdr_logging.h"

#include <stdio.h>
#include <string.h>
#include "base64.h"

#if 0
#include "../ipblks/streams/dma_rx_32.h"
#include "../ipblks/streams/sfe_rx_4.h"
#include "../ipblks/streams/dma_tx_32.h"
#include "../ipblks/streams/sfe_tx_4.h"
#endif

#include "../ipblks/espi_flash.h"
#include "../ipblks/xlnx_bitstream.h"
#include "../device/m2_lm7_1/xsdr_ctrl.h"

// TODO: get rid of this foo
xsdr_dev_t* get_xsdr_dev(pdevice_t udev);

#if 0
// wirefmt 16, 12, 8 bits

// TODO: move away core configuration
static const unsigned fe_base = VIRT_CFG_SFX_BASE + 256;
static const unsigned fe_fifobsz = 0x10000;
static const unsigned sx_cfg_base = VIRT_CFG_SFX_BASE;
static const unsigned sx_base = M2PCI_REG_WR_RXDMA_CONFIRM;

enum {
    CTLXSDR_RXEXTSTATTX = 1,
};

static
int ctrlxsdr_rxstreaming_prepare(
        struct xsdr_dev* dev,
        unsigned chmask,
        unsigned pktsyms,
        const char* wirefmt,
        unsigned* block_size,
        unsigned* burstcnt,
        unsigned flags)
{
    int res;
    lldev_t lldev = dev->base.lmsstate.dev;


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

int ctrlxsdr_rxstreaming_startstop(struct xsdr_dev* dev, bool start)
{
    int res;

    // RX
    res = lowlevel_reg_wr32(dev->base.lmsstate.dev, 0,
                            sx_base + 1, start ? 1 : 0);
    if (res)
        return res;

    res = sfe_rx4_startstop(dev->base.lmsstate.dev, 0,
                            fe_base, 0, start);
    if (res)
        return res;


    // TX (SISO)
    res = lowlevel_reg_wr32(dev->base.lmsstate.dev, 0,
                            M2PCI_REG_WR_TXDMA_COMB, start ? ((1<<3) | 3) : ((1<<7)) | 0);
    if (res)
        return res;


    return 0;
}
#endif
static int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, 0, 16 + (bank / 4), data);
}
#if 0
static int dev_gpo_set(lldev_t dev, unsigned bank, unsigned data)
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

//enum {
//    CSR_RFE4_BASE = VIRT_CFG_SFX_BASE + 256,
//};

int xsdr_call(pdm_dev_t dmdev, const struct sdr_call *pcall,
              unsigned outbufsz, char *outbuffer, const char *inbuffer)
{
    int res;
    lldev_t lldev = dmdev->lldev;
#if 0
    webusb_device_t* dev = (webusb_device_t*)lldev;
#endif
    switch (pcall->call_type) {
#if 0
    case SDR_INIT_STREAMING: {
        unsigned chans = (pcall->params.parameters_type[SDRC_CHANS] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_CHANS] : 0;
        unsigned pktsyms = (pcall->params.parameters_type[SDRC_PACKETSIZE] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_PACKETSIZE] : 0;
        const char* fmt = (pcall->params.parameters_type[SDRC_DATA_FORMAT] == SDRC_PARAM_TYPE_INT) ?
                    (const char*)pcall->params.parameters_uint[SDRC_DATA_FORMAT] : 0;

        unsigned samplerate = (pcall->params.parameters_type[SDRC_SAMPLERATE] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_SAMPLERATE] : 0;
        unsigned param = (pcall->params.parameters_type[SDRC_PARAM] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_PARAM] : 0;
        unsigned throttleon = (pcall->params.parameters_type[SDRC_THROTTLE_ON] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_THROTTLE_ON] : 0;
        unsigned mode = (pcall->params.parameters_type[SDRC_MODE] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_MODE] : 3; //1 - rx, 2 - tx

        bool stream_start = (param & 8) ? false : true;
        unsigned sync_type = 7 - (param & 7);
        unsigned ctrl_flags = (param & 16) ? CTLXSDR_RXEXTSTATTX : 0;

        if (ctrl_flags)
            fprintf(stderr, "Enabled extended stats on RX\n");

        res = ctrlxsdr_rxstreaming_startstop(dev, false);
        if (res)
            return res;

        /////////////////////////////////////////
#if 0
        res = dev_gpo_set(dev->base.dev, IGPO_LMS_PWR, 0); //Disble, put into reset
        if (res)
            return res;

        res = xsdr_pwren_revx(dev, true);
        if (res)
            return res;
        usleep(5000);

        res = dev_gpo_set(dev->base.dev, IGPO_LMS_PWR, 1); //Enable LDO, put into reset
        if (res)
            return res;
        usleep(1000);

        res = dev_gpo_set(dev->base.dev, IGPO_LMS_PWR, 9); //Enable LDO, reset release
        if (res)
            return res;

        usleep(2500);

        res = lms7_enable(&dev->lmsstate, false);
        if (res)
            return res;
#endif
        res = xsdr_pwren(dev, true);
        if (res)
            return res;

        /////////////////////////////////////////////
        // Set samplerate
        if (samplerate == 0) {
            samplerate = 1000000;
        }
        res = xsdr_set_samplerate(dev, samplerate, samplerate, 0, 0);
        if (res)
            return res;

        res = xsdr_prepare(dev, true, (mode & 2) ? true : false);
#if 0
        res = dev_gpo_set(dev->base.dev, IGPO_DSP_RST, 1);
        res = dev_gpo_set(dev->base.dev, IGPO_DSP_RST, 0);


        // Init RFIC
        res = dev_gpo_set(dev->base.dev, IGPO_LMS_PWR, 9 + 4 + 2); //Enable LDO, reset release, RX enble
        if (res) {
            return res;
        }

        // Set samplerate
        if (samplerate == 0) {
            samplerate = 1000000;
        }
        res = xsdr_set_samplerate(dev, samplerate, samplerate, 0, 0);
        if (res)
            return res;


        res = dev_gpo_set(dev->base.dev, IGPO_LED, 1);
        if (res) {
            return res;
        }

        // We need TX chain for calibration, turn if off once we did it
        dev->rx_run[0] = true;
        dev->rx_run[1] = true;
        dev->tx_run[0] = true;
        dev->tx_run[1] = true;
        res = xsdr_rfic_streaming_up(dev, RFIC_LMS7_RX | RFIC_LMS7_TX,
                                     RFIC_CHAN_AB,
                                     0,
                                     RFIC_CHAN_AB, 0);
#endif
        if (res) {
            return res;
        }

        // Prepare streaming
        unsigned blocksize, bursts;
        res = ctrlxsdr_rxstreaming_prepare(dev, chans, pktsyms, fmt, &blocksize, &bursts, ctrl_flags);
        if (res)
            return res;

        res = xsdr_hwchans_cnt(dev, true, 1);
        if (res)
            return res;

        res = xsdr_hwchans_cnt(dev, false, 1);
        if (res)
            return res;


        res = lowlevel_reg_wr32(lldev, 0, 14, (1u << 31) | (0 << 16));
        if (res) {
            return res;
        }

        res = xsdr_rfic_fe_set_lna(dev, 3, XSDR_RX_AUTO);
        if (res) {
            return res;
        }

#if 0
        res = dev_gpo_set(dev->base.dev, IGPO_PHYCAL,  0x31);
        if (res) {
            return res;
        }

        res = dev_gpo_set(dev->base.dev, IGPO_PHYCAL,  0x30);
        if (res) {
            return res;
        }
#endif

        USDR_LOG("DSTR", USDR_LOG_INFO, "Start data: %d, sync type: %d\n",
                 stream_start, sync_type);

        res = ctrlxsdr_rxstreaming_startstop(dev, stream_start);
        if (res)
            return res;

        if (throttleon > 0 && samplerate > throttleon) {
            unsigned skip = (samplerate + throttleon - 1) / throttleon;
            res = sfe_rx4_throttle(lldev, 0, CSR_RFE4_BASE, true, 1, 2 * skip - 1);
        } else {
            res = sfe_rx4_throttle(lldev, 0, CSR_RFE4_BASE, false, 0, 0);
        }

        // Free running timer
        res = lowlevel_reg_wr32(lldev, 0, 14, (1u << 31) | (sync_type << 16));
        if (res) {
            return res;
        }

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"details\":{\"wire-block-size\":%d,\"wire-bursts\":%d}}",
                 blocksize, bursts);
        return 0;
    }
#endif
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
            res = ctrlxsdr_rxstreaming_startstop(dev, false);
            if (res)
                return res;

            res = lowlevel_reg_wr32(lldev, 0, 14, (1u << 31) | (0 << 16));
            if (res) {
                return res;
            }
        }

        if ((stream_start || dont_touch_tm) && samplerate != 0) {
            res = xsdr_set_samplerate(dev, samplerate, samplerate, 0, 0);
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
            res = ctrlxsdr_rxstreaming_startstop(dev, stream_start);
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
    case SDR_RX_FREQUENCY:
    case SDR_TX_FREQUENCY:
    {
        unsigned chans = (pcall->params.parameters_type[SDRC_CHANS] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_CHANS] : 0;
        unsigned freq = (pcall->params.parameters_type[SDRC_FREQUENCTY] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_FREQUENCTY] : 0;
        double actual;

        res = xsdr_rfic_fe_set_freq(dev, chans,
                                    (pcall->call_type == SDR_RX_FREQUENCY) ? RFIC_LMS7_TUNE_RX_FDD : RFIC_LMS7_TUNE_TX_FDD,
                                    freq, &actual);
        if (res)
            return res;

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"details\":{\"actual-frequency\":%g}}",
                 actual);
        return 0;
    }
    case SDR_RX_BANDWIDTH:
    case SDR_TX_BANDWIDTH:
    {
        unsigned chans = (pcall->params.parameters_type[SDRC_CHANS] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_CHANS] : 0;
        unsigned freq = (pcall->params.parameters_type[SDRC_FREQUENCTY] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_FREQUENCTY] : 0;
        unsigned actual;

        res = xsdr_rfic_bb_set_badwidth(dev, chans,
                                        (pcall->call_type == SDR_RX_BANDWIDTH) ? false : true, freq+0.5, &actual);
        if (res)
            return res;

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"details\":{\"actual-frequency\":%d}}",
                 actual);
        return 0;
    }
    case SDR_RX_GAIN:
    case SDR_TX_GAIN:
    {
        unsigned chans = (pcall->params.parameters_type[SDRC_CHANS] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_CHANS] : 0;
        unsigned gain = (pcall->params.parameters_type[SDRC_GAIN] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_GAIN] : 0;
        double actual;

        res = xsdr_rfic_set_gain(dev, chans,
                                 (pcall->call_type == SDR_RX_GAIN) ? RFIC_LMS7_RX_LNA_GAIN : RFIC_LMS7_TX_PAD_GAIN, gain, &actual);
        if (res)
            return res;

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"details\":{\"actual-gain\":%g}}",
                 actual);
        return 0;
    }
#endif
#if 0
    case SDR_STOP_STREAMING: {
       res = ctrlxsdr_rxstreaming_startstop(dev, false);
       if (res)
           return res;

       res = xsdr_rfic_streaming_down(dev, RFIC_LMS7_TX | RFIC_LMS7_RX);
       if (res)
           return res;

       res = dev_gpo_set(lldev, IGPO_LMS_PWR, 0);
       if (res) {
           return res;
       }

       res = dev_gpo_set(lldev, IGPO_LED, 0);
       if (res) {
           return res;
       }

       snprintf(outbuffer, outbufsz, "{\"result\":0}");
       return 0;
    }
#endif
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

            res = check_firmware_header(dmdev->lldev, buf, update_golden);
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
#if 0
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

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"revision\":\"%04d%02d%02d%02d%02d%02d\",\"devid\":\"%d\",\"devrev\":\"%d\",\"device\":\"%s\"}",
                 2000 + year, month, day, hour, min, sec, devid, devrev,
                 (devidf == 0x2e) ? "Authentic XTRX Pro rev. 4/5" : "xsdr");
        return 0;
    }
#endif
    case SDR_CALIBRATE: {
        unsigned chans = (pcall->params.parameters_type[SDRC_CHANS] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_CHANS] : 0;
        unsigned param = (pcall->params.parameters_type[SDRC_PARAM] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_PARAM] : 0;

        res = xsdr_calibrate(get_xsdr_dev(lldev->pdev), chans, param, NULL);
        if (res)
            return res;

        snprintf(outbuffer, outbufsz, "{\"result\":0}");
        return 0;
    }
    default:
        break;
    }
    return -EINVAL;
}
