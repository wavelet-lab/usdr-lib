// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef EXT_SUPERSYNC_H
#define EXT_SUPERSYNC_H

#include <usdr_port.h>
#include <usdr_lowlevel.h>
#include <stdbool.h>
#include "../device.h"
#include "../device_vfs.h"

#include "../hw/lmk5c33216/lmk5c33216.h"


struct board_ext_supersync {
    l5c33216_state_t lmk;
};

typedef struct board_ext_supersync board_ext_supersync_t;

int board_ext_supersync_init(lldev_t dev,
                             unsigned subdev,
                             unsigned gpio_base,
                             const char *compat,
                             unsigned i2cloc,
                             board_ext_supersync_t* ob);

#endif
