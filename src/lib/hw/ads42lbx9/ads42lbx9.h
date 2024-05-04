// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef ADS42LBX9_H
#define ADS42LBX9_H

#include <usdr_lowlevel.h>

int ads42lbx9_ctrl(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                   bool chaen, bool chben, bool pd, bool reset);

int ads42lbx9_set_normal(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr, bool ena, bool enb);
int ads42lbx9_set_pbrs(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr);
int ads42lbx9_set_tp(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                     uint16_t p1, uint16_t p2);

int ads42lbx9_dump(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr);

int ads42lbx9_reset_and_check(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr);

#endif
