// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LP8758_H
#define LP8758_H

#include <usdr_lowlevel.h>

int lp8758_ss(lldev_t dev, subdev_t subdev, lsopaddr_t addr, bool en);

int lp8758_get_rev(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                  uint16_t* prev);

int lp8758_vout_set(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                     unsigned ch, unsigned vout);

int lp8758_vout_ctrl(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                     unsigned ch, bool en, bool forcepwm);

int lp8758_check_pg(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                        unsigned chmsk, bool* pg);

#endif
