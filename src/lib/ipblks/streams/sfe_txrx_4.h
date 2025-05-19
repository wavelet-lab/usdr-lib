// Copyright (c) 2023-2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef SFE_TXRX_4
#define SFE_TXRX_4

#include <usdr_port.h>
#include <usdr_logging.h>
#include <usdr_lowlevel.h>

#include "streams.h"

struct sfe_cfg {
    lldev_t dev;
    subdev_t subdev;

    unsigned cfg_fecore_id;

    unsigned cfg_base;
    unsigned cfg_fifomaxbytes;
    unsigned cfg_word_bytes;      // Data align in FE data stream
    unsigned cfg_dma_align_bytes; // Data align in DMA data stream
    unsigned cfg_raw_chans;       // Maximum number of raw channels in FE
};
typedef struct sfe_cfg sfe_cfg_t;

#endif
