// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "sfe_tx_4.h"
#include <string.h>
#include <usdr_logging.h>

//
enum {
    MAX_TX_FIFO_SZ = 126976
};

int sfe_tx4_check_format(const struct stream_config* psc)
{
    struct bitsfmt bfmt = get_bits_fmt(psc->sfmt);

    if (bfmt.bits != 16)
        return -EINVAL;

    if (!bfmt.complex)
        return -EINVAL;

    return 0;
}


int sfe_tx4_mtu_get(const struct stream_config* sc)
{
    (void)sc;
    return MAX_TX_FIFO_SZ;
}

int sfe_tx4_push_ring_buffer(lldev_t dev,
                             subdev_t subdev,
                             unsigned cfg_base,
                             unsigned samples,
                             int64_t timestamp)
{
    //  13 12
    uint32_t regs[2] = {
        ((timestamp >> 32) & 0x7fff) | (((samples - 1) & 0x7fff) << 15) | ((timestamp < 0) ? 0x40000000 : 0),
        timestamp,
    };

    return lowlevel_ls_op(dev, subdev, USDR_LSOP_HWREG, cfg_base,
                              0, NULL, sizeof(regs), regs);
}

enum {
    GP_PORT_TXDMA_CTRL_MODE_REP  = 2,
    GP_PORT_TXDMA_CTRL_MODE_FMT  = 3,

    GP_PORT_TXDMA_CTRL_RESET_BUFS = 7,
    GP_PORT_TXDMA_CTRL_MODE_MUTEB = 8,
    GP_PORT_TXDMA_CTRL_MODE_MUTEA = 9,
    GP_PORT_TXDMA_CTRL_MODE_SWAP_AB = 10,
    GP_PORT_TXDMA_CTRL_MODE_UPDATE = 11,

    TX_CMD_STOP = 0,
    TX_CMD_UPD_FLAGS = 1,
    TX_CMD_SRART_12BIT = 2,
    TX_CMD_START_16BIT = 3,
};

int fe_tx4_swap_ab_get(unsigned channels,
                       const channel_info_t* newmap,
                       unsigned* swap_ab)
{
    if (channels == 2 && (newmap->ch_map[0] == newmap->ch_map[1]))
        return -EINVAL;

    *swap_ab = (newmap->ch_map[0] == 1) ? 1 : 0;
    return 0;
}

int sfe_tx4_ctl(sfe_cfg_t *pfe,
                unsigned cfg_base,
                unsigned chans,
                uint8_t swap_ab_flag,
                uint8_t mute_flag,
                bool repeat,
                bool start)
{
    uint32_t cmd;
    int res;

    res = lowlevel_reg_wr32(pfe->dev, pfe->subdev, cfg_base + 2, TX_CMD_STOP);
    if (res)
        return res;

    if (!start)
        return 0;

    res = lowlevel_reg_wr32(pfe->dev, pfe->subdev, cfg_base + 2, (1 << GP_PORT_TXDMA_CTRL_RESET_BUFS));
    if (res)
        return res;

    cmd = ((chans) & 3) << GP_PORT_TXDMA_CTRL_MODE_FMT;
    if (mute_flag & 1) {
        cmd |= (1 << GP_PORT_TXDMA_CTRL_MODE_MUTEA);
    }
    if (mute_flag & 2) {
        cmd |= (1 << GP_PORT_TXDMA_CTRL_MODE_MUTEB);
    }
    if (swap_ab_flag) {
        cmd |= (1 << GP_PORT_TXDMA_CTRL_MODE_SWAP_AB);
    }
    if (repeat) {
        cmd |= (1 << GP_PORT_TXDMA_CTRL_MODE_REP);
    }

    res = lowlevel_reg_wr32(pfe->dev, pfe->subdev, cfg_base + 2, cmd | TX_CMD_START_16BIT);
    if (res)
        return res;

    return 0;
}

int sfe_tx4_upd(sfe_cfg_t *pfe,
                unsigned cfg_base,
                unsigned mute_flags,
                unsigned swap_ab_flag)
{
    uint32_t cmd = ((swap_ab_flag & 1) << GP_PORT_TXDMA_CTRL_MODE_SWAP_AB) |
                   ((mute_flags & 1) << GP_PORT_TXDMA_CTRL_MODE_MUTEA) |
                   (((mute_flags >> 1) & 1) << GP_PORT_TXDMA_CTRL_MODE_MUTEB);
    return lowlevel_reg_wr32(pfe->dev, pfe->subdev, cfg_base + 2, cmd | TX_CMD_UPD_FLAGS);
}


