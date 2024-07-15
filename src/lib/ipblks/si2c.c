// Copyright (c) 2023-2024 Wavelet Lab
//
// This work is dual-licensed under MIT and GPL 2.0.
// You can choose between one of them if you use this work.
//
// SPDX-License-Identifier: MIT OR GPL-2.0

#include "si2c.h"

int si2c_make_ctrl_reg(unsigned char idx,
        const unsigned char *dd,
        unsigned memoutsz,
        unsigned meminsz,
        unsigned* ctrl_w)
{
    unsigned data = 0;

    if (memoutsz > 3)
        return -EINVAL;
    if (meminsz > 4)
        return -EINVAL;

    if (memoutsz > 0)
        data = dd[0];
    if (memoutsz > 1)
        data |= ((unsigned)dd[1]) << 8;
    if (memoutsz > 2)
        data |= ((unsigned)dd[2]) << 16;

    *ctrl_w = MAKE_I2C_CMD(meminsz > 0 ? 1 : 0, meminsz - 1, memoutsz, idx, data);
    return 0;
}

unsigned si2c_get_lut(const struct i2c_cache* cd)
{
    unsigned i, cw;
    unsigned ilut = 0x80000000;

    for (i = 0; i < 4; i++) {
        cw = cd[i].addr | (cd[i].busn ? 0x80 : 0);
        ilut |= cw << (8 * i);
    }
    return ilut;
}

unsigned si2c_update_lut_idx(struct i2c_cache* cd, unsigned addr, unsigned busno)
{
    unsigned max = (busno == 0) ? 3 : 4;
    unsigned smin = ~0, sidx = 0, i, j;

    for (i = 0; i < max; i++) {
        if (cd[i].addr == addr && cd[i].busn == busno) {

            for (j = 0; j < max; j++) {
                if (cd[j].lrui == cd[i].lrui + 1) {
                    cd[j].lrui = cd[i].lrui;
                    cd[i].lrui++;
                    //return i;
                    break;
                }
            }
            return i;
        }

        if (smin > cd[i].lrui) {
            smin = cd[i].lrui;
            sidx = i;
        }
    }

    if (smin == 0) {
        for (j = 0; j < max; j++) {
            if (cd[j].lrui > 0)
                cd[j].lrui++;
        }
        cd[sidx].lrui++;
    }

    cd[sidx].busn = busno;
    cd[sidx].addr = addr;
    return sidx;
}

