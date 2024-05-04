// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include "controller.h"
#include <usdr_logging.h>

int lime_call(pdm_dev_t pdev,
              struct sdr_call* pcall,
              unsigned outbufsz, char *outbuffer, char *inbuffer)
{
#if 0
    int res;
    webusb_device_lime_t* d = (webusb_device_lime_t*)dev;

    limesdr_dev_t* lime_dev = get_limesdr_dev(d->base.ll.pdev);
#endif
    switch (pcall->call_type) {
    case SDR_FLASH_READ:
    case SDR_FLASH_WRITE_SECTOR:
    case SDR_FLASH_ERASE:
    case SDR_CALIBRATE:
        return -ENOTSUP;
#if 0
    case SDR_CRTL_STREAMING: {
        unsigned samplerate = (pcall->params.parameters_type[SDRC_SAMPLERATE] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_SAMPLERATE] : 0;
        unsigned param = (pcall->params.parameters_type[SDRC_PARAM] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_PARAM] : 0;
//        unsigned throttleon = (pcall->params.parameters_type[SDRC_THROTTLE_ON] == SDRC_PARAM_TYPE_INT) ?
//                    pcall->params.parameters_uint[SDRC_THROTTLE_ON] : 0;

        bool stream_restart = (param & 16) ? true : false;
        bool stream_start = (param & 8) ? false : true;
        bool dont_touch_tm = (param == 42) ? true : false;
//        unsigned sync_type = 7 - (param & 7);

        if (stream_restart && !dont_touch_tm) {
            res = limesdr_stop_streaming(lime_dev);
            if (res)
                return res;
        }
        if ((stream_start || dont_touch_tm) && samplerate != 0) {
            res = limesdr_set_samplerate(lime_dev, samplerate, samplerate, 0, 0);
            if (res)
                return res;
        }
        if (!dont_touch_tm && stream_start) {
            res = limesdr_setup_stream(lime_dev, false, true, false);
            if (res)
                return res;
        }

        snprintf(outbuffer, outbufsz, "{\"result\":0}");
        return 0;
    }
#endif
#if 0
    case SDR_INIT_STREAMING: {
//        unsigned chans = (pcall->params.parameters_type[SDRC_CHANS] == SDRC_PARAM_TYPE_INT) ?
//                    pcall->params.parameters_uint[SDRC_CHANS] : 0;
        unsigned pktsyms = (pcall->params.parameters_type[SDRC_PACKETSIZE] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_PACKETSIZE] : 0;
//        const char* fmt = (pcall->params.parameters_type[SDRC_DATA_FORMAT] == SDRC_PARAM_TYPE_INT) ?
//                    (const char*)pcall->params.parameters_uint[SDRC_DATA_FORMAT] : 0;

        unsigned samplerate = (pcall->params.parameters_type[SDRC_SAMPLERATE] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_SAMPLERATE] : 0;
        unsigned param = (pcall->params.parameters_type[SDRC_PARAM] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_PARAM] : 0;
//        unsigned throttleon = (pcall->params.parameters_type[SDRC_THROTTLE_ON] == SDRC_PARAM_TYPE_INT) ?
//                    pcall->params.parameters_uint[SDRC_THROTTLE_ON] : 0;
//        unsigned mode = (pcall->params.parameters_type[SDRC_MODE] == SDRC_PARAM_TYPE_INT) ?
//                    pcall->params.parameters_uint[SDRC_MODE] : 3; //1 - rx, 2 - tx

        bool stream_start = (param & 8) ? false : true;
        //unsigned sync_type = 7 - (param & 7);
        //unsigned ctrl_flags = (param & 16) ? CTLXSDR_RXEXTSTATTX : 0;

        if (pktsyms % 1020) {
            USDR_LOG("LIME", USDR_LOG_ERROR, "Samples in packet should be number of 1020 samples for CS16 wire firmat; requested %d!\n",
                     pktsyms);
            return -EINVAL;
        }

        res = limesdr_stop_streaming(lime_dev);
        if (res)
            return res;

        res = res ? res : ft601_flush_pipe(d, EP_OUT_DEFSTREAM);
        res = res ? res : ft601_set_stream_pipe(d, EP_OUT_DEFSTREAM, DATA_PACKET_SIZE);
        res = res ? res : ft601_flush_pipe(d, EP_IN_DEFSTREAM);
        res = res ? res : ft601_set_stream_pipe(d, EP_IN_DEFSTREAM, DATA_PACKET_SIZE);

        if (samplerate == 0) {
            samplerate = 1000000;
        }
        res = limesdr_set_samplerate(lime_dev, samplerate, samplerate, 0, 0);
        if (res)
            return res;

        res = limesdr_prepare_streaming(lime_dev);
        if (res)
            return res;

        if (stream_start) {
            res = limesdr_setup_stream(lime_dev, false, true, false);
            if (res)
                return res;
        }

        unsigned bursts = pktsyms / 1020;
        unsigned blocksize = 4096 * bursts;
        snprintf(outbuffer, outbufsz, "{\"result\":0,\"details\":{\"wire-block-size\":%d,\"wire-bursts\":%d}}",
                 blocksize, bursts);
        return 0;

    }
#endif
#if 0
    case SDR_STOP_STREAMING: {
        res = limesdr_stop_streaming(lime_dev);
        if (res)
            return res;

        return 0;
    }
#endif
#if 0
    case SDR_DEBUG_DUMP: {
        snprintf(outbuffer, outbufsz, "{\"result\":0}");
        return 0;
    }
#endif
#if 0
    case SDR_GET_REVISION: {
        unsigned day = 1;//(rev >> 27) & 0x1f;
        unsigned month = 1; //(rev >> 23) & 0x0f;
        unsigned year = 2013; //(rev >> 17) & 0x3f;
        unsigned hour = 1; //(rev >> 12) & 0x1f;
        unsigned min = 1; //(rev >> 6) & 0x3f;
        unsigned sec = 1; //(rev >> 0) & 0x3f;

        unsigned devrev = 2; //(rev >> 8) & 0xff;
        unsigned devid = 0x77; //(devidf << 24) | (devrev << 4);

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"revision\":\"%04d%02d%02d%02d%02d%02d\",\"devid\":\"%d\",\"devrev\":\"%d\",\"device\":\"limesdr_mini\"}",
                 2000 + year, month, day, hour, min, sec, devid, devrev);
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

        res = lms7002m_fe_set_freq(&lime_dev->base, chans,
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

        res = lms7002m_bb_set_badwidth(&lime_dev->base, chans,
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

        res = lms7002m_set_gain(&lime_dev->base, chans,
                                (pcall->call_type == SDR_RX_GAIN) ? RFIC_LMS7_RX_LNA_GAIN : RFIC_LMS7_TX_PAD_GAIN, gain, &actual);
        if (res)
            return res;

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"details\":{\"actual-gain\":%g}}",
                 actual);
        return 0;
    }
#endif
    default:
        break;
    }
    return -EINVAL;
}

#if 0
int webusb_create_lime(struct webusb_ops* ctx,
                       uintptr_t param,
                       struct webusb_device** dev,
                       lldev_t lldev)
{
    webusb_device_lime_t *d = (webusb_device_lime_t *)malloc(sizeof(webusb_device_lime_t));

    d->base.param = param;
    d->base.ops = ctx;
    d->base.rpc_call = &generic_rpc_call;
    d->base.dif_set_uint = &_dif_set_uint;
    d->base.dif_get_uint = &_dif_get_uint;

    d->base.ll = *lldev;

    d->base.strms[0] = NULL;
    d->base.strms[1] = NULL;

    *dev = &d->base;
/*
    res = res ? res : ft601_flush_pipe(d, EP_IN_CTRL);  //clear ctrl ep rx buffer
    res = res ? res : ft601_set_stream_pipe(d, EP_IN_CTRL, CTRL_SIZE);
    res = res ? res : ft601_set_stream_pipe(d, EP_OUT_CTRL, CTRL_SIZE);
    if (res) {
        goto failed_init;
    }

    res = limesdr_ctor(lldev, &d->ldev);
    if (res) {
        goto failed_init;
    }

    res = limesdr_init(&d->ldev);
    if (res) {
        goto failed_init;
    }
*/
    return 0;
/*
failed_init:
    free(d);
    *dev = NULL;
    return res;
*/
}
#endif


