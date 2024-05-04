// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DM_DEBUG_H
#define DM_DEBUG_H

#include <stdint.h>
#include "dm_dev.h"


struct usdr_debug_ctx
{
    pdm_dev_t dev;
    pthread_t debug_thread;
    int fd;
    int clifd;
};

int usdr_dif_init(const char *params,
                  pdm_dev_t dev,
                  struct usdr_debug_ctx** octx);

int usdr_dif_free(struct usdr_debug_ctx* ctx);

#endif
