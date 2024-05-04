// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "tmp114.h"

enum tmp114_regs {
    TMP114_Temp_Result = 0x00,   // R    0000h Temperature result register
    TMP114_Slew_Result = 0x01,   // R    0000h Slew rate result register
    TMP114_Alert_Status = 0x02,  // R/RC 0000h Alert status register
    TMP114_Configuration = 0x03, // R/W  0004h Configuration register
    TMP114_TLow_Limit = 0x04,    // R/W  F380h Temperature low limit register
    TMP114_THigh_Limit = 0x05,   // R/W  2A80h Temperature high limit register
    TMP114_Hysteresis = 0x06,    // R/W  0A0Ah Hysteresis register
    TMP114_Slew_Limit = 0x07,    // R/W  0500h Temperature slew rate limit register
    TMP114_Unique_ID1 = 0x08,    // R    xxxxh Unique_ID1 register
    TMP114_Unique_ID2 = 0x09,    // R    xxxxh Unique_ID2 register
    TMP114_Unique_ID3 = 0x0A,    // R    xxxxh Unique_ID3 register
    TMP114_Device_ID = 0x0B,     // R    1114h Device ID register
};

static
int tmp114_reg_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                   uint8_t taddr, uint16_t *pv)
{
    return lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        USDR_LSOP_I2C_DEV, ls_op_addr,
                                        2, pv, 1, &taddr);
}

int tmp114_temp_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                    int* outtemp)
{
    int16_t temp = -2;
    int res = tmp114_reg_get(dev, subdev, ls_op_addr, TMP114_Temp_Result, (uint16_t *)&temp);
    *outtemp = temp << 1;
    return res;
}

int tmp114_devid_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                    int* devid)
{
    uint16_t ldevid = 0;
    int res = tmp114_reg_get(dev, subdev, ls_op_addr, TMP114_Device_ID, (uint16_t *)&ldevid);
    *devid = ldevid;
    return res;
}

