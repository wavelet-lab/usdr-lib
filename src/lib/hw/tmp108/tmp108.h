// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef TMP108_H
#define TMP108_H

#include <usdr_lowlevel.h>

int tmp108_temp_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                    int* outtemp);

#endif
