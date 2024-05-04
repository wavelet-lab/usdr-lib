// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef TPS6381X_H
#define TPS6381X_H

#include <usdr_lowlevel.h>

int tps6381x_init(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                  bool enable, bool force_pwm, int vout_mv);


#endif
