// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef SI5332_H
#define SI5332_H

#include <usdr_lowlevel.h>

int si5332_init(lldev_t dev, subdev_t subdev, lsopaddr_t addr, unsigned div, bool ext_in2, bool rv);

struct si5332_layout_info {
    unsigned infreq;
    unsigned out;
};

int si5332_set_layout(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                      struct si5332_layout_info* nfo, bool old, unsigned int lodiv, unsigned* vcofreq);

#if 0
int si5332_set_idfreq(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                      unsigned idfreq);
#endif


int si5332_set_port3_en(lldev_t dev, subdev_t subdev, lsopaddr_t lsopaddr, bool loen, bool txen);

#endif
