// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef _WEBUSB_GENERIC_H
#define _WEBUSB_GENERIC_H

#include <errno.h>

static inline
    int webusb_generic_plugin_discovery(unsigned pcount, const char** filterparams,
                                 const char** filtervals,
                                 unsigned maxbuf, char* outarray)
{
    return -ENOTSUP;
}

#endif  //_WEBUSB_GENERIC_H
