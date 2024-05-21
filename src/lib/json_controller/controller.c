// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "tiny-json.h"

#include "../ipblks/streams/streams.h"
#include "controller.h"
#include "controller_usdr.h"
#include "controller_xsdr.h"
#include "controller_lime.h"

static const struct idx_list s_method_list[] = {
    { "sdr_init_streaming",   SDR_INIT_STREAMING },
    { "sdr_set_rx_frequency", SDR_RX_FREQUENCY },
    { "sdr_set_rx_gain",      SDR_RX_GAIN },
    { "sdr_set_rx_bandwidth", SDR_RX_BANDWIDTH },
    { "sdr_set_tx_frequency", SDR_TX_FREQUENCY },
    { "sdr_set_tx_gain",      SDR_TX_GAIN },
    { "sdr_set_tx_bandwidth", SDR_TX_BANDWIDTH },
    { "sdr_stop_streaming",   SDR_STOP_STREAMING },
    { "flash_erase",          SDR_FLASH_ERASE},
    { "flash_read",           SDR_FLASH_READ },
    { "flash_write_sector",   SDR_FLASH_WRITE_SECTOR },
    { "sdr_debug_dump",       SDR_DEBUG_DUMP },
    { "sdr_ctrl_streaming",   SDR_CRTL_STREAMING },
    { "sdr_get_revision",     SDR_GET_REVISION },
    { "sdr_calibrate",        SDR_CALIBRATE }
};

static const struct idx_list s_param_list[] = {
    { "chans",      SDRC_CHANS },
    { "samplerate", SDRC_SAMPLERATE },
    { "dataformat", SDRC_DATA_FORMAT },
    { "packetsize", SDRC_PACKETSIZE },
    { "param_type", SDRC_PARAM_TYPE },
    { "frequency",  SDRC_FREQUENCTY },
    { "gain",       SDRC_GAIN },
    { "offset",     SDRC_OFFSET },
    { "length",     SDRC_LENGTH },
    { "checksum",   SDRC_CHECKSUM },
    { "param",      SDRC_PARAM },
    { "throttleon", SDRC_THROTTLE_ON },
    { "mode",       SDRC_MODE },
};

static int parse_parameter(const char* parameter)
{
    for (unsigned i = 0; i < SIZEOF_ARRAY(s_param_list); i++) {
        if (!strcmp(parameter, s_param_list[i].param))
            return s_param_list[i].idx;
    }
    return -1;
}

static int get_req_method(const char* method)
{
    for (unsigned i = 0; i < SIZEOF_ARRAY(s_method_list); i++) {
        if (!strcmp(method, s_method_list[i].param))
            return s_method_list[i].idx;
    }
    return SDR_NOP;
}

static int get_req_parameters(struct sdr_call *psdrc, json_t const* parent)
{
    for (json_t const* child = json_getChild( parent ); child != 0; child = json_getSibling( child ) ) {
        jsonType_t propertyType = json_getType( child );
        char const* name = json_getName( child );

        int param_idx = parse_parameter(name);
        if (param_idx < 0) {
            USDR_LOG("WEBU", USDR_LOG_DEBUG, "JSON REQUEST uknkown parameter: `%s`\n", name);
            return -EINVAL;
        }

        if (propertyType == JSON_INTEGER) {
            psdrc->params.parameters_uint[param_idx] = json_getInteger( child );
            psdrc->params.parameters_type[param_idx] = SDRC_PARAM_TYPE_INT;
        } else if (propertyType == JSON_TEXT) {
            psdrc->params.parameters_uint[param_idx] = (uintptr_t)json_getValue( child );
            psdrc->params.parameters_type[param_idx] = SDRC_PARAM_TYPE_STRING;
        } else {
            USDR_LOG("WEBU", USDR_LOG_DEBUG, "JSON REQUEST PARAMETER %d (`%s`) type %d not supported!\n",
                     param_idx, name, propertyType);
            return -EINVAL;
        }
    }

    return 0;
}

#define MAX_JSON_OBJS 64

int controller_prepare_rpc(char* request, sdr_call_t* psdrc)
{
    psdrc->call_type = SDR_NOP;
    memset(psdrc->params.parameters_type, 0, sizeof(psdrc->params.parameters_type));

    json_t storage[MAX_JSON_OBJS];
    json_t const* parent = json_create(request, storage, MAX_JSON_OBJS);
    if (!parent) {
        USDR_LOG("WEBU", USDR_LOG_DEBUG, "Can't parse JSON: `%s`\n", request);
        return -EINVAL;
    }

    jsonType_t const type = json_getType( parent );
    if ( type != JSON_OBJ ) {
        return -EINVAL;
    }

    json_t const* child;
    for( child = json_getChild( parent ); child != 0; child = json_getSibling( child ) ) {
        jsonType_t propertyType = json_getType( child );
        char const* name = json_getName( child );
        char const* value = json_getValue( child );

        switch (propertyType) {
        case JSON_TEXT:
            if (strcmp(name, "req_method") == 0 && value != NULL) {
                USDR_LOG("WEBU", USDR_LOG_DEBUG, "JSON REQ_METHOD: %s\n", value);
                psdrc->call_type = get_req_method(value);
            } else if (strcmp(name, "req_data") == 0 && value != NULL) {
                //USDR_LOG("WEBU", USDR_LOG_DEBUG, "JSON REQ_DATA: %s\n", value);
                unsigned idx = value - request;
                if (idx > 512)
                    return -EINVAL;
                unsigned len = strlen(value);

                psdrc->call_data_ptr = idx;
                psdrc->call_data_size = len;
            } else {
                USDR_LOG("WEBU", USDR_LOG_DEBUG, "JSON unknown text parameter: %s\n", name);
                return -EINVAL;
            }
            break;
        case JSON_OBJ:
            if (strcmp(name, "req_params") == 0) {
                int res = get_req_parameters(psdrc, child);
                if (res)
                    return res;
            } else {
                USDR_LOG("WEBU", USDR_LOG_DEBUG, "JSON unknown object parameter: %s\n", name);
                return -EINVAL;
            }
            break;
        default:
            USDR_LOG("WEBU", USDR_LOG_ERROR, "JSON unexpected root type:%d\n",
                     propertyType);
            return -EINVAL;
        }
    }

    return 0;
}

static
    int set_throttle(pdm_dev_t dmdev, unsigned throttleon, unsigned samplerate)
{
    int res = 0;

    if (throttleon > 0 && samplerate > throttleon)
    {
        uint64_t skip = (samplerate + throttleon - 1) / throttleon;
        if(skip > 0xFF)
        {
            USDR_LOG("TRTL", USDR_LOG_ERROR, "incorrect params throttleon, samplerate!\n");
            return -EINVAL;
        }
        uint64_t tval = (uint64_t)1 << 16;
        tval |= (uint64_t)1 << 8;
        tval |= (2 * skip - 1);
        res = usdr_dme_set_uint(dmdev, "/dm/sdr/0/rfe/throttle", tval);
    }
    else
    {
        res = usdr_dme_set_uint(dmdev, "/dm/sdr/0/rfe/throttle", 0);
    }

    if(res == -ENOENT)
    {
        USDR_LOG("TRTL", USDR_LOG_WARNING, "throttle is not implemented, ignoring...\n");
        res = 0;
    }
    return res;
}

static
    int set_endpoint_uint_param(pdm_dev_t dmdev, const char* endpoint, uint64_t chans,
                            uint64_t param, uint64_t * actual_param)
{
    int res = usdr_dme_set_uint(dmdev, "/dm/sdr/channels", chans);
    res = (res == -ENOENT) ? 0 : res;

    res = res ? res : usdr_dme_set_uint(dmdev, endpoint, param);

    if(!res)
    {
        if( (res = usdr_dme_get_uint(dmdev, endpoint, actual_param)) == -ENOENT)
        {
            *actual_param = param;
            res = 0;
        }
    }

    return res;
}

static const char* sync_type_to_str(unsigned sync_type)
{
    switch (sync_type) {
    case 1: return "1pps";
    case 2: return "rx";
    case 3: return "tx";
    case 4: return NULL;
    case 5: return "any";
    case 6: return NULL;
    case 7: return "none";
    default:
        return "off";
    }
}

int generic_rpc_call(pdm_dev_t dmdev,
                     pusdr_dms_t* usds,
                     struct sdr_call* sdrc,
                     unsigned response_maxlen,
                     char* response,
                     char* request)
{
    int res = 0;

    sdr_type_t sdrtype;
    res = dmdev->lldev->ops->generic_get(dmdev->lldev, LLGO_DEVICE_SDR_TYPE, (const char**)&sdrtype);
    if(res)
        return res;

    const struct sdr_call *pcall = sdrc;
    unsigned outbufsz = response_maxlen;
    char* outbuffer = response;

    unsigned long chans = (pcall->params.parameters_type[SDRC_CHANS] == SDRC_PARAM_TYPE_INT) ?
                              pcall->params.parameters_uint[SDRC_CHANS] : 0;

    //common calls
    switch (pcall->call_type) {
    case SDR_GET_REVISION:
    {
        uint64_t raw_rev;

        res = usdr_dme_get_uint(dmdev, "/dm/revision", &raw_rev);
        if(res)
            return res;

        uint32_t rev = raw_rev & 0x00000000ffffffff;

        unsigned day = (rev >> 27) & 0x1f;
        unsigned month = (rev >> 23) & 0x0f;
        unsigned year = (rev >> 17) & 0x3f;
        unsigned hour = (rev >> 12) & 0x1f;
        unsigned min = (rev >> 6) & 0x3f;
        unsigned sec = (rev >> 0) & 0x3f;

        rev = (raw_rev >> 32) & 0x00000000ffffffff;

        unsigned devrev = (rev >> 8) & 0xff;
        unsigned devidf = (rev >> 16) & 0xff;
        unsigned devid = (devidf << 24) | (devrev << 4);

        char device_name[16] = {0};

        //FIXME! Temporary decision to preserve compatibility
        switch(sdrtype) {
        case SDR_XSDR: strncpy(device_name, "xsdr",         sizeof(device_name)); break;
        case SDR_USDR: strncpy(device_name, "usdr",         sizeof(device_name)); break;
        case SDR_LIME: strncpy(device_name, "limesdr_mini", sizeof(device_name)); break;
        }
        //

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"revision\":\"%04d%02d%02d%02d%02d%02d\",\"devid\":\"%d\",\"devrev\":\"%d\",\"device\":\"%s\"}",
                 2000 + year, month, day, hour, min, sec, devid, devrev, device_name);
        return 0;
    }
    case SDR_RX_FREQUENCY:
    case SDR_TX_FREQUENCY:
    {
        uint64_t freq = (pcall->params.parameters_type[SDRC_FREQUENCTY] == SDRC_PARAM_TYPE_INT) ?
                            pcall->params.parameters_uint[SDRC_FREQUENCTY] : 0;
        uint64_t actual;

        res = set_endpoint_uint_param(dmdev,
                                      (pcall->call_type == SDR_RX_FREQUENCY) ? "/dm/sdr/0/rx/freqency" : "/dm/sdr/0/tx/freqency",
                                      chans,
                                      freq,
                                      &actual);
        if (res)
            return res;

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"details\":{\"actual-frequency\":%" PRIu64 "}}",
                 actual);
        return 0;
    }
    case SDR_RX_BANDWIDTH:
    case SDR_TX_BANDWIDTH:
    {
        uint64_t freq = (pcall->params.parameters_type[SDRC_FREQUENCTY] == SDRC_PARAM_TYPE_INT) ?
                            pcall->params.parameters_uint[SDRC_FREQUENCTY] : 0;
        uint64_t actual;

        res = set_endpoint_uint_param(dmdev,
                                      (pcall->call_type == SDR_RX_BANDWIDTH) ? "/dm/sdr/0/rx/bandwidth" : "/dm/sdr/0/tx/bandwidth",
                                      chans,
                                      freq,
                                      &actual);
        if (res)
            return res;

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"details\":{\"actual-frequency\":%" PRIu64 "}}",
                 actual);
        return 0;
    }
    case SDR_RX_GAIN:
    case SDR_TX_GAIN:
    {
        int gain = (pcall->params.parameters_type[SDRC_GAIN] == SDRC_PARAM_TYPE_INT) ?
                       pcall->params.parameters_uint[SDRC_GAIN] : 0;
        uint64_t actual;

        res = set_endpoint_uint_param(dmdev,
                                      (pcall->call_type == SDR_RX_GAIN) ? "/dm/sdr/0/rx/gain" : "/dm/sdr/0/tx/gain",
                                      chans,
                                      gain,
                                      &actual);
        if (res)
            return res;

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"details\":{\"actual-gain\":%d}}",
                 (int)actual);
        return 0;
    }
    case SDR_INIT_STREAMING:
    {
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
        unsigned chmsk = (chans == 1) ? 0x1 : (chans == 2) ? 0x3 : 0;
        usdr_dms_nfo_t rx_stream_info;
        unsigned blocksize = 0, bursts = 0;

        fmt = fmt ? fmt : SFMT_CI16;
        samplerate = samplerate ? samplerate : 1000000;
        pktsyms = pktsyms ? pktsyms : 4096;
        chmsk = chmsk ? chmsk : 0x1;
        unsigned rates[4] = { (mode & 1) ? samplerate : 0, (mode & 2) ? samplerate : 0, 0, 0 };

        if (ctrl_flags)
            fprintf(stderr, "Enabled extended stats on RX\n");

        for(int i = 0; i < 2; ++i)
            if( usds[i] && (mode & (i+1)) )
                res = res ? res : usdr_dms_op(usds[i], USDR_DMS_STOP, 0);

        res = res ? res : usdr_dme_set_uint(dmdev, "/dm/rate/rxtxadcdac", (uintptr_t)rates);

        if(mode & 1)
        {
            res = res ? res : usdr_dms_create_ex(dmdev, "/ll/srx/0", fmt, chmsk, pktsyms, ctrl_flags, &usds[0]);
            if(!res)
            {
                res = usdr_dms_info(usds[0], &rx_stream_info);
                blocksize = res ? 0 : rx_stream_info.pktbszie;
                bursts = rx_stream_info.burst_count;
            }
        }
        if(mode & 2)
        {
            res = res ? res : usdr_dms_create_ex(dmdev, "/ll/stx/0", fmt, chmsk, pktsyms, 0, &usds[1]);
        }

        if(res)
            return res;

        const char* stypes = sync_type_to_str(sync_type);
        if(!stypes)
            return -ENOTSUP;

        USDR_LOG("DSTR", USDR_LOG_INFO, "Start data: %d, sync type: %d => %s\n",
                 stream_start, sync_type, stypes);


        int streams_num = 0;
        pusdr_dms_t* streams = NULL;

        if((mode & 3) == 3)
        {
            streams = usds;
            streams_num = 2;
        }
        else if(mode & 1)
        {
            streams = usds;
            streams_num = 1;
        }
        else if(mode & 2)
        {
            streams = usds + 1;
            streams_num = 1;
        }
        else
            return -ENOTSUP;

        res = res ? res : usdr_dms_sync(dmdev, sync_type_to_str(0), streams_num, streams);

        for(int i = 0; i < 2; ++i)
            if(mode & (i+1))
                res = res ? res : usdr_dms_op(usds[i], stream_start ? USDR_DMS_START : USDR_DMS_STOP, 0);

        res = res ? res : set_throttle(dmdev, throttleon, samplerate);
        res = res ? res : usdr_dms_sync(dmdev, stypes, streams_num, streams);

        if(res)
            return res;

        snprintf(outbuffer, outbufsz, "{\"result\":0,\"details\":{\"wire-block-size\":%d,\"wire-bursts\":%d}}",
                 blocksize, bursts);
        return 0;
    }
    case SDR_STOP_STREAMING:
    {
        for(int i = 0; i < 2; ++i)
            if(usds[i])
            {
                res = res ? res : usdr_dms_op(usds[i], USDR_DMS_STOP, 0);
                res = res ? res : usdr_dms_destroy(usds[i]);
                usds[i] = NULL;
            }

        if(res)
            return res;

        snprintf(outbuffer, outbufsz, "{\"result\":0}");
        return 0;
    }
    case SDR_CRTL_STREAMING:
    {
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
        unsigned rates[4] = { samplerate, samplerate, 0, 0 };

        if (stream_restart && !dont_touch_tm)
            for(int i = 0; i < 2; ++i)
                if(usds[i])
                    res = res ? res : usdr_dms_op(usds[i], USDR_DMS_STOP, 0);

        if ((stream_start || dont_touch_tm) && samplerate != 0)
        {
            res = res ? res : usdr_dme_set_uint(dmdev, "/dm/rate/rxtxadcdac", (uintptr_t)rates);
            res = res ? res : set_throttle(dmdev, throttleon, samplerate);
        }

        if (!dont_touch_tm)
        {
            for(int i = 0; i < 2; ++i)
                if(usds[i])
                    res = res ? res : usdr_dms_op(usds[i], stream_start ? USDR_DMS_START : USDR_DMS_STOP, 0);

            const char* stypes = sync_type_to_str(sync_type);
            if(!stypes)
                return -ENOTSUP;

            res = res ? res : usdr_dms_sync(dmdev, stypes, 2, usds);
        }

        if(res)
            return res;

        snprintf(outbuffer, outbufsz, "{\"result\":%d}", res);
        return 0;
    }
    case SDR_DEBUG_DUMP:
    {
        union {
            uint32_t i32[2];
            uint64_t i64;
        } data[2];

        int res = usdr_dme_get_uint(dmdev, "/dm/debug/all", &data[0].i64);
        if(res)
            return res;

        USDR_LOG("DSTR", USDR_LOG_INFO, "DUMP - %08x - %08x - %08x - %08x\n",
                 data[0].i32[0], data[0].i32[1], data[1].i32[0], data[1].i32[1]);

        snprintf(outbuffer, outbufsz, "{\"result\":0}");
        return 0;
    }
    default:
        break; //should process it in particular call
    }

    //particular calls (we should get rid of them soon)
    switch(sdrtype) {
    case SDR_XSDR : {
        return xsdr_call(dmdev, sdrc, response_maxlen, response, request);
    }
    case SDR_USDR:  {
        return usdr_call(dmdev, sdrc, response_maxlen, response, request);
    }
    case SDR_LIME:  {
        return lime_call(dmdev,sdrc,response_maxlen,response,request);
    }

    default:
        return -EINVAL;
    }
}

