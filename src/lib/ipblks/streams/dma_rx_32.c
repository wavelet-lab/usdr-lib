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
    DRX32_CFG_FERESET = 96,
    DRX32_CFG_DIS_LOWMRK = 97,
   // DRX32_CFG_AUTO_CNF = 98,
    DRX32_CFG_FORCE_PL128B = 99,
   // DRX32_CFG_OLB = 100,
    DRX32_CFG_EXT_NTFY = 100,
};

// RW:
// 0 Confirm register
// 1 Control register
// B1 -- FE RXTimerReset
// B0 -- Start/Stop



// RD:
// 0 Stat
// assign axis_stat_data[MAX_BUUFS_BITS:0] = dma_bufno_read;          BUF# confirmed by user
// assign axis_stat_data[MAX_BUUFS_BITS+8:8] = dma_bufno_reg;         BUF# transferred to host
// assign axis_stat_data[MAX_BUUFS_BITS+16:16] = bufno_fifo_filled;   BUF# filled in FIFO RAM
// assign axis_stat_data[MAX_BUUFS_BITS+24:24] = bufno_fill;          BUF# notification sent


int dma_rx32_reset(lldev_t lldev,
                   subdev_t subdev,
                   unsigned sx_base,
                   unsigned sx_cfg_base)
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

    res = dma_rx32_fe_reset(lldev, subdev, sx_cfg_base, 1);
    if (res)
        return res;

    res = dma_rx32_fe_reset(lldev, subdev, sx_cfg_base, 0);
    if (res)
        return res;

    // DMA STOP, FE RESET
    res = lowlevel_reg_wr32(lldev, subdev, sx_base + 1, 2);
    if (res)
        return res;

    res = lowlevel_reg_wr32(lldev, subdev, sx_base + 1, 0);
    if (res)
        return res;

    return 0;
}

// 1 FBURSTS
// 2 FBUFFS
int dma_rx32_fe_reset(lldev_t dev,
                      subdev_t subdev,
                      unsigned cfg_dma_base,
                      bool reset)
{
    return lowlevel_reg_wr32(dev, subdev,
                             cfg_dma_base + DRX32_CFG_FERESET, (reset) ? 1 : 0);
}

int dma_rx32_configure(lldev_t dev,
                       subdev_t subdev,
                       unsigned cfg_dma_base,
                       const struct fifo_config* pfc,
                       unsigned flags)
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


    res = lowlevel_reg_wr32(dev, subdev,
                            cfg_dma_base + DRX32_CFG_EXT_NTFY,
                            (flags & ENABLE_TX_STATS) ? 1 : 0);
    if (res)
        return res;

    return 0;
}

