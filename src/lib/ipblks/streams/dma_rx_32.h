// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DMA_RX_32
#define DMA_RX_32

#include <usdr_port.h>
#include <usdr_lowlevel.h>

#include "streams.h"


int dma_rx32_reset(lldev_t lldev,
                   subdev_t subdev,
                   unsigned sx_base,
                   unsigned sx_cfg_base);

int dma_rx32_fe_reset(lldev_t dev,
                      subdev_t subdev,
                      unsigned cfg_dma_base,
                      bool reset);

enum dma_rx32_flags {
    ENABLE_TX_STATS = 1,
};

int dma_rx32_configure(lldev_t dev,
                       subdev_t subdev,
                       unsigned cfg_dma_base,
                       const struct fifo_config* pfc,
                       unsigned flags);
#endif
