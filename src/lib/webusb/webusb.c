// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "webusb.h"
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

int webusb_process_rpc(
        pdm_dev_t dmdev,
        char* request,
        char* response,
        unsigned response_maxlen)
{
    int res = 0;
    struct sdr_call sdrc;

    json_t storage[MAX_JSON_OBJS];
    json_t const* parent = allocate_json(request, storage, MAX_JSON_OBJS);
    if(!parent)
    {
        return -EINVAL;
    }

    res = controller_prepare_rpc(request, &sdrc, parent);
    if(res)
        return res;

    if (sdrc.call_type != SDR_NOP) {
        USDR_LOG("WEBU", USDR_LOG_DEBUG, "sdr_call(%d response=%p request=%p)\n",
                 sdrc.call_type, response, request);

        webusb_device_t* dev = (webusb_device_t*)(dmdev->lldev);
        res = dev->rpc_call(dmdev, dev->strms, &sdrc, response_maxlen, response, request);
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
