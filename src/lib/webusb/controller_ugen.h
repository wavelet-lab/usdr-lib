// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CONTROLLER_UGEN_H
#define CONTROLLER_UGEN_H

#include "controller.h"
#include <usdr_lowlevel.h>

#if 0
int ctrl_sdr_rxstreaming_startstop(lldev_t lldev, bool start);

int ctrl_sdr_rxstreaming_prepare(
        lldev_t lldev,
        unsigned chmask,
        unsigned pktsyms,
        const char* wirefmt,
        unsigned* block_size,
        unsigned* burstcnt,
        unsigned flags);
#endif

int general_call(pdm_dev_t dmdev, const struct sdr_call *pcall,
                 unsigned outbufsz, char *outbuffer, const char *inbuffer,
                 fn_callback_t f, void* obj);

#if 0
int webusb_create_ugen(struct webusb_ops* ctx,
                       uintptr_t param,
                       struct webusb_device** dev);
#endif
#endif

