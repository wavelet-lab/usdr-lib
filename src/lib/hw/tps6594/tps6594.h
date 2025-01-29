// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef TPS6594_H
#define TPS6594_H

#include <usdr_lowlevel.h>

enum tps6594_ch {
    TPS6594_BUCK1,
    TPS6594_BUCK2,
    TPS6594_BUCK3,
    TPS6594_BUCK4,
    TPS6594_BUCK5,
    TPS6594_LDO1,
    TPS6594_LDO2,
    TPS6594_LDO3,
    TPS6594_LDO4,
};

int tps6594_check(lldev_t dev, subdev_t subdev, lsopaddr_t addr);

int tps6594_reg_dump(lldev_t dev, subdev_t subdev, lsopaddr_t addr);

int tps6594_vout_set(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                     unsigned ch, unsigned vout);

int tps6594_vout_ctrl(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                      unsigned ch, bool en);

int tps6594_set_fr(lldev_t dev, subdev_t subdev, lsopaddr_t addr);

#endif
