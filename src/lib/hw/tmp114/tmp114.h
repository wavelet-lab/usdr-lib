// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef TMP114_H
#define TMP114_H

#include <usdr_lowlevel.h>

#define TMP114_DEVICE_ID 0x1114

int tmp114_temp_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                    int* outtemp);

int tmp114_devid_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                    int* devid);

#endif
