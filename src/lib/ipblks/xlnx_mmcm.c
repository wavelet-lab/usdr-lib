// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "xlnx_mmcm.h"
#include "mmcm_rom.h"

enum mmcm_regs {
    CLKOUT5_ClkReg1   = 0x06, //CLKOUT5 Register 1
    CLKOUT5_ClkReg2   = 0x07, //CLKOUT5 Register 2
    CLKOUT0_ClkReg1   = 0x08, //CLKOUT0 Register 1
    CLKOUT0_ClkReg2   = 0x09, //CLKOUT0 Register 2
    CLKOUT1_ClkReg1   = 0x0A, //CLKOUT1 Register 1
    CLKOUT1_ClkReg2   = 0x0B, //CLKOUT1 Register 2
    CLKOUT2_ClkReg1   = 0x0C, //CLKOUT2 Register 1 (!PLLE3)
    CLKOUT2_ClkReg2   = 0x0D, //CLKOUT2 Register 2 (!PLLE3)
    CLKOUT3_ClkReg1   = 0x0E, //CLKOUT3 Register 1 (!PLLE3)
    CLKOUT3_ClkReg2   = 0x0F, //CLKOUT3 Register 2 (!PLLE3)
    CLKOUT4_ClkReg1   = 0x10, //CLKOUT4 Register 1 (!PLLE3)
    CLKOUT4_ClkReg2   = 0x11, //CLKOUT4 Register 2 (!PLLE3)
    CLKOUT6_ClkReg1   = 0x12, //CLKOUT6 Register 1 (!PLLE3/2)
    CLKOUT6_ClkReg2   = 0x13, //CLKOUT6 Register 2 (!PLLE3/2)
    CLKFBOUT_ClkReg1  = 0x14, //CLKFBOUT Register 1
    CLKFBOUT_ClkReg2  = 0x15, //CLKFBOUT Register 2
    DIVCLK_DivReg     = 0x16, //DIVCLK Register
    LockReg1          = 0x18, //Lock Register 1
    LockReg2          = 0x19, //Lock Register 2
    LockReg3          = 0x1A, //Lock Register 3
    PowerRegUS        = 0x27, //Power Register (UltraScale / UltraScale+)
    PowerRegV7        = 0x28, //Power Register (7 series)
    FiltReg1          = 0x4E, //Filter Register 1
    FiltReg2          = 0x4F, //Filter Register 2
};

enum {
    DIG_DIV_MAX = 63,
    DIG_DLY_MAX = 63,
};

int mmcm_init_raw_clkout(lldev_t dev, subdev_t subdev,
                         unsigned drp_port, uint8_t clkout_reg,
                         const struct mmcm_port_config_raw *pcfg)
{
    int res;
    uint16_t clk1_reg_old, clk2_reg_old;
    uint16_t clk1_reg_out, clk2_reg_out;

    if (pcfg->period_h > DIG_DIV_MAX || pcfg->period_l > DIG_DIV_MAX || pcfg->delay > DIG_DLY_MAX)
        return -EINVAL;

    res = lowlevel_drp_rd16(dev, subdev, drp_port, clkout_reg, &clk1_reg_old);
    if (res)
        return res;

    clk1_reg_out = ((pcfg->phase & 7) << 13) |
            (clk1_reg_old & (1 << 12)) |
            ((pcfg->period_h & 0x3f) << 6) |
            (pcfg->period_l & 0x3f);

    res = lowlevel_drp_wr16(dev, subdev, drp_port, clkout_reg, clk1_reg_out);
    if (res)
        return res;

    res = lowlevel_drp_rd16(dev, subdev, drp_port, clkout_reg + 1, &clk2_reg_old);
    if (res)
        return res;

    // Chooses the edge that the High Time counter transitions on
    //
    // As an example, if a 50/50 duty cycle is desired with a divide value of 3, the Edge bit would be
    // set. The High Time counter would be set to one and the Low Time counter would be set to 2.
    // With the edge bit set, the net count for the High and Low times would be 1.5 clock cycles each.
    unsigned ht_edge = (pcfg->period_l - pcfg->period_h) == 1 ? 1 : 0;

    clk2_reg_out = (clk2_reg_old & 0xfc00) | (ht_edge << 7)  | (pcfg->delay & 0x3f);

    res = lowlevel_drp_wr16(dev, subdev, drp_port, clkout_reg + 1, clk2_reg_out);
    if (res)
        return res;

    USDR_LOG("MMCM", USDR_LOG_NOTE, " CLKREG %02x OLD: PHASE=%d HIGH=%d LOW=%d MX=%d EDGE=%d NO_CNT=%d DELAY=%2d | NEW: PHASE=%d HIGH=%d LOW=%d MX=%d EDGE=%d NO_CNT=%d DELAY=%2d\n",
             clkout_reg,
             (clk1_reg_old >> 13) & 0x7, (clk1_reg_old >> 6) & 0x3f, clk1_reg_old & 0x3f,
             (clk2_reg_old >> 8) & 0x3, (clk2_reg_old >> 7) & 1, (clk2_reg_old >> 6) & 1, (clk2_reg_old & 0x3f),
             (clk1_reg_out >> 13) & 0x7, (clk1_reg_out >> 6) & 0x3f, clk1_reg_out & 0x3f,
             (clk2_reg_out >> 8) & 0x3, (clk2_reg_out >> 7) & 1, (clk2_reg_out >> 6) & 1, (clk2_reg_out & 0x3f)
             );
    return 0;
}


int mmcm_init_raw_div(lldev_t dev, subdev_t subdev,
                      unsigned drp_port, int div)
{
    int res;
    uint16_t tmp, out;

    res = lowlevel_drp_rd16(dev, subdev, drp_port, DIVCLK_DivReg, &tmp);
    if (res)
        return res;

    out = (tmp & 0xC000) |
            ((div % 2) << 13) |
            ((div <= 1) ? (1 << 12) : 0) |
            (((div / 2) & 0x3f) << 6) |
            (((div + 1) / 2) & 0x3f);

    res = lowlevel_drp_wr16(dev, subdev, drp_port, DIVCLK_DivReg, out);
    if (res)
        return res;
    return 0;
}

// Lock1,2,3
int mmcm_init_raw_lock(lldev_t dev, subdev_t subdev, unsigned drpport, int div)
{
    uint16_t tmp, out;
    int res = 0;

    // Lock1
    res = res ? res : lowlevel_drp_rd16(dev, subdev, drpport, LockReg1, &tmp);
    out = (tmp & 0xfc00) | ((mmcm_rom[div] >> 20) & 0x3ff);
    res = res ? res : lowlevel_drp_wr16(dev, subdev, drpport, LockReg1, out);

    // Lock2
    res = res ? res : lowlevel_drp_rd16(dev, subdev, drpport, LockReg2, &tmp);
    out = (tmp & 0x8000) | (((mmcm_rom[div] >> 30) & 0x1f) << 10) | (mmcm_rom[div] & 0x3ff);
    res = res ? res : lowlevel_drp_wr16(dev, subdev, drpport, LockReg2, out);

    // Lock3
    res = res ? res : lowlevel_drp_rd16(dev, subdev, drpport, LockReg3, &tmp);
    out = (tmp & 0x8000) | (((mmcm_rom[div] >> 35) & 0x1f) << 10) | ((mmcm_rom[div] >> 10) & 0x3ff);
    res = res ? res : lowlevel_drp_wr16(dev, subdev, drpport, LockReg3, out);

    return res;
}

// Filt1,2
int mmcm_init_raw_filt(lldev_t dev, subdev_t subdev, unsigned drpport, int div, int h)
{
    uint16_t tmp, out;
    int res;

    unsigned tblval = (h) ? (mmcm_rom[div] >> 50) : ((mmcm_rom[div] >> 40) & 0x3ff);
    // Lock1
    res = lowlevel_drp_rd16(dev, subdev, drpport, FiltReg1, &tmp);
    if (res)
        return res;

    out = (((tblval >> 9) & 0x1) << 15) |
            (tmp & 0x6000) |
            (((tblval >> 7) & 0x3) << 11) |
            (tmp & 0x0600) |
            (((tblval >> 6) & 0x1) << 8) |
            (tmp & 0x00ff);

    res = lowlevel_drp_wr16(dev, subdev, drpport, FiltReg1, out);
    if (res)
        return res;

    // Lock2
    res = lowlevel_drp_rd16(dev, subdev, drpport, FiltReg2, &tmp);
    if (res)
        return res;

    out = (((tblval >> 5) & 0x1) << 15) |
            (tmp & 0x6000) |
            (((tblval >> 3) & 0x3) << 11) |
            (tmp & 0x0600) |
            (((tblval >> 1) & 0x3) << 7) |
            (tmp & 0x0060) |
            ((tblval & 0x1) << 4) |
            (tmp & 0x000f);

    res = lowlevel_drp_wr16(dev, subdev, drpport, FiltReg2, out);
    if (res)
        return res;

    return 0;
}

int mmcm_set_phdigdelay_raw(lldev_t dev, subdev_t subdev,
                            unsigned drp_port, unsigned port, unsigned phdelay)
{
    uint16_t reg_old, reg_out, clkout_reg = CLKOUT5_ClkReg1 + 2 * port;
    int res = 0;

    // VCO phase
    res = res ? res : lowlevel_drp_rd16(dev, subdev, drp_port, clkout_reg + 0, &reg_old);
    reg_out = ((phdelay & 7) << 13) | (reg_old & 0x1fff);
    res = res ? res : lowlevel_drp_wr16(dev, subdev, drp_port, clkout_reg + 0, reg_out);

    // Digital counter delay
    res = res ? res : lowlevel_drp_rd16(dev, subdev, drp_port, clkout_reg + 1, &reg_old);
    reg_out = (reg_old & 0xffc0) | ((phdelay >> 3) & 0x3f);
    res = res ? res : lowlevel_drp_wr16(dev, subdev, drp_port, clkout_reg + 1, reg_out);

    return res;
}


int mmcm_set_digdelay_raw(lldev_t dev, subdev_t subdev,
                          unsigned drp_port, unsigned port, unsigned delay)
{
    uint16_t reg_old, reg_out, clkout_reg = CLKOUT5_ClkReg1 + 2 * port + 1;
    int res = 0;

    res = res ? res : lowlevel_drp_rd16(dev, subdev, drp_port, clkout_reg, &reg_old);
    reg_out = (reg_old & 0xffc0) | (delay & 0x3f);
    res = res ? res : lowlevel_drp_wr16(dev, subdev, drp_port, clkout_reg, reg_out);
    return res;
}

int mmcm_init_raw(lldev_t dev, subdev_t subdev,
                  unsigned drp_port,
                  const struct mmcm_config_raw *cfg)
{
    int res;
    unsigned clkfbdiv = cfg->ports[CLKOUT_PORT_FB].period_h + cfg->ports[CLKOUT_PORT_FB].period_l;

    res = lowlevel_drp_wr16(dev, subdev, drp_port, PowerRegV7, 0xffff);
    if (res) {
        USDR_LOG("MMCM", USDR_LOG_ERROR, " unable to turn it on\n");
        return res;
    }

    for (unsigned i = 0; i < MAX_MMCM_PORTS; i++) {
        res = mmcm_init_raw_clkout(dev, subdev, drp_port, CLKOUT5_ClkReg1 + 2 * i,
                                   &cfg->ports[i]);
        if (res)
            return res;
    }

    // Input divide
    res = mmcm_init_raw_div(dev, subdev, drp_port, 1);
    if (res)
        return res;

    // Lock
    res = mmcm_init_raw_lock(dev, subdev, drp_port, clkfbdiv);
    if (res)
        return res;

    // Filter
    res = mmcm_init_raw_filt(dev, subdev, drp_port, clkfbdiv, 1);
    if (res)
        return res;

    return 0;
}


