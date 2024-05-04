// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DM_DEV_IMPL_H
#define DM_DEV_IMPL_H

#include "dm_obj.h"
#include "dm_dev.h"

#include <usdr_port.h>
#include <usdr_logging.h>
#include <usdr_lowlevel.h>

#include "dm_debug.h"

struct dm_dev {
    lldev_t lldev;
    struct usdr_debug_ctx *debug_obj;

    usdr_dm_obj_t obj_head;
};



#endif
