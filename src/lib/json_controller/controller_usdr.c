// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <string.h>

#include "controller_ugen.h"
#include "controller_usdr.h"

#include "../device/m2_lm6_1/usdr_ctrl.h"

// TODO: get rid of this foo
usdr_dev_t* get_usdr_dev(pdevice_t udev);

int _usdr_callback(void* obj, int type, unsigned parameter)
{
    struct usdr_dev* dev = (struct usdr_dev*)obj;
    if (type == SDR_PC_SET_SAMPLERATE)
        return usdr_set_samplerate_ex(dev, parameter, parameter, 0, 0, 0);

    return -EINVAL;
}

int usdr_call(pdm_dev_t dmdev, const struct sdr_call *pcall,
              unsigned outbufsz, char* outbuffer, const char *inbuffer)
{
    return general_call(dmdev, pcall, outbufsz, outbuffer, inbuffer, _usdr_callback, get_usdr_dev(dmdev->lldev->pdev));
}
