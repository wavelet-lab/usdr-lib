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

    libcapi79xx_upd_nco_fn_t libcapi79xx_upd_nco;
    libcapi79xx_get_nco_fn_t libcapi79xx_get_nco;

    libcapi79xx_set_dsa_fn_t libcapi79xx_set_dsa;

    libcapi79xx_check_health_fn_t libcapi79xx_check_health;

};
typedef struct afe79xx_state afe79xx_state_t;


int afe79xx_create(lldev_t dev, unsigned subdev, unsigned lsaddr, unsigned chipType, afe79xx_state_t* out);
int afe79xx_init(afe79xx_state_t* afe, const char *configuration);


int afe79xx_create_dummy(afe79xx_state_t* afe);

#endif
