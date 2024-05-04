// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DEVICE_FE_H
#define DEVICE_FE_H

#include "device.h"

struct dev_fe;

int device_fe_probe(device_t* base, const char* compat, const char* hint,
                    struct dev_fe** obj);

int device_fe_destroy(struct dev_fe* obj);


#endif


