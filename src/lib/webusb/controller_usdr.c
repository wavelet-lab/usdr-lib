// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "controller_ugen.h"
#include "controller_usdr.h"
#include "usdr_logging.h"

#include <stdio.h>
#include <string.h>
#include "../device/m2_lm6_1/usdr_ctrl.h"

// TODO: get rid of this foo
usdr_dev_t* get_usdr_dev(pdevice_t udev);

#if 0
#include "base64.h"

#include "../ipblks/streams/dma_rx_32.h"
#include "../ipblks/streams/sfe_rx_4.h"
#include "../ipblks/streams/dma_tx_32.h"
#include "../ipblks/streams/sfe_tx_4.h"

#include "../ipblks/espi_flash.h"
#include "../ipblks/xlnx_bitstream.h"

enum {
    CTLXSDR_RXEXTSTATTX = 1,
};

static int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, 0, 16 + (bank / 4), data);
}
#endif

int _usdr_callback(void* obj, int type, unsigned parameter)
{
    struct usdr_dev* dev = (struct usdr_dev*)obj;
    if (type == SDR_PC_SET_SAMPLERATE)
        return usdr_set_samplerate_ex(dev, parameter, parameter, 0, 0, 0);

    return -EINVAL;
}

#if 0
static int dev_gpo_set(lldev_t dev, unsigned bank, unsigned data)
{
    return lowlevel_reg_wr32(dev, 0, 0, ((bank & 0x7f) << 24) | (data & 0xff));
}

usdr_dev_t* get_usdr_dev(pdevice_t udev);
#endif

int usdr_call(pdm_dev_t dmdev, const struct sdr_call *pcall,
              unsigned outbufsz, char* outbuffer, const char *inbuffer)
{
#if 0
    int res;

    switch (pcall->call_type) {
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
                    pcall->params.parameters_uint[SDRC_MODE] : 2; //1 - rx, 2 - tx

        bool stream_start = (param & 8) ? false : true;
        unsigned sync_type = 7 - (param & 7);
        unsigned ctrl_flags = (param & 16) ? CTLXSDR_RXEXTSTATTX : 0;

        if (ctrl_flags)
            fprintf(stderr, "Enabled extended stats on RX\n");

        res = ctrl_sdr_rxstreaming_startstop(dev->base.dev, false);
        if (res)
            return res;

        /*
        res = usdr_pwren(dev, true);
        if (res)
            return res;
        */

        // Usdr initialization
        usdr_rfic_streaming_up(dev, ((mode & 1) ? RFIC_LMS6_RX : 0) | ((mode & 2) ? RFIC_LMS6_TX : 0));

        // Set samplerate
        if (samplerate == 0) {
            samplerate = 1000000;
        }
        res = usdr_set_samplerate_ex(dev, samplerate, samplerate, 0, 0, 0);
        if (res)
            return res;

        res = dev_gpo_set(dev->base.dev, IGPO_LED, 1);
        if (res) {
            return res;
        }

        // Prepare streaming
        unsigned blocksize, bursts;
        res = ctrl_sdr_rxstreaming_prepare(dev->base.dev, chans, pktsyms, fmt, &blocksize, &bursts, ctrl_flags);
        if (res)
            return res;

        res = lowlevel_reg_wr32(dev->base.dev, 0, 14, (1u << 31) | (0 << 16));
        if (res) {
            return res;
        }


        USDR_LOG("DSTR", USDR_LOG_INFO, "Start data: %d, sync type: %d\n",
                 stream_start, sync_type);

        res = ctrl_sdr_rxstreaming_startstop(dev->base.dev, stream_start);
        if (res)
            return res;

        if (throttleon > 0 && samplerate > throttleon) {
            unsigned skip = (samplerate + throttleon - 1) / throttleon;
            res = sfe_rx4_throttle(dev->base.dev, 0, CSR_RFE4_BASE, true, 1, 2 * skip - 1);
        } else {
            res = sfe_rx4_throttle(dev->base.dev, 0, CSR_RFE4_BASE, false, 0, 0);
        }

        // Free running timer
        res = lowlevel_reg_wr32(dev->base.dev, 0, 14, (1u << 31) | (sync_type << 16));
        if (res) {
            return res;
        }

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"details\":{\"wire-block-size\":%d,\"wire-bursts\":%d}}",
                 blocksize, bursts);
        return 0;
    }
#endif
#if 0
    case SDR_STOP_STREAMING: {
       res = ctrl_sdr_rxstreaming_startstop(dev->base.dev, false);
       if (res)
           return res;

       res = usdr_rfic_streaming_down(dev, RFIC_LMS6_TX | RFIC_LMS6_RX);
       if (res)
           return res;

       res = dev_gpo_set(dev->base.dev, IGPO_LED, 0);
       if (res) {
           return res;
       }

       snprintf(outbuffer, outbufsz, "{\"result\":0}");
       return 0;
    }
#endif
#if 0
    case SDR_RX_FREQUENCY:
    case SDR_TX_FREQUENCY:
    {
        unsigned long freq = (pcall->params.parameters_type[SDRC_FREQUENCTY] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_FREQUENCTY] : 0;
        double actual;

        res = usdr_rfic_fe_set_freq(dev,
                                    (pcall->call_type == SDR_RX_FREQUENCY) ? false : true,
                                    freq, &actual);
        if (res)
            return res;

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"details\":{\"actual-frequency\":%g}}",
                 (double)actual);
        return 0;
    }
    case SDR_RX_BANDWIDTH:
    case SDR_TX_BANDWIDTH:
    {
        unsigned freq = (pcall->params.parameters_type[SDRC_FREQUENCTY] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_FREQUENCTY] : 0;
        unsigned actual;

        res = usdr_rfic_bb_set_badwidth(dev,
                                        (pcall->call_type == SDR_RX_BANDWIDTH) ? false : true,
                                        freq+0.5, &actual);
        if (res)
            return res;

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"details\":{\"actual-frequency\":%d}}",
                 actual);
        return 0;
    }
    case SDR_RX_GAIN:
    case SDR_TX_GAIN:
    {
        int gain = (pcall->params.parameters_type[SDRC_GAIN] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_GAIN] : 0;
        int actual;

        res = usdr_rfic_set_gain(dev,
                                (pcall->call_type == SDR_RX_GAIN) ? GAIN_RX_VGA1 : GAIN_TX_VGA2, gain, &actual);
        if (res)
            return res;

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"details\":{\"actual-gain\":%d}}",
                 actual);
        return 0;
    }
#endif
#if 0
    case SDR_GET_REVISION: {
        uint32_t rev;
        res = dev_gpi_get32(dev->base.dev, IGPI_USR_ACCESS2, &rev);
        if (res)
            return res;

        unsigned day = (rev >> 27) & 0x1f;
        unsigned month = (rev >> 23) & 0x0f;
        unsigned year = (rev >> 17) & 0x3f;
        unsigned hour = (rev >> 12) & 0x1f;
        unsigned min = (rev >> 6) & 0x3f;
        unsigned sec = (rev >> 0) & 0x3f;

        res = dev_gpi_get32(dev->base.dev, IGPI_HWID, &rev);
        if (res)
            return res;

        unsigned devrev = (rev >> 8) & 0xff;
        unsigned devidf = (rev >> 16) & 0xff;
        unsigned devid = (devidf << 24) | (devrev << 4);

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"revision\":\"%04d%02d%02d%02d%02d%02d\",\"devid\":\"%d\",\"devrev\":\"%d\",\"device\":\"usdr\"}",
                 2000 + year, month, day, hour, min, sec, devid, devrev);
        return 0;
    }
    }
#endif

    return general_call(dmdev, pcall, outbufsz, outbuffer, inbuffer, _usdr_callback, get_usdr_dev(dmdev->lldev->pdev));
}
