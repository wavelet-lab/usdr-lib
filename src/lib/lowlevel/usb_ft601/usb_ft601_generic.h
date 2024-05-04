// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef USB_FT601_H
#define USB_FT601_H

#include <stdint.h>
#include <string.h>

#include "../usdr_lowlevel.h"
#include <usdr_logging.h>
#include "../ipblks/lms64c_proto.h"
#include "../ipblks/lms64c_cmds.h"

#define LMS7002M_SPI_INDEX 0x10
#define ADF4002_SPI_INDEX  0x30

static inline
void fill_ft601_cmd(uint8_t wbuffer[20],
                    uint32_t ft601_counter,
                    uint8_t ep,
                    uint8_t cmd,
                    uint32_t param)
{
    memset(wbuffer, 0, 20);

    wbuffer[0] = (ft601_counter) & 0xFF;
    wbuffer[1] = (ft601_counter >> 8) & 0xFF;
    wbuffer[2] = (ft601_counter >> 16) & 0xFF;
    wbuffer[3] = (ft601_counter >> 24) & 0xFF;
    wbuffer[4] = ep;
    wbuffer[5] = cmd;
    wbuffer[8] = (param) & 0xFF;
    wbuffer[9] = (param >> 8) & 0xFF;
    wbuffer[10] = (param >> 16) & 0xFF;
    wbuffer[11] = (param >> 24) & 0xFF;
}

int _ft601_cmd(lldev_t lld, uint8_t ep, uint8_t cmd, uint32_t param);

static inline
    int ft601_flush_pipe(lldev_t lld, unsigned char ep)
{
    return _ft601_cmd(lld, ep, 0x03, 0);
}

static inline
    int ft601_set_stream_pipe(lldev_t lld, unsigned char ep, size_t size)
{
    return _ft601_cmd(lld, ep, 0x02, size);
}

const struct lowlevel_plugin *usbft601_uram_register();

#endif
