// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef MOCK_LOWLEVEL_H
#define MOCK_LOWLEVEL_H

#include <usdr_lowlevel.h>

struct mock_functions {
    int (*mock_spi_tr32)(unsigned busno, uint32_t dout, uint32_t* din);
};

// Create dummy device for unit tests
lldev_t mock_lowlevel_create(const struct mock_functions *mf);

struct mock_lowlevel_dev {
    struct lowlevel_dev base;
    const struct mock_functions *mock_func;
};

#endif
