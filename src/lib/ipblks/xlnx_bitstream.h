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

static inline
uint64_t get_xilinx_rev_h(unsigned rev)
{
    uint64_t day = (rev >> 27) & 0x1f;
    uint64_t month = (rev >> 23) & 0x0f;
    uint64_t year = (rev >> 17) & 0x3f;
    uint64_t hour = (rev >> 12) & 0x1f;
    uint64_t min = (rev >> 6) & 0x3f;
    uint64_t sec = (rev >> 0) & 0x3f;
    uint64_t h = 0;

    h = sec + (min + (hour + (day + (month + (2000 + year) * 100) * 100) * 100) * 100) * 100;
    return h;
}

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
