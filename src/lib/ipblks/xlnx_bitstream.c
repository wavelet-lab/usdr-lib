// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "xlnx_bitstream.h"
#include <usdr_logging.h>
#include <string.h>

enum {
    W_DUMMY = 0xffffffff,
    W_BYTEORD = 0x000000bb,
    W_BUSWIDTH = 0x11220044,
    W_SYNCWORD = 0xAA995566,
    W_NOP = 0x20000000,
};

int xlnx_btstrm_parse_header(const uint32_t* mem, unsigned len, xlnx_image_params_t* stat)
{
    uint16_t blk_param;
    uint16_t blk_pcount;
    uint32_t w;
    unsigned ptr = 0;
    bool devid_found = false;
    bool wbstar_found = false;

    memset(stat, 0, sizeof(*stat));

    // Sync FSM
    bool bo_seen = false;
    bool bw_seen = false;
    for (; ptr < len; ptr++) {
        w = be32toh(mem[ptr]);
        switch (w) {
        case W_DUMMY:
            continue;
        case W_BYTEORD:
            bo_seen = true;
            continue;
        case W_BUSWIDTH:
            bw_seen = true;
            continue;
        case W_SYNCWORD:
            ptr++;
            goto next;
        default:
            // Unrecognized symbol
            USDR_LOG("BSTR", USDR_LOG_DEBUG, "Unrecognised WORD: n=%d w=%08x\n", ptr, w);
            return -EINVAL;
        }
    }
next:
    if (!bo_seen || !bw_seen)
        return -EINVAL;

    // Block FSM
    bool in_param = false;
    bool in_devid = false;
    bool in_wbstar = false;
    bool in_icmd = false;
    bool in_axss = false;
    for (; ptr < len; ptr++) {
        w = be32toh(mem[ptr]);
        if (in_param) {
            --blk_pcount;

            if (blk_pcount == 0)
                in_param = 0;

            if (in_devid) {
                stat->devid = w;
                devid_found = true;
            } else if (in_wbstar) {
                stat->wbstar = w;
                wbstar_found = true;
            } else if (in_icmd) {
                if (w == 0x0000000f)
                    stat->iprog = true;
            } else if (in_axss) {
                stat->usr_access2 = w;
            }
            continue;
        }

        if (w == W_NOP)
            continue;

        if (w >> 24 == 0x30) {
            blk_param = (w >> 8) & 0xffff;
            blk_pcount = w & 0xff;

            if (blk_pcount > 0) {
                in_param = true;
                in_devid = (blk_param == 0x0180);
                in_wbstar = (blk_param == 0x0200);
                in_icmd = (blk_param == 0x0080);
                in_axss = (blk_param == 0x01A0);
            }
            continue;
        }

        USDR_LOG("BSTR", USDR_LOG_DEBUG, "Unrecognised WORD: n=%d w=%08x\n", ptr, w);
        return -EINVAL;
    }

    return devid_found && wbstar_found ? 0 : -ENOENT;
}

int xlnx_btstrm_iprgcheck(
        const xlnx_image_params_t* internal_golden,
        const xlnx_image_params_t* newimg,
        unsigned wbstar,
        bool golden_image)
{
    if (internal_golden->devid != newimg->devid) {
        USDR_LOG("BSTR", USDR_LOG_ERROR, "FPGA Devid mismatch: FPGA=%08x in new image %08x\n",
                internal_golden->devid, newimg->devid);
        return -EINVAL;
    }
    if (newimg->wbstar == wbstar && !golden_image) {
        USDR_LOG("BSTR", USDR_LOG_ERROR, "The new image is the golden image, but requested master!\n");
        return -EINVAL;
    }
    if (golden_image && (newimg->wbstar != wbstar || !newimg->iprog)) {
        USDR_LOG("BSTR", USDR_LOG_ERROR, "You requested to update the golden image but image provided is not!\n");
        return -EINVAL;
    }
    if (!golden_image && (newimg->wbstar != 0)) {
        USDR_LOG("BSTR", USDR_LOG_ERROR, "You requested to update master image but golden image was provided!\n");
        return -EINVAL;
    }
    if (!golden_image && (internal_golden->wbstar != wbstar)) {
        USDR_LOG("BSTR", USDR_LOG_ERROR, "The FPGA golden image isn't aligned with the master image! Update the golden first\n");
        return -EINVAL;
    }
    return 0;
}
