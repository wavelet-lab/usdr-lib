// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DEVICE_IMPL_H
#define DEVICE_IMPL_H

#include "device_vfs.h"

enum {
    MAX_OBJECTS_DEF = 128,
};

struct device_impl_base {
    unsigned objcount; // Number filled objects
    unsigned objmax;   // Storage
    usdr_vfs_obj_base_t* objlist[0];
};

#endif
