// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef STREAM_LIMESDR_H
#define STREAM_LIMESDR_H

#include "streams_api.h"

enum {
    LIMESDR_RX = 0,
    LIMESDR_TX = 1,
};

int create_limesdr_stream(device_t* device,
                          unsigned core_id,
                          const char* dformat,
                          logical_ch_msk_t channels,
                          unsigned pktsyms,
                          unsigned flags,
                          stream_handle_t** outu);


// Extra streaming commands to be sync with lower layer
enum {
    LIMESDR_OP_LMS7002_RESET = USDR_LSOP_CUSTOM_CMD + 0,
};

#endif
