// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdint.h>

#include "lmk5c33216.h"
#include "def_lmk5c33216.h"
#include "lmk5c33216_rom.h"

#include <usdr_logging.h>


// enum {
//     I2C_ADDR_LMK = 0x64,
// };

// enum {
//     I2C_EXTERNAL_CMD_OFF = 16,
// };

int lmk_5c33216_reg_wr(l5c33216_state_t *d, uint16_t reg, uint8_t out)
{
    uint8_t data[3] = { reg >> 8, reg, out };
    return d->i2c_func(d->dev, d->subdev,
             USDR_LSOP_I2C_DEV, d->lsaddr,
             0, NULL, 3, data);
}

int lmk_5c33216_reg_rd(l5c33216_state_t *d, uint16_t reg, uint8_t* val)
{
    uint8_t addr[2] = { reg >> 8, reg };
    return d->i2c_func(d->dev, d->subdev,
             USDR_LSOP_I2C_DEV, d->lsaddr,
             1, val, 2, &addr[0]);
}

static int lmk_5c33216_reg_u32(l5c33216_state_t *d, uint16_t reg, uint8_t* val)
{
    uint8_t addr[2] = { reg >> 8, reg };
    return d->i2c_func(d->dev, d->subdev,
             USDR_LSOP_I2C_DEV, d->lsaddr,
             4, val, 2, &addr[0]);
}

static int lmk5c33216_reg_wr_n(l5c33216_state_t* d, const uint32_t* regs, unsigned count)
{
    int res;
    for (unsigned j = 0; j < count; j++) {
        uint16_t addr = regs[j] >> 8;
        uint8_t data = regs[j];

        res = lmk_5c33216_reg_wr(d, addr, data);
        if (res)
            return res;
    }

    return 0;
}

int lmk5c33216_create(lldev_t dev, unsigned subdev, unsigned lsaddr, ext_i2c_func_t func, l5c33216_state_t* out)
{
    int res;
    uint8_t dummy[4];

    out->dev = dev;
    out->subdev = subdev;
    out->lsaddr = lsaddr;
    out->i2c_func = func;

    res = lmk_5c33216_reg_u32(out, 0, &dummy[0]);
    if (res)
        return res;

    USDR_LOG("5C33", USDR_LOG_INFO, "LMK5C33216 DEVID[0/1/2/3] = %02x %02x %02x %02x\n", dummy[3], dummy[2], dummy[1], dummy[0]);
    if ( dummy[3] != 0x10 || dummy[2] != 0x0b || dummy[1] != 0x40 || dummy[0] != 0x05 ) {
        return -ENODEV;
    }

    // TODO: Soft reset

    res = lmk5c33216_reg_wr_n(out, lmk5c33216_rom, SIZEOF_ARRAY(lmk5c33216_rom));
    if (res)
        return res;

    USDR_LOG("5C33", USDR_LOG_ERROR, "LMK5C33216 initialized\n");
    return 0;
}
