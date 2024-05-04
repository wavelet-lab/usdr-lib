// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "sfe_tx_4.h"
#include <string.h>



int sfe_tx4_check_format(const struct stream_config* psc)
{
    struct bitsfmt bfmt = get_bits_fmt(psc->sfmt);

    if (bfmt.bits != 16)
        return -EINVAL;

    if (!bfmt.complex)
        return -EINVAL;

    return (psc->chmsk == 0x3 || psc->chmsk == 0x1) ? 0 : -EINVAL;
}

int sfe_tx4_push_ring_buffer(lldev_t dev,
                             subdev_t subdev,
                             unsigned cfg_base,
                             unsigned samples,
                             int64_t timestamp)
{
    if (samples > 8192)
        return -EINVAL;

    //  13 12
    uint32_t regs[2] = {
        ((timestamp >> 32) & 0xffff) | (((samples - 1) & 0x3fff) << 16) | ((timestamp < 0) ? 0x40000000 : 0),
        timestamp,
    };

    return lowlevel_ls_op(dev, subdev, USDR_LSOP_HWREG, cfg_base,
                              0, NULL, sizeof(regs), regs);
}


int sfe_tx4_ctl(lldev_t dev,
                subdev_t subdev,
                unsigned cfg_base,
                bool mimo,
                bool repeat,
                bool start)
{
    enum {
        GP_PORT_TXDMA_CTRL_MODE_REP  = 2,
        GP_PORT_TXDMA_CTRL_MODE_SISO = 3,
        GP_PORT_TXDMA_CTRL_MODE_INTER_OFF = 4,
        GP_PORT_TXDMA_CTRL_RESET_BUFS = 7,
        GP_PORT_TXDMA_CTRL_MODE_MUTEB = 8,
    };

    uint32_t cmd;
    int res;

    res = lowlevel_reg_wr32(dev, subdev, cfg_base + 2, 0);
    if (res)
        return res;

    if (!start)
        return 0;

    res = lowlevel_reg_wr32(dev, subdev, cfg_base + 2, (1 << GP_PORT_TXDMA_CTRL_RESET_BUFS));
    if (res)
        return res;

    cmd = (!mimo) ? ((1 << GP_PORT_TXDMA_CTRL_MODE_SISO) | (1 << GP_PORT_TXDMA_CTRL_MODE_MUTEB)) : 0;
    if (repeat)
        cmd |= (1 << GP_PORT_TXDMA_CTRL_MODE_REP);

    res = lowlevel_reg_wr32(dev, subdev, cfg_base + 2, cmd | 3);
    if (res)
        return res;

    return 0;
}
