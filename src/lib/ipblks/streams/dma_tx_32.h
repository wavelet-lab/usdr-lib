// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DMA_TX_32
#define DMA_TX_32

#include <usdr_port.h>
#include <usdr_lowlevel.h>

#include "streams.h"


int dma_tx32_configure(lldev_t dev,
                       subdev_t subdev,
                       unsigned cfg_dma_base,
                       const struct fifo_config* pfc);
#endif
