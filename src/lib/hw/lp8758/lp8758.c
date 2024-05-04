// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "lp8758.h"

#include <usdr_logging.h>

enum {
    DEV_REV = 0x00,
    OTP_REV = 0x01,
    BUCK0_CTRL1 = 0x02,
    BUCK0_CTRL2 = 0x03,
    BUCK1_CTRL1 = 0x04,
    BUCK1_CTRL2 = 0x05,
    BUCK2_CTRL1 = 0x06,
    BUCK2_CTRL2 = 0x07,
    BUCK3_CTRL1 = 0x08,
    BUCK3_CTRL2 = 0x09,
    BUCK0_VOUT = 0x0a,
    BUCK0_FLOOR_VOUT = 0x0b,
    BUCK1_VOUT = 0x0c,
    BUCK1_FLOOR_VOUT = 0x0d,
    BUCK2_VOUT = 0x0e,
    BUCK2_FLOOR_VOUT = 0x0f,
    BUCK3_VOUT = 0x10,
    BUCK3_FLOOR_VOUT = 0x11,
    BUCK0_DELAY = 0x12,
    BUCK1_DELAY = 0x13,
    BUCK2_DELAY = 0x14,
    BUCK3_DELAY = 0x15,
    RESET = 0x16,
    CONFIG = 0x17,
    INT_TOP = 0x18,
    INT_BUCK_0_1 = 0x19,
    INT_BUCK_2_3 = 0x1a,
    TOP_STAT = 0x1b,
    BUCK_0_1_STAT = 0x1c,
    BUCK_2_3_STAT = 0x1d,
    TOP_MASK = 0x1e,
    BUCK_0_1_MASK = 0x1f,
    BUCK_2_3_MASK = 0x20,
    SEL_I_LOAD = 0x21,
    I_LOAD_2 = 0x22,
    I_LOAD_1 = 0x23,
};

enum {
    PMIC_CH_ENABLE = 0x88,
    PMIC_CH_DISABLE = 0xc8,
};

static
int lp8758_reg_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                          uint8_t reg, uint8_t out)
{
    uint8_t data[2] = { reg, out };
    return lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        USDR_LSOP_I2C_DEV, ls_op_addr,
                                        0, NULL, 2, data);
}

static
int lp8758_reg_rd(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                   uint8_t addr, uint8_t* val)
{
    return lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        USDR_LSOP_I2C_DEV, ls_op_addr,
                                        1, val, 1, &addr);
}

int lp8758_get_rev(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                  uint16_t* prev)
{
    uint8_t v = 0xff, d = 0xff;
    int res;

    res = lp8758_reg_rd(dev, subdev, addr, DEV_REV, (uint8_t*)&v);
    if (res)
        return res;
    res = lp8758_reg_rd(dev, subdev, addr, OTP_REV, (uint8_t*)&d);
    if (res)
        return res;

    USDR_LOG("8758", USDR_LOG_NOTE, "%s.%lld.%d: REV %02x:%02x\n",
             lowlevel_get_devname(dev),
             (long long)subdev, addr, v, d);

    *prev = (((uint16_t)d) << 8) | v;
    return 0;
}

static uint8_t s_get_vout(unsigned vin)
{
    unsigned x;

    if (vin > 3330) {
        return 0xFF;
    } else if (vin > 1400) {
        x = (vin - 1400) / 20;
        return (uint8_t)(0x9D + x);
    } else if (vin > 730) {
        x = (vin - 730) / 5;
        return (uint8_t)(0x18 + x);
    } else if (vin > 500) {
        x = (vin - 500) / 10;
        return (uint8_t)x;
    }

    return 0;
}


int lp8758_vout_set(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                     unsigned ch, unsigned vout)
{
    const uint8_t regs[] = {
        BUCK0_VOUT, BUCK1_VOUT, BUCK2_VOUT, BUCK3_VOUT,
    };
    if (ch > 3)
        return -EINVAL;

    USDR_LOG("8758", USDR_LOG_NOTE, "%s.%lld.%d: Set CH%d VOUT:%d\n",
             lowlevel_get_devname(dev),
             (long long)subdev, addr, ch, vout);

    return lp8758_reg_set(dev, subdev, addr, regs[ch],
                          s_get_vout(vout));
}

int lp8758_vout_ctrl(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                     unsigned ch, bool en, bool forcepwm)
{
    const uint8_t regs[] = {
        BUCK0_CTRL1, BUCK1_CTRL1, BUCK2_CTRL1, BUCK3_CTRL1,
    };
    if (ch > 3)
        return -EINVAL;

    USDR_LOG("8758", USDR_LOG_NOTE, "%s.%lld.%d: CH%d %s\n",
             lowlevel_get_devname(dev),
             (long long)subdev, addr, ch, en ? "enable" : "disable");

    return lp8758_reg_set(dev, subdev, addr, regs[ch],
                          en ? (PMIC_CH_ENABLE | (forcepwm ? 2 : 0)) : PMIC_CH_DISABLE);
}


int lp8758_check_pg(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                    unsigned chmsk, bool* pg)
{

    uint8_t st0, st1;
    int res;

    res = lp8758_reg_rd(dev, subdev, addr, BUCK_0_1_STAT, &st0);
    if (res)
        return res;

    res = lp8758_reg_rd(dev, subdev, addr, BUCK_2_3_STAT, &st1);
    if (res)
        return res;

    unsigned msk = 0;
    if ((st0 & (1<<2))) msk |= (1 << 0);
    if ((st0 & (1<<6))) msk |= (1 << 1);
    if ((st1 & (1<<2))) msk |= (1 << 2);
    if ((st1 & (1<<6))) msk |= (1 << 3);

    *pg = ((chmsk & msk) == msk);
    return 0;
}

int lp8758_ss(lldev_t dev, subdev_t subdev, lsopaddr_t addr, bool en)
{
    return lp8758_reg_set(dev, subdev, addr, 0x17, en ? 0x09 : 0x08);
}
