// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "tps6381x.h"
#include "def_tps6381x.h"

#include <usdr_logging.h>

#include <stdlib.h>

enum {
    VMIN = 1800,
    VMAX = 4975,
};

// VOUT1 - VSEL is low
// VOUT2 - VSEL is high

static int tps6381x_reg_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                            uint8_t taddr, uint8_t *pv)
{
    return lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        USDR_LSOP_I2C_DEV, ls_op_addr,
                                        1, pv, 1, &taddr);
}

static int tps6381x_reg_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                            uint8_t a, uint8_t r)
{
    uint8_t tv[] = { a, r };
    return lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        USDR_LSOP_I2C_DEV, ls_op_addr,
                                        0, NULL, 2, &tv);
}

int tps6381x_check_pg(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr, bool* ppg)
{
    uint8_t reg_status = 0xff;
    int res = tps6381x_reg_get(dev, subdev, ls_op_addr, STATUS, &reg_status);
    *ppg = (reg_status == 0x00);
    return res;
}

int tps6381x_init(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                  bool enable, bool force_pwm, int vout_mv)
{
    int res;
    uint8_t id;

    res = tps6381x_reg_get(dev, subdev, ls_op_addr, DEVID, &id);
    if (res) {
        return res;
    }

    if (id != 0x04) {
        USDR_LOG("6381", USDR_LOG_ERROR, "Unable to initialize tps6381x, id=%d\n", id);
        return -ENODEV;
    }

    if ((vout_mv < VMIN) || (vout_mv > VMAX)) {
        return -ERANGE;
    }

    uint8_t reg_ctrl = (uint8_t)MAKE_TPS6381X_CONTROL(0, enable ? 1 : 0,
                                                      force_pwm ? 1 : 0,
                                                      force_pwm ? 1 : 0, 0);
    uint8_t reg_vout = (vout_mv - VMIN) / 25;

    res = (res) ? res : tps6381x_reg_set(dev, subdev, ls_op_addr, CONTROL, reg_ctrl);
    res = (res) ? res : tps6381x_reg_set(dev, subdev, ls_op_addr, VOUT1, reg_vout);
    res = (res) ? res : tps6381x_reg_set(dev, subdev, ls_op_addr, VOUT2, reg_vout);
    return res;
}

