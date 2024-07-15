// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "dac80501.h"
#include "def_dac80501.h"

#include <usdr_logging.h>

static
int _dac_x0501_set_reg(lldev_t dev, subdev_t subdev, lsopaddr_t addr, uint8_t reg, uint16_t val)
{
    uint8_t data[3] = { reg, (val >> 8), (val) };
    return lowlevel_ls_op(dev, subdev,
                          USDR_LSOP_I2C_DEV, addr,
                          0, NULL, 3, data);
}

static
    int _dac_x0501_get_reg(lldev_t dev, subdev_t subdev, lsopaddr_t addr, uint8_t reg, uint16_t* oval)
{
    return lowlevel_ls_op(dev, subdev,
                          USDR_LSOP_I2C_DEV, addr,
                          2, oval, 1, &reg);
}

int dac80501_init(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                  dac80501_cfg_t config)
{
    int res;
    uint16_t devid;

    res = _dac_x0501_get_reg(dev, subdev, ls_op_addr, DEVID, &devid);
    if (res)
        return res;

    switch (devid) {
    // Preproduction 16 bit DAC
    case 0x0194: //PDAC80501M
    // Production 12, 14, 16 bit DAC
    case 0x0195: //DAC80501M
    case 0x1195: //DAC70501M
    case 0x2195: //DAC60501M
    case 0x0115: //DAC80501Z
    case 0x1115: //DAC70501Z
    case 0x2115: //DAC60501Z
        break;

    case 0x0000:
    case 0xbeef:
        return -ENODEV;

    default:
        USDR_LOG("D501", USDR_LOG_WARNING, "DACx0501 unknown DAC ID=%x\n", devid);
        return -EIO;
    }

    if (config != DAC80501_CFG_REF_DIV_GAIN_MUL)
        return -EINVAL;

    res = _dac_x0501_set_reg(dev, subdev, ls_op_addr, GAIN, (uint16_t)MAKE_DAC80501_GAIN(1, 1));
    if (res)
        return res;

    return 0;
}


int dac80501_dac_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                     uint16_t code)
{
    return _dac_x0501_set_reg(dev, subdev, ls_op_addr, DAC, code);
}

int dac80501_dac_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                     uint16_t* pcode)
{
    return _dac_x0501_get_reg(dev, subdev, ls_op_addr, DAC, pcode);
}
