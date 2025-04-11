// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "def_lmx1204.h"
#include "lmx1204.h"
#include "usdr_logging.h"


static int lmx1204_print_registers(uint32_t* regs, unsigned count)
{
    for (unsigned i = 0; i < count; i++)
    {
        uint8_t  rn = regs[i] >> 16;
        uint16_t rv = (uint16_t)regs[i];
        USDR_LOG("1204", USDR_LOG_DEBUG, "WRITE#%u: R%03u (0x%02x) -> 0x%04x [0x%06x]", i, rn, rn, rv, regs[i]);
    }

    return 0;
}

static int lmx1204_spi_post(lmx1204_state_t* obj, uint32_t* regs, unsigned count)
{
    int res;
    lmx1204_print_registers(regs, count);

    for (unsigned i = 0; i < count; i++) {
        res = lowlevel_spi_tr32(obj->dev, obj->subdev, obj->lsaddr, regs[i], NULL);
        if (res)
            return res;

        USDR_LOG("1204", USDR_LOG_NOTE, "[%d/%d] reg wr %08x\n", i, count, regs[i]);
    }

    return 0;
}

static int lmx1204_spi_get(lmx1204_state_t* obj, uint16_t addr, uint16_t* out)
{
    uint32_t v;
    int res = lowlevel_spi_tr32(obj->dev, obj->subdev, obj->lsaddr, MAKE_LMX1204_REG_RD((uint32_t)addr), &v);
    if (res)
        return res;

    USDR_LOG("1204", USDR_LOG_NOTE, " reg rd %04x => %08x\n", addr, v);
    *out = v;
    return 0;
}

UNUSED static int lmx1204_read_all_regs(lmx1204_state_t* st)
{
    uint8_t regs[] =
    {
        R0,
        R2,
        R3,
        R4,
        R5,
        R6,
        R7,
        R8,
        R9,
        R11,
        R12,
        R13,
        R14,
        R15,
        R16,
        R17,
        R18,
        R19,
        R20,
        R21,
        R22,
        R23,
        R24,
        R25,
        R28,
        R29,
        R33,
        R34,
        R65,
        R67,
        R72,
        R75,
        R79,
        R86,
        R90,
    };

    for(unsigned i = 0; i < SIZEOF_ARRAY(regs); ++i)
    {
        uint16_t regval;
        int res = lmx1204_spi_get(st, regs[i], &regval);
        if(res)
            return res;
        USDR_LOG("1204", USDR_LOG_DEBUG, "READ R%02u = 0x%04x", regs[i], regval);
    }

    return 0;
}

int lmx1204_get_temperature(lmx1204_state_t* st, float* value)
{
    if(!value)
        return -EINVAL;

    uint16_t r24;

    int res = lmx1204_spi_get(st, R24, &r24);
    if(res)
        return res;

    int16_t code = (r24 & RB_TEMPSENSE_MSK) >> RB_TEMPSENSE_OFF;
    *value = 0.65f * code - 351.0f;

    USDR_LOG("1204", USDR_LOG_DEBUG, "LMX1204 temperature sensor:%.2fC", *value);
    return 0;
}

int lmx1204_create(lldev_t dev, unsigned subdev, unsigned lsaddr, lmx1204_state_t* st)
{
    memset(st, 0, sizeof(*st));
    int res;

    st->dev = dev;
    st->subdev = subdev;
    st->lsaddr = lsaddr;

    uint32_t regs[] =
    {
        MAKE_LMX1204_R86(0),                //MUXOUT_EN_OVRD=0
        MAKE_LMX1204_R24(0,0,0,1),          //enable temp sensor
        MAKE_LMX1204_R23(1,1,1,0,1,0,0,0),  //enable temp sensor + MUXOUT_EN=1(push-pull) MUXOUT=1(SDO)
    };
    res = lmx1204_spi_post(st, regs, SIZEOF_ARRAY(regs));
    if(res)
        return res;

    usleep(1000);

    float tval;
    res = lmx1204_get_temperature(st, &tval);
    if(res)
        return res;

    return 0;
}

int lmx1204_destroy(lmx1204_state_t* st)
{
    USDR_LOG("1204", USDR_LOG_DEBUG, "Destroy OK");
    return 0;
}
