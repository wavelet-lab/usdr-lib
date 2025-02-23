// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef STREAM_SFETRX4_DMA32_H
#define STREAM_SFETRX4_DMA32_H

#include "streams_api.h"


enum {
    CORE_SFERX_DMA32_R0 = 0,
    CORE_SFETX_DMA32_R0 = 1,


    CORE_EXFERX_DMA32_R0 = 256,
};

enum {
    DMS_FLAG_BIFURCATION = 65536,
    DMS_DONT_CHECK_FWID  = 131072,
};

struct sfetrx4_config
{
    struct parsed_data_format pfmt;
    struct stream_config sc;
    struct stream_config sc_b;

    unsigned cfgchs; // Number of requested channels
    unsigned logicchs; // Number of logical channels in data plan
    bool bifurcation_valid;

    // Storage
    char dfmt[256];
};

int parse_sfetrx4(const char* dformat, const channel_info_t* channels, unsigned pktsyms, unsigned int chcnt,
                  struct sfetrx4_config *out);

int create_sfetrx4_stream(device_t* device,
                          unsigned core_id,
                          const char* dformat,
                          unsigned int chcount,
                          channel_info_t *channels,
                          unsigned pktsyms,
                          unsigned flags,
                          unsigned sx_base,
                          unsigned sx_cfg_base,
                          unsigned fe_fifobsz,
                          unsigned fe_base,
                          stream_handle_t** outu,
                          unsigned *hw_chans_cnt);

// Syncronize streams
int sfetrx4_stream_sync(device_t* device,
                        stream_handle_t** pstream, unsigned scount,
                        const char* synctype);



#endif
