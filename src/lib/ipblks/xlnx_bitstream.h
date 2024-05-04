// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef XLNX_BITSTREAM_H
#define XLNX_BITSTREAM_H

#include <usdr_port.h>
#include <stdbool.h>


enum xlnx_devids {
    XLNX_DEVID_XC7A35T = 0x0362D093,
    XLNX_DEVID_XC7A50T = 0x0362C093,
    XLNX_DEVID_XC7A75T = 0x03632093,
    XLNX_DEVID_XC7A200T = 0x03636093,

    XLNX_DEVID_XC7K70T = 0x03647093,
    XLNX_DEVID_XC7K160T = 0x0364c093,
};


struct xlnx_image_params {
    uint32_t devid;
    uint32_t wbstar;
    uint32_t usr_access2;
    bool iprog;
};
typedef struct xlnx_image_params xlnx_image_params_t;

int xlnx_btstrm_parse_header(const uint32_t* mem, unsigned len, xlnx_image_params_t* stat);
int xlnx_btstrm_iprgcheck(
        const xlnx_image_params_t* internal_golden,
        const xlnx_image_params_t* newimg,
        unsigned wbstar,
        bool golden_image);

#endif //XLNX_BITSTREAM_H
