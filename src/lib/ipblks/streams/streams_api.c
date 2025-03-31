// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <usdr_logging.h>
#include "streams_api.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>


int usdr_channel_info_map_default(const usdr_channel_info_t* channels, const channel_map_info_t* map, unsigned max_chnum, channel_info_t* core_chans)
{
    if (channels->phys_names == NULL && channels->phys_nums == NULL) {
        for (unsigned i = 0; i < channels->count; i++) {
            core_chans->ch_map[i] = i;
        }
        return 0;
    }

    if (channels->phys_names != NULL) {
        return channel_info_remap(channels->count, channels->phys_names, map, core_chans);
    }

    for (unsigned i = 0; i < channels->count; i++) {
        unsigned num = channels->phys_nums[i];
        if (num >= max_chnum)
            return -EINVAL;

        core_chans->ch_map[i] = num;
    }
    return 0;
}

int usdr_channel_info_string_parse(char* chanlist, unsigned max_chans, usdr_channel_info_t* cinfo)
{
    const char* delim = ":_-/";
    unsigned ncount = 0;
    unsigned scount = 0;

    char* saveptr;
    char* str1;
    unsigned t;

    if (cinfo->phys_names) {
        memset(cinfo->phys_names, 0, max_chans * sizeof(cinfo->phys_names[0]));
    }
    if (cinfo->phys_nums) {
        memset(cinfo->phys_nums, 0xff, max_chans * sizeof(cinfo->phys_nums[0]));
    }

    for (t = 0, str1 = chanlist; (ncount < max_chans) && (scount < max_chans); str1 = NULL, t++) {
        const char* token = strtok_r(str1, delim, &saveptr);
        if (token == NULL) {
            break;
        }

        if (isdigit(*token) && cinfo->phys_nums) {
            unsigned chn = atoi(token);
            cinfo->phys_nums[ncount++] = chn;
        } else if (isalpha(*token) && cinfo->phys_names) {
            cinfo->phys_names[scount++] = token;
        } else {
            USDR_LOG("STRM", USDR_LOG_ERROR, "Channel parsing: incorrect token# %d `%s`\n", t, token);
            return -EINVAL;
        }
    }

    if (ncount > 0 && scount > 0) {
        USDR_LOG("UDEV", USDR_LOG_ERROR, "Hardware and logical channel types mixing logic: %d, hw: %d\n", ncount, scount);
        return -EINVAL;
    }

    if (ncount == 0) {
        cinfo->phys_nums = NULL;
    }

    if (scount == 0) {
        cinfo->phys_names = NULL;
    }

    cinfo->count = (ncount > 0) ? ncount : scount;
    cinfo->flags = 0;
    return 0;
}
