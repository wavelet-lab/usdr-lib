// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef WEBUSB_H
#define WEBUSB_H

#include <stdint.h>
#include "webusb_generic.h"

//// interface

int webusb_create(
        struct webusb_ops* ctx,
        uintptr_t param,
        unsigned loglevel,
        unsigned vidpid,
        pdm_dev_t* dmdev);

int webusb_process_rpc(pdm_dev_t dmdev,
        char* request,
        char* response,
        unsigned response_maxlen);

int webusb_debug_rpc(pdm_dev_t dmdev,
        char* request,
        char* response,
        unsigned response_maxlen);

int webusb_destroy(
        pdm_dev_t dmdev);

#endif
