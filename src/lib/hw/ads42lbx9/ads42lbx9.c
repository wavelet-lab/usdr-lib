// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "ads42lbx9.h"
#include <usdr_logging.h>

enum ads42lbx9 {
    REG_CLKDIV = 0x6,
    REG_SYNCINDELAY = 0x7,
    REG_PWR_CONTROL = 0x8,
    REG_GAIN_CHA = 0xB,
    REG_GAIN_CHB = 0xC,
    REG_HF0 = 0xD,
    REG_HF1 = 0xE,
    REG_CP_GEN = 0xF,
    REG_CP1_HI = 0x10,
    REG_CP1_LO = 0x11,
    REG_CP2_HI = 0x12,
    REG_CP2_LO = 0x13,
    REG_LVDS_OPTS = 0x14,
    REG_LVDS_MODE = 0x15,
    REG_DDR_TIMING = 0x16,
    REG_QDR_CHA = 0x17,
    REG_QDR_CHB = 0x18,
    REG_FAST_OVER_THRES = 0x1F,
    REG_OVR_CTRL = 0x20,
};

enum ads42lbx9_tps {
    TP_NORMAL = 0,
    TP_ALL_ZERO = 1,
    TP_ALL_ONES = 2,
    TP_ALT_ZERO_ONE = 3,// 1010101 0101010
    TP_RAMP_UP = 4,
    TP_CP1 = 6,
    TP_ALT_CP1_CP2 = 7,
    TP_AAAA = 8,
    TP_PRBS = 10,
    TP_8POINT = 11,
};

#define MAKE_CTRL(chae, chbe, pd, reset) \
    ((((chae) ? 0 : 1) << 7) | \
    (((chbe) ? 0 : 1) << 6) | \
    (((pd) ? 1 : 0) << 5) | \
    ((reset) ? 1 : 0))

static int ads42lbx9_wr(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                        uint8_t regno, uint8_t data)
{
    return lowlevel_spi_tr32(dev, subdev, ls_op_addr,
                             ((regno & 0x3f) << 8) | data, NULL);
}

static int ads42lbx9_rd(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                        uint8_t regno, uint8_t* odata)
{
    uint32_t od;
    int res = lowlevel_spi_tr32(dev, subdev, ls_op_addr,
                                (0x8000 | ((regno & 0x3f) << 8)),
                                &od);
    if (res)
        return res;

    *odata = od;
    return 0;
}

int ads42lbx9_ctrl(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                   bool chaen, bool chben, bool pd, bool reset)
{
    return ads42lbx9_wr(dev, subdev, ls_op_addr,
                        REG_PWR_CONTROL, MAKE_CTRL(chaen, chben, pd, reset));
}

static
int ads42lbx9_set_generic(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr, bool ena, bool enb)
{
    int res;
    //res = ads42lbx9_wr(dev, subdev, ls_op_addr, REG_PWR_CONTROL, 8 | 4 | (ena ? 0 : 0x80) | (enb ? 0 : 0x40) | ((ena || enb) ? 0 : 0x20));
    res = ads42lbx9_wr(dev, subdev, ls_op_addr, REG_PWR_CONTROL, 8 | 4 | ((ena || enb) ? 0 : 0x20));
    if (res)
        return res;

    res = ads42lbx9_wr(dev, subdev, ls_op_addr, REG_QDR_CHA, 0x80 | (0x5<<1));
    if (res)
        return res;

    res = ads42lbx9_wr(dev, subdev, ls_op_addr, REG_QDR_CHB, 0x5<<1);
    if (res)
        return res;

    res = ads42lbx9_wr(dev, subdev, ls_op_addr, REG_LVDS_OPTS, 12);
    if (res)
        return res;

    return 0;
}


int ads42lbx9_set_tp(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                     uint16_t p1, uint16_t p2)
{
    int res;
    res = ads42lbx9_wr(dev, subdev, ls_op_addr, REG_CP1_HI, p1 >> 8);
    if (res)
        return res;

    res = ads42lbx9_wr(dev, subdev, ls_op_addr, REG_CP1_LO, p1);
    if (res)
        return res;

    res = ads42lbx9_wr(dev, subdev, ls_op_addr, REG_CP2_HI, p2 >> 8);
    if (res)
        return res;

    res = ads42lbx9_wr(dev, subdev, ls_op_addr, REG_CP2_LO, p2);
    if (res)
        return res;

    res = ads42lbx9_set_generic(dev, subdev, ls_op_addr, true, true);
    if (res)
        return res;

#if 0
    res = ads42lbx9_wr(dev, subdev, ls_op_addr, REG_PWR_CONTROL, 8 | 4);
    if (res)
        return res;

    res = ads42lbx9_wr(dev, subdev, ls_op_addr, REG_QDR_CHA, 0x80 | (0x5<<1));
    if (res)
        return res;

    res = ads42lbx9_wr(dev, subdev, ls_op_addr, REG_QDR_CHB, 0x5<<1);
    if (res)
        return res;

    res = ads42lbx9_wr(dev, subdev, ls_op_addr, REG_LVDS_OPTS, 12);
    if (res)
        return res;
#endif
    return ads42lbx9_wr(dev, subdev, ls_op_addr, REG_CP_GEN, (TP_ALT_CP1_CP2 << 4) | TP_ALT_CP1_CP2);
}

int ads42lbx9_set_normal(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr, bool ena, bool enb)
{
    int res;
    res = ads42lbx9_set_generic(dev, subdev, ls_op_addr, ena, enb);
    if (res)
        return res;

    return ads42lbx9_wr(dev, subdev, ls_op_addr, REG_CP_GEN, 0);
}

int ads42lbx9_set_pbrs(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr)
{
    int res;
    res = ads42lbx9_set_generic(dev, subdev, ls_op_addr, true, true);
    if (res)
        return res;

    return ads42lbx9_wr(dev, subdev, ls_op_addr, REG_CP_GEN, (TP_PRBS << 4) | (TP_PRBS << 0));
}

int ads42lbx9_dump(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr)
{
    static const uint8_t regs[] = {
        REG_CLKDIV,
        REG_SYNCINDELAY,
        REG_PWR_CONTROL,
        REG_GAIN_CHA,
        REG_GAIN_CHB,
        REG_HF0,
        REG_HF1,
        REG_CP_GEN,
        REG_CP1_HI,
        REG_CP1_LO,
        REG_CP2_HI,
        REG_CP2_LO,
        REG_LVDS_OPTS,
        REG_LVDS_MODE,
        REG_DDR_TIMING,
        REG_QDR_CHA,
        REG_QDR_CHB,
        REG_FAST_OVER_THRES,
        REG_OVR_CTRL,
    };

    for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        uint8_t o;
        int res = ads42lbx9_rd(dev, subdev, ls_op_addr, regs[i], &o);
        if (res)
            return res;

        USDR_LOG("42L9", USDR_LOG_INFO, "ads42lbx9 REG%d = %02x\n", regs[i], o);
    }

    return 0;
}

int ads42lbx9_reset_and_check(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr)
{
    uint8_t o;
    int res = ads42lbx9_rd(dev, subdev, ls_op_addr, REG_HF0, &o);
    if (res)
        return res;

    USDR_LOG("42L9", USDR_LOG_NOTE, "ads42lbx9:%d REG_HF0 = %02x\n", ls_op_addr, o);
    if ((o & 0xFE) != 0x6C)
        return -ENODEV;

    return 0;
}
