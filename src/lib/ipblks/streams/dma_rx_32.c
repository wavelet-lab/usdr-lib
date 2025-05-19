// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "dma_rx_32.h"

#include <usdr_logging.h>

enum dma_rx32 {
    DMA_BUFFERS = 32,
};

enum dma_rx32_cfg_regs {
    DRX32_CFG_BWORDSZ = 32,
    DRX32_CFG_BBURSTSZ = 64,
};

int dma_rx32_reset(lldev_t lldev,
                   subdev_t subdev,
                   unsigned sx_base)
{
    int res;
    uint32_t tmp;

    res = lowlevel_reg_rd32(lldev, subdev, sx_base + 2, &tmp);
    if (res)
        return res;

    if (tmp & 0x40000000) {
        if (tmp == 0xffffffff) {
             USDR_LOG("DMRX", USDR_LOG_ERROR, "DMA engine is dead!\n");
             return -EIO;
        }
        USDR_LOG("DMRX", USDR_LOG_WARNING, "DMA engine is active!\n");
    }

    // DMA STOP, FE RESET
    res = lowlevel_reg_wr32(lldev, subdev, sx_base + 1, 2);
    if (res)
        return res;

    res = lowlevel_reg_wr32(lldev, subdev, sx_base + 1, 0);
    if (res)
        return res;

    return 0;
}

int dma_rx32_configure(lldev_t dev,
                       subdev_t subdev,
                       unsigned cfg_dma_base,
                       const struct fifo_config* pfc)
{
    int res;
    res = lowlevel_reg_wr32(dev, subdev,
                            cfg_dma_base + DRX32_CFG_BWORDSZ,
                            pfc->bpb / 8 - 1);
    if (res)
        return res;

    res = lowlevel_reg_wr32(dev, subdev,
                            cfg_dma_base + DRX32_CFG_BBURSTSZ,
                            pfc->burstspblk - 1);
    if (res)
        return res;


    return 0;
}

