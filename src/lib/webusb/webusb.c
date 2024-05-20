// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "webusb.h"
#include "tiny-json.h"
#include "controller.h"

#include "usdr_lowlevel.h"
#include "usdr_logging.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "usdr_logging.h"

#define MAX_LOG_LINE 512

#if 0
void usdrlog_vout(unsigned loglevel,
                  const char* subsystem,
                  const char* function,
                  const char* file,
                  int line,
                  const char* fmt,
                  va_list list)
{
    if(!swd) return;

    if (loglevel > USDR_LOG_TRACE)
        loglevel = USDR_LOG_TRACE;

    if (!usdr_check_level(loglevel, subsystem))
        return;

    char buf[MAX_LOG_LINE];
    size_t stsz = 0;
    int sz;

    sz = snprintf(buf + stsz, sizeof(buf) - stsz, " [%4.4s] ",
                  subsystem);
    if (sz < 0) {
        buf[MAX_LOG_LINE - 1] = 0;
        goto out_truncated;
    }
    stsz += (size_t)sz;

    sz = vsnprintf(buf + stsz, sizeof(buf) - stsz, fmt, list);
    if (sz < 0) {
        buf[MAX_LOG_LINE - 1] = 0;
        goto out_truncated;
    }
    stsz += (size_t)sz;

out_truncated:
    swd->ops->log(swd->param, loglevel, buf);
}
#endif

#if 0
void usdrlog_out(unsigned loglevel,
                 const char* subsystem,
                 const char* function,
                 const char* file,
                 int line,
                 const char* fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    usdrlog_vout(loglevel, subsystem, function, file, line, fmt, ap);
    va_end(ap);
}
#endif
#if 0
lowlevel_ops_t *lowlevel_get_ops(lldev_t dev)
{
    struct webusb_device* wdev = (struct webusb_device*)dev;
    return wdev->ll.ops;
}

const char* lowlevel_get_devname(lldev_t dev)
{
    //struct webusb_device* wdev = (struct webusb_device*)dev;
    return "webusb";
}
#endif
int webusb_create(struct webusb_ops* ctx,
                  uintptr_t param,
                  unsigned loglevel,
                  unsigned vidpid,
                  pdm_dev_t* dmdev)
{
    int res = 0;

    usdrlog_setlevel( NULL, loglevel );
    usdrlog_disablecolorize( NULL );
    usdrlog_set_log_op(ctx->log);

    res = usdr_dmd_create_webusb(vidpid, ctx, param, dmdev);
    if(res)
        return res;

    printf("V202312021512 webusb_create created type=%d\n", ((webusb_device_t*)((*dmdev)->lldev))->type_sdr);
    return 0;
}

struct idx_list {
    const char *param;
    unsigned idx;
};

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

int webusb_get_req_method(const char* method)
{
    for (unsigned i = 0; i < SIZEOF_ARRAY(s_method_list); i++) {
         if (!strcmp(method, s_method_list[i].param))
             return s_method_list[i].idx;
    }
    return SDR_NOP;
}

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


int webusb_parse_parameter(const char* parameter)
{
    for (unsigned i = 0; i < SIZEOF_ARRAY(s_param_list); i++) {
         if (!strcmp(parameter, s_param_list[i].param))
             return s_param_list[i].idx;
    }
    return -1;
}

int webusb_get_req_parameters(struct sdr_call *psdrc, json_t const* parent)
{
    for (json_t const* child = json_getChild( parent ); child != 0; child = json_getSibling( child ) ) {
        jsonType_t propertyType = json_getType( child );
        char const* name = json_getName( child );

        int param_idx = webusb_parse_parameter(name);
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



static
int _dif_process_cmd(webusb_device_t* dev, char *cmd, unsigned len,
                         char* reply, unsigned rlen)
{
    uint64_t oval = 0;
    int res = -EINVAL;
    int j;
    char *pptr[4] = {NULL,};
    char *str1 = NULL, *saveptr1 = NULL, *token = NULL;

    USDR_LOG("DBGS", USDR_LOG_DEBUG, "got cmd: %s\n", cmd);

    for (j = 0, str1 = cmd; j < 4; j++, str1 = NULL) {
        token = strtok_r(str1, ",", &saveptr1);
        if (token == NULL)
            break;

        pptr[j] = token;
    }

    // CMD,PATH,VALUE
    // CMD,PATH
    // dm_dev_entity_t entity;
    if (strcasecmp(pptr[0], "SETU64") == 0 && j == 3) {
        res = dev->dif_set_uint(dev, pptr[1], strtoull(pptr[2], NULL, 16));
    } else if (strcasecmp(pptr[0], "GETU64") == 0 && j == 2) {
        res = dev->dif_get_uint(dev, pptr[1], &oval);
    }

    if (res == 0) {
        return snprintf(reply, rlen, "OK,%016" PRIx64 "\n", oval);
    } else {
        return snprintf(reply, rlen, "FAIL,%d\n", res);
    }
}

int webusb_debug_rpc(pdm_dev_t dmdev,
        char* request,
        char* response,
        unsigned response_maxlen)
{
    char* end = strchr(request, '\n');
    if (end == NULL) {
        USDR_LOG("DBGS", USDR_LOG_INFO, "Incorrect CMD!\n");
        return -EIO;
    }

    // Terminate string
    *end = 0;

    int replen = _dif_process_cmd((webusb_device_t*)(dmdev->lldev), request, end - request,
                           response, response_maxlen);

    return replen;
}

#define MAX_JSON_OBJS 64
int webusb_process_rpc(
        pdm_dev_t dmdev,
        char* request,
        char* response,
        unsigned response_maxlen)
{
    int res;
    webusb_device_t* dev = (webusb_device_t*)(dmdev->lldev);
    struct sdr_call sdrc;
    sdrc.call_type = SDR_NOP;
    memset(sdrc.params.parameters_type, 0, sizeof(sdrc.params.parameters_type));

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
                sdrc.call_type = webusb_get_req_method(value);
            } else if (strcmp(name, "req_data") == 0 && value != NULL) {
                //USDR_LOG("WEBU", USDR_LOG_DEBUG, "JSON REQ_DATA: %s\n", value);
                unsigned idx = value - request;
                if (idx > 512)
                    return -EINVAL;
                unsigned len = strlen(value);

                sdrc.call_data_ptr = idx;
                sdrc.call_data_size = len;
            } else {
                USDR_LOG("WEBU", USDR_LOG_DEBUG, "JSON unknown text parameter: %s\n", name);
                return -EINVAL;
            }
            break;
        case JSON_OBJ:
            if (strcmp(name, "req_params") == 0) {
                res = webusb_get_req_parameters(&sdrc, child);
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

    // TODO: do call
    if (sdrc.call_type != SDR_NOP) {
        USDR_LOG("WEBU", USDR_LOG_DEBUG, "sdr_call(%d response=%p request=%p)\n",
                 sdrc.call_type, response, request);

        res = dev->rpc_call(dmdev, &sdrc, response_maxlen, response, request);
        if (res)
            return res;
    } else {
        return -ENOENT;
    }
    return 0;
}

int webusb_destroy(pdm_dev_t dmdev)
{
    return usdr_dmd_close(dmdev);
}
