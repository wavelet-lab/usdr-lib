// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "dma_tx_32.h"

#include <usdr_logging.h>

enum dma_tx32 {
    DMA_BUFFERS = 32,
};


int dma_tx32_configure(lldev_t dev,
                       subdev_t subdev,
                       unsigned cfg_dma_base,
                       const struct fifo_config* pfc)
{
    return -EINVAL;
}
