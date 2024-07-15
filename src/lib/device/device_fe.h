// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DEVICE_FE_H
#define DEVICE_FE_H

#include "device.h"

struct dev_fe;

int device_fe_probe(device_t* base, const char* compat, const char* hint, unsigned def_i2c_loc,
                    struct dev_fe** obj);
int device_fe_destroy(struct dev_fe* obj);

void* device_fe_to(struct dev_fe* obj, const char* type);

#endif


