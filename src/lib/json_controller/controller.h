// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <../device/device.h>
#include <usdr_lowlevel.h>

#include <../models/dm_dev.h>
#include <../models/dm_rate.h>
#include <../models/dm_stream.h>
#include <../models/dm_dev_impl.h>

#include "tiny-json.h"

#include "usdr_logging.h"

enum sdr_call_parameters {
    SDRC_CHANS,
    SDRC_SAMPLERATE,
    SDRC_DATA_FORMAT,
    SDRC_PACKETSIZE,
    SDRC_PARAM_TYPE,
    SDRC_FREQUENCTY,
    SDRC_GAIN,

    SDRC_OFFSET,
    SDRC_LENGTH,
    SDRC_CHECKSUM,
    SDRC_PARAM,
    SDRC_THROTTLE_ON,
    SDRC_MODE,
    SDRC_SENSOR,
    SDRC_VALUE,
    SDRC_PATH,
    //
    // daemon request params
    //
    SDRC_CONNECT_STRING,
    SDRC_FPS,
    SDRC_FFT_SIZE,
    SDRC_FFT_WINDOW_TYPE,
    SDRC_FFT_AVG,
    SDRC_UPPER_PWR_BOUND,
    SDRC_LOWER_PWR_BOUND,
    SDRC_DIVS_FOR_DB,
    SDRC_CONTRAST,
    SDRC_SATURATION,
    SDRC_STREAM_TYPE,
    SDRC_FFT_PROVIDER,
    SDRC_COMPRESSION,

    SDRC_PARAMS_MAX,
};

enum {
    SDRC_PARAM_TYPE_NULL = 0,
    SDRC_PARAM_TYPE_INT = 1,
    SDRC_PARAM_TYPE_REAL = 2,
    SDRC_PARAM_TYPE_STRING = 3,
};

struct sdr_call_paramteters {
    uintptr_t parameters_uint[SDRC_PARAMS_MAX];
    uint8_t  parameters_type[SDRC_PARAMS_MAX];
    unsigned parameters_len[SDRC_PARAMS_MAX];
};


enum sdr_call_type {
    SDR_NOP,
    SDR_INIT_STREAMING,
    SDR_RX_FREQUENCY,
    SDR_RX_GAIN,
    SDR_RX_BANDWIDTH,
    SDR_TX_FREQUENCY,
    SDR_TX_GAIN,
    SDR_TX_BANDWIDTH,
    SDR_STOP_STREAMING,
    SDR_FLASH_ERASE,
    SDR_FLASH_READ,
    SDR_FLASH_WRITE_SECTOR,
    SDR_DEBUG_DUMP,
    SDR_CRTL_STREAMING,
    SDR_GET_REVISION,
    SDR_CALIBRATE,
    SDR_GET_SENSOR,
    SDR_PARAMETER_SET,
    SDR_PARAMETER_GET,
    //
    // daemon requests
    //
    SDR_DISCOVER,
    SDR_CONNECT,
    SDR_DISCONNECT,
    SDR_RX_START_RAW_STREAM,
    SDR_RX_START_SA_STREAM,
    SDR_RX_START_RTSA_STREAM,
    SDR_RX_STOP_STREAM,
    SDR_RX_CONTROL_STREAM,
    SDR_GET_RX_STREAM_STATS,
};

enum {
    CTLXSDR_RXEXTSTATTX = 1,
};

enum {
    EP_CSR_OUT = 1,
    EP_CSR_IN  = 1,

    EP_CSR_NTFY = 2,
};

#define SSNS_SDR_TEMPERATURE "sdr_temp"

enum sensor_type {
    TSNS_NONE,
    TSNS_UNKNOWN,
    TSNS_SDR_TEMPERATURE,
};
typedef enum sensor_type sensor_type_t;

static inline sensor_type_t sensor_type_from_char(const char* sensor)
{
    if(!sensor)
        return TSNS_NONE;

    if(!strcmp(sensor, SSNS_SDR_TEMPERATURE))
        return TSNS_SDR_TEMPERATURE;

    return TSNS_UNKNOWN;
}

typedef int (*fn_callback_t)(void* obj, int type, unsigned parameter);

struct sdr_call {
    unsigned call_type;
    const char* call_req_ref;
    struct sdr_call_paramteters params;
    unsigned call_data_ptr;
    unsigned call_data_size;
};
typedef struct sdr_call sdr_call_t;

enum sdr_param_call {
    SDR_PC_SET_SAMPLERATE,
};

typedef int (*rpc_call_fn)(pdm_dev_t dmdev,
                           pusdr_dms_t* usds,
                           const struct sdr_call* sdrc,
                           unsigned response_maxlen,
                           char* response,
                           char* request);

int generic_rpc_call(pdm_dev_t dmdev,
                     pusdr_dms_t* usds,
                     const struct sdr_call* sdrc,
                     unsigned response_maxlen,
                     char* response,
                     char* request);

struct idx_list {
    const char *param;
    unsigned idx;
};

#define MAX_JSON_OBJS 64

json_t const* allocate_json(char* request, json_t storage[], unsigned qty);
int controller_prepare_rpc(char* request, sdr_call_t* psdrc, json_t const* parent);

void print_rpc_reply(const struct sdr_call* sdrc,
                     char* response,
                     unsigned response_maxlen,
                     int res,
                     const char* details_format,
                     ...);

#endif
