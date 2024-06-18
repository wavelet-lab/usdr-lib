// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "lp875484.h"
#include "def_lp875484.h"

#include <usdr_logging.h>

#include <stdlib.h>

#define LP875484_CHIP_ID_CHECK 0x84

static int lp875484_reg_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                   uint8_t reg, uint8_t out)
{
    uint8_t data[2] = { reg, out };
    return lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        USDR_LSOP_I2C_DEV, ls_op_addr,
                                        0, NULL, 2, data);
}

static int lp875484_reg_rd(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                  uint8_t addr, uint8_t* val)
{
    return lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        USDR_LSOP_I2C_DEV, ls_op_addr,
                                        1, val, 1, &addr);
}


int lp875484_init(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr)
{
    uint8_t v = 0xff;
    int res;

    res = lp875484_reg_rd(dev, subdev, ls_op_addr, CHIP_ID, (uint8_t*)&v);
    if (res)
        return res;

    USDR_LOG("8758", USDR_LOG_NOTE, "%s.%lld.%d: CHIP_ID %02x\n",
             lowlevel_get_devname(dev),
             (long long)subdev, ls_op_addr, v);

    if (v != LP875484_CHIP_ID_CHECK) {
        USDR_LOG("8758", USDR_LOG_WARNING, "%s.%lld.%d: Incorrect CHIP_ID %02x\n",
                 lowlevel_get_devname(dev),
                 (long long)subdev, ls_op_addr, v);
        return -ENODEV;
    }

    return 0;
}

int lp875484_set_vout(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr, unsigned out_mv)
{
    unsigned en = 1;
    unsigned regv;
    if (out_mv == 0) {
        regv = 10;
        en  = 0;
    } else if ((out_mv < 600) || (out_mv > 1700)) {
        USDR_LOG("8758", USDR_LOG_WARNING, "%s.%lld.%d: Vout is out of range %d mV!\n",
                 lowlevel_get_devname(dev),
                 (long long)subdev, ls_op_addr, out_mv);
        return -EINVAL;
    } else {
        regv = (out_mv - 500) / 10;
    }

    USDR_LOG("8758", USDR_LOG_NOTE, "%s.%lld.%d: Set output to %d mV (VOUT %02x EN %d)\n",
             lowlevel_get_devname(dev),
             (long long)subdev, ls_op_addr, out_mv, regv, en);

    return lp875484_reg_set(dev, subdev, ls_op_addr, VSET_B0, MAKE_LP875484_VSET_B0(en, regv));
}

int lp875484_is_pg(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr, bool *pg)
{
    uint8_t v = 0xff;
    int res;

    res = lp875484_reg_rd(dev, subdev, ls_op_addr, FLAGS, (uint8_t*)&v);
    if (res)
        return res;

    USDR_LOG("8758", USDR_LOG_NOTE, "%s.%lld.%d: nPG is %d, TEMP is %d (<85C, <120C, <150C, >150C)\n",
             lowlevel_get_devname(dev),
             (long long)subdev, ls_op_addr, GET_LP875484_N_PG_B0(v), GET_LP875484_TEMP(v));

    *pg = !GET_LP875484_N_PG_B0(v);
    return 0;
}

