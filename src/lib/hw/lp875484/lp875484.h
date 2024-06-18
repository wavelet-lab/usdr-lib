// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LP875484_H
#define LP875484_H

#include <usdr_lowlevel.h>

int lp875484_init(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr);

int lp875484_set_vout(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr, unsigned out_mv);

int lp875484_is_pg(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr, bool* pg);


#endif
