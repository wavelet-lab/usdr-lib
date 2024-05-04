// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <inttypes.h>
#include "si549.h"
#include <usdr_logging.h>
#include <usdr_lowlevel.h>

static
int si549_reg_rd(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                 uint8_t addr, uint8_t* val)
{
    return lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        USDR_LSOP_I2C_DEV, ls_op_addr,
                                        1, val, 1, &addr);
}

static
int si549_reg_wr(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                 uint8_t addr, uint8_t val)
{
    uint8_t data[2] = {addr, val};
    return lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        USDR_LSOP_I2C_DEV, ls_op_addr,
                                        0, NULL, 2, data);
}

enum si549_helpers {
    si549_Fvco_min = 10800000000,
    si549_Fvco_max = 12109728345,
    si549_Fout_min = 200000,
    si549_Fout_max = 800000000,
    si549_xtal = 152600000,
    si549_hsdiv_min = 5,
    si549_hsdiv_max = 2046,
};

int si549_set_freq(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                   unsigned output)
{
    if ((output < si549_Fout_min) || (output > si549_Fout_max))
        return -ERANGE;

    uint64_t fvco;
    unsigned lsdiv;
    unsigned lsdiv_rv;
    unsigned hsdiv = 1;
    unsigned fbdiv_frac;
    unsigned fbdiv;

    //Fvco = (Fosc x FBDIV)
    //Fout = Fvco / (HSDIV x LSDIV)
    for (lsdiv = 1, lsdiv_rv = 0; lsdiv != 64; lsdiv = lsdiv << 1, lsdiv_rv++) {
        fvco = (uint64_t)output * lsdiv;
        if (fvco * si549_hsdiv_max < si549_Fvco_min)
            continue;

        hsdiv = si549_Fvco_max / fvco;
        if (hsdiv & 1)
            hsdiv++;

        if (hsdiv > si549_hsdiv_max)
            hsdiv = si549_hsdiv_max;

        fvco *= hsdiv;
        break;
    }

    fbdiv = fvco / si549_xtal;
    uint64_t frem =  fvco % si549_xtal;
    frem <<= 32;
    frem /= si549_xtal;
    fbdiv_frac = frem;

    USDR_LOG("S549", USDR_LOG_INFO, "Si549 LSDIV=%d/%d, HSDIV=%d, VCO=%" PRIu64 " FBDIV=%u/%u\n", lsdiv, lsdiv_rv, hsdiv, fvco, fbdiv, fbdiv_frac);
    si549_reg_wr(dev, subdev, addr, 255, 0);
    si549_reg_wr(dev, subdev, addr, 69, 0);
    si549_reg_wr(dev, subdev, addr, 17, 0);

    si549_reg_wr(dev, subdev, addr, 23, hsdiv & 0xff);
    si549_reg_wr(dev, subdev, addr, 24, ((lsdiv_rv) << 4) | ((hsdiv >> 8) & 0x7));
    si549_reg_wr(dev, subdev, addr, 26, fbdiv_frac & 0xff);
    si549_reg_wr(dev, subdev, addr, 27, (fbdiv_frac >> 8) & 0xff);
    si549_reg_wr(dev, subdev, addr, 28, (fbdiv_frac >> 16) & 0xff);
    si549_reg_wr(dev, subdev, addr, 29, (fbdiv_frac >> 24) & 0xff);
    si549_reg_wr(dev, subdev, addr, 30, fbdiv & 0xff);
    si549_reg_wr(dev, subdev, addr, 31, (fbdiv >> 8) & 0xff);

    si549_reg_wr(dev, subdev, addr, 7, 8);
    si549_reg_wr(dev, subdev, addr, 17, 1);

    return 0;
}

int si549_enable(lldev_t dev, subdev_t subdev, lsopaddr_t addr, bool enable)
{
    return si549_reg_wr(dev, subdev, addr, 17, enable ? 1 : 0);
}

int si549_dump(lldev_t dev, subdev_t subdev, lsopaddr_t addr)
{
    int res;
    unsigned regs[] = {7, 17, 23, 24, 26, 27, 28, 29, 30, 31, 69, 255};
    for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        uint8_t o;
        res = si549_reg_rd(dev, subdev, addr, regs[i], &o);
        if (res)
            return res;

        USDR_LOG("S549", USDR_LOG_INFO, "Si549 REG%d = %02x\n", regs[i], o);
    }

    return 0;
}
