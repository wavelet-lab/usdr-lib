// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "common.h"
#include "usdr_logging.h"
#include "../cal/opt_func.h"

int common_ti_calc_sync_delay(uint32_t clkpos, unsigned* calced_delay)
{
    const unsigned WBSIZE = sizeof(uint32_t) * 8;

    char tmps[WBSIZE + 1];

    binary_print_u32_reverse(clkpos, tmps);
    USDR_LOG("COMN", USDR_LOG_DEBUG, "WINDOW DATA:0b%s", tmps);

    //MSB & LSB should be 1
    if((clkpos & 0x80000001) != 0x80000001)
    {
        USDR_LOG("COMN", USDR_LOG_ERROR, "Window data is inconstent, check capture conditions");
        return -EINVAL;
    }

    unsigned i = 0;
    uint8_t idx_found[WBSIZE];

    while(i < WBSIZE)
    {
        uint32_t v = clkpos >> i;

        if((v & 0b1111) == 0b1101 || (v & 0b1111) == 0b1011)
        {
            idx_found[i] = 1;
            i += 4;
        }
        else if((v & 0b111) == 0b111)
        {
            idx_found[i] = 1;
            i += 3;
        }
        else if((v & 0b11) == 0b11)
        {
            idx_found[i] = 1;
            i += 2;
        }

        idx_found[i++] = 0;
    }

    uint8_t first_raise = 0xff, second_raise = 0xff;
    for(unsigned i = 0; i < WBSIZE; ++i)
    {
        if(idx_found[i])
        {
            if(first_raise == 0xff)
                first_raise = i;
            else
            {
                second_raise = i;
                break;
            }
        }
    }

    if(first_raise == 0xff || second_raise == 0xff)
    {
        USDR_LOG("COMN", USDR_LOG_ERROR, "Clock raise patterns not found, cannot determine delay");
        return -EINVAL;
    }

    unsigned delay = (second_raise + first_raise) >> 1;
    if(!delay || delay > 0x3f)
    {
        USDR_LOG("COMN", USDR_LOG_ERROR, "Invalid calculated delay:%u, out of range (0; 0x3f)", delay);
        return -EINVAL;
    }
    USDR_LOG("COMN", USDR_LOG_DEBUG, "SYSREF vs CLOCK delay:%u", delay);

    *calced_delay = delay;
    return 0;
}

int common_print_registers_a8d16(uint32_t* regs, unsigned count, int loglevel)
{
    for (unsigned i = 0; i < count; i++)
    {
        uint8_t  ra = regs[i] >> 16;
        uint16_t rv = (uint16_t)regs[i];
        USDR_LOG("COMN", loglevel, "WRITE#%u: R%03u (0x%02x) -> 0x%04x [0x%06x]", i, ra, ra, rv, regs[i]);
    }

    return 0;
}

int common_spi_post(void* o, uint32_t* regs, unsigned count)
{
    int res;
    const common_hw_state_struct_t* obj = (common_hw_state_struct_t*)o;

    for (unsigned i = 0; i < count; i++) {
        res = lowlevel_spi_tr32(obj->dev, obj->subdev, obj->lsaddr, regs[i], NULL);
        if (res)
            return res;

        USDR_LOG("COMN", USDR_LOG_NOTE, "[%d/%d] reg wr %08x\n", i, count, regs[i]);
    }

    return 0;
}

int common_spi_get(void* o, uint16_t addr, uint16_t* out)
{
    uint32_t v;
    const common_hw_state_struct_t* obj = (common_hw_state_struct_t*)o;

    int res = lowlevel_spi_tr32(obj->dev, obj->subdev, obj->lsaddr, addr, &v);
    if (res)
        return res;

    USDR_LOG("COMN", USDR_LOG_NOTE, " reg rd %04x => %08x\n", addr, v);
    *out = v;
    return 0;
}
