// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef AFE79XX_H
#define AFE79XX_H

#include <usdr_lowlevel.h>
#include "libcapi79xx_api.h"

struct afe79xx_state {
    lldev_t dev;
    unsigned subdev;
    unsigned addr;

    // NDA part implementation
    void* dl_handle;
    libcapi79xx_t capi;
    libcapi79xx_create_fn_t libcapi79xx_create;
    libcapi79xx_destroy_fn_t libcapi79xx_destroy;
    libcapi79xx_init_fn_t libcapi79xx_init;
};
typedef struct afe79xx_state afe79xx_state_t;


int afe79xx_create(lldev_t dev, unsigned subdev, unsigned lsaddr, afe79xx_state_t* out);
int afe79xx_init(afe79xx_state_t* afe);


#endif
