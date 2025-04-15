// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <inttypes.h>
#include <math.h>

#include "def_lmx1214.h"
#include "lmx1214.h"
#include "usdr_logging.h"
#include "../common/common.h"

#include "lmx1214_dump.h"

#define FREQ_EPS 1.0f

enum
{
    CLKIN_MIN     = 300000000ull,
    CLKIN_MAX_DIV = 12800000000ull,
    CLKIN_MAX_BUF = 18000000000ull,

    SYNC_IN_MIN = CLKIN_MIN,
    SYNC_IN_MAX = CLKIN_MAX_DIV,

    CLKOUT_MIN_DIV = 150000000ull,
    CLKOUT_MIN_BUF = CLKIN_MIN,

    CLKOUT_MAX_DIV_WO_SYNC = 8000000000ull,
    CLKOUT_MAX_DIV_SYNC    = 6400000000ull,
    CLKOUT_MAX_BUF         = CLKIN_MAX_BUF,

    AUXCLKOUT_MIN          = 1000000ull,
    AUXCLKOUT_MAX          = 800000000ull,
    AUXCLK_DIV_INP_MAX     = 3200000000ull,

    CLK_DIV_MIN = 2,
    CLK_DIV_MAX = 8,

    AUXCLK_DIV_MIN = 2,
    AUXCLK_DIV_MAX = 1023,
};

static int lmx1214_spi_post(lmx1214_state_t* obj, uint32_t* regs, unsigned count)
{
    return
        common_print_registers_a8d16(regs, count, USDR_LOG_DEBUG)
        ||
        common_spi_post(obj, regs, count);
}

static int lmx1214_spi_get(lmx1214_state_t* obj, uint16_t addr, uint16_t* out)
{
    return common_spi_get(obj, MAKE_LMX1214_REG_RD((uint32_t)addr), out);
}

UNUSED static int lmx1214_read_all_regs(lmx1214_state_t* st)
{
    uint8_t regs[] =
    {
        R0,
        R2,
        R3,
        R4,
        R5,
        R7,
        R8,
        R9,
        R11,
        R12,
        R13,
        R14,
        R15,
        R23,
        R24,
        R25,
        R75,
        R79,
        R86,
        R90,
    };

    for(unsigned i = 0; i < SIZEOF_ARRAY(regs); ++i)
    {
        uint16_t regval;
        int res = lmx1214_spi_get(st, regs[i], &regval);
        if(res)
            return res;
        USDR_LOG("1214", USDR_LOG_DEBUG, "READ R%02u = 0x%04x", regs[i], regval);
    }

    return 0;
}

UNUSED static int lmx1214_loaddump(lmx1214_state_t* st)
{
    int res = lmx1214_spi_post(st, lmx1214_rom_test, SIZEOF_ARRAY(lmx1214_rom_test));
    if(res)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "lmx1214_loaddump() err:%d", res);
    }
    else
    {
        USDR_LOG("2820", USDR_LOG_DEBUG, "lmx1214_loaddump() OK");
    }
    return res;
}

int lmx1214_get_temperature(lmx1214_state_t* st, float* value)
{
    if(!value)
        return -EINVAL;

    uint16_t r24;

    int res = lmx1214_spi_get(st, R24, &r24);
    if(res)
        return res;

    int16_t code = (r24 & RB_TS_MSK) >> RB_TS_OFF;
    *value = 0.65f * code - 351.0f;

    USDR_LOG("1214", USDR_LOG_DEBUG, "LMX1214 temperature sensor:%.2fC", *value);
    return 0;
}

static int lmx1214_reset_main_divider(lmx1214_state_t* st, bool set_flag)
{
    uint16_t r25;

    int res = lmx1214_spi_get(st, R25, &r25);
    if(res)
        return res;

    uint32_t reg = MAKE_LMX1214_REG_WR(R25, set_flag ? (r25 | CLK_DIV_RST_MSK) : (r25 & ~CLK_DIV_RST_MSK));
    return lmx1214_spi_post(st, &reg, 1);
}

int lmx1214_create(lldev_t dev, unsigned subdev, unsigned lsaddr, lmx1214_state_t* st)
{
    memset(st, 0, sizeof(*st));
    int res;

    st->dev = dev;
    st->subdev = subdev;
    st->lsaddr = lsaddr;

    uint32_t regs[] =
    {
        MAKE_LMX1214_R86(0, 0, 0),           //MUXOUT_EN_OVRD=0
        MAKE_LMX1214_R79(0, 0x5),            //magic R79->0x5 (see manual)
        MAKE_LMX1214_R24(0, 0, 0, 1),        //temp sensor
        MAKE_LMX1214_R23(1, 1, 1, 1 << 6),   //temp sensor + MUXOUT_EN=1(push-pull) MUXOUT=1(SDO)
    };

    res = lmx1214_spi_post(st, regs, SIZEOF_ARRAY(regs));
    if(res)
    {
        USDR_LOG("1214", USDR_LOG_ERROR, "Registers set lmx1214_spi_post() failed, err:%d", res);
        return res;
    }

    usleep(1000);

    float tempval;
    res = lmx1214_get_temperature(st, &tempval);
    if(res)
    {
        USDR_LOG("1214", USDR_LOG_ERROR, "lmx1214_get_temperature() failed, err:%d", res);
        return res;
    }

    USDR_LOG("1214", USDR_LOG_DEBUG, "Create OK");
    return 0;
}

int lmx1214_destroy(lmx1214_state_t* st)
{
    USDR_LOG("1214", USDR_LOG_DEBUG, "Destroy OK");
    return 0;
}

static int lmx1214_solver_prevalidate(uint64_t in, uint64_t out, bool* out_en, lmx1214_auxclkout_cfg_t* aux)
{
    if(in < CLKIN_MIN || in > CLKIN_MAX_BUF)
    {
        USDR_LOG("1214", USDR_LOG_ERROR, "CLKIN:%" PRIu64 " out of range [%" PRIu64 ";%" PRIu64 "]", in, (uint64_t)CLKIN_MIN, (uint64_t)CLKIN_MAX_BUF);
        return -EINVAL;
    }

    const bool buffer_mode = (out == in);
    if(!buffer_mode && in > CLKIN_MAX_DIV)
    {
        USDR_LOG("1214", USDR_LOG_ERROR, "CLKIN:%" PRIu64 " too high (>%" PRIu64 ") [BUFFERMODE:%u]", in, (uint64_t)CLKIN_MAX_DIV, buffer_mode);
        return -EINVAL;
    }

    const uint64_t out_min = (buffer_mode ? CLKOUT_MIN_BUF : CLKOUT_MIN_DIV);
    const uint64_t out_max = (buffer_mode ? CLKOUT_MAX_BUF : CLKOUT_MAX_DIV_SYNC);

    if(out < out_min || out > out_max)
    {
        USDR_LOG("1214", USDR_LOG_ERROR, "CLKOUT:%" PRIu64 " out of range [%" PRIu64 ";%" PRIu64 "] [BUFFERMODE:%u]", out, out_min, out_max, buffer_mode);
        return -EINVAL;
    }

    if(aux->enable && (aux->freq < AUXCLKOUT_MIN || aux->freq > AUXCLKOUT_MAX))
    {
        USDR_LOG("1214", USDR_LOG_ERROR, "AUXCLKOUT:%.4f out of range [%" PRIu64 ";%" PRIu64 "]", aux->freq, (uint64_t)AUXCLKOUT_MIN, (uint64_t)AUXCLKOUT_MAX);
        return -EINVAL;
    }

    switch(aux->fmt)
    {
    case LMX1214_FMT_LVDS: aux->fmt = AUXCLKOUT_FMT_LVDS; break;
    case LMX1214_FMT_CML : aux->fmt = AUXCLKOUT_FMT_CML; break;
    default:
    {
        if(!aux->enable)
        {
            aux->fmt = AUXCLKOUT_FMT_LVDS;
        }
        else
        {
            USDR_LOG("1214", USDR_LOG_ERROR, "AUXCLKOUT_FMT:%u is invalid", aux->fmt);
            return -EINVAL;
        }
    }
    }

    if(out_en[2] != out_en[3])
    {
        USDR_LOG("1214", USDR_LOG_ERROR, "bad configuration, OUT_EN2 != OUT_EN3");
        return -EINVAL;
    }

    return 0;
}

static const char* lmx1214_decode_mux(uint8_t mux)
{
    switch(mux)
    {
    case CLK_MUX_BUFFER : return "CLK_MUX_BUFFER";
    case CLK_MUX_DIVIDER: return "CLK_MUX_DIVIDER";
    }
    return "UNKNOWN";
}

static const char* lmx1214_decode_auxfmt(uint8_t fmt)
{
    switch(fmt)
    {
    case AUXCLKOUT_FMT_LVDS: return "LVDS";
    case AUXCLKOUT_FMT_CML : return "CML";
    }
    return "UNKNOWN";
}

int lmx1214_solver(lmx1214_state_t* st, uint64_t in, uint64_t out, bool* out_en, lmx1214_auxclkout_cfg_t* aux, bool prec_mode, bool dry_run)
{
    int res = lmx1214_solver_prevalidate(in, out, out_en, aux);
    if(res)
        return res;

    const bool buffer_mode = (out == in);

    unsigned clk_div;
    uint8_t clk_mux;

    if(buffer_mode)
    {
        clk_mux = CLK_MUX_BUFFER;
        clk_div = 1; //disabled
    }
    else
    {
        clk_mux = CLK_MUX_DIVIDER;
        clk_div = (unsigned)((double)in / out + 0.5);

        if(clk_div < CLK_DIV_MIN || clk_div > CLK_DIV_MAX)
        {
            USDR_LOG("1214", USDR_LOG_ERROR, "CLK_DIV:%u out of range", clk_div);
            return -EINVAL;
        }
        double f = (double)in / clk_div;
        if(fabs(f - out) > FREQ_EPS)
        {
            USDR_LOG("1214", USDR_LOG_ERROR, "Calculated CLKOUT:%.4f too rough", f);
            return -EINVAL;
        }
        if(prec_mode && in != out * clk_div)
        {
            USDR_LOG("1214", USDR_LOG_ERROR, "Cannot solve CLKOUT:%" PRIu64 " by int divider", out);
            return -EINVAL;
        }
    }

    USDR_LOG("1214", USDR_LOG_DEBUG, "CLKIN:%" PRIu64 " CLKOUT:%.4f CLK_DIV:%u MUX:%u [BUFFER_MODE:%u] [PREC_MODE:%u]",
             in, (double)in / clk_div, clk_div, clk_mux, buffer_mode, prec_mode);


    uint8_t auxclk_div_pre = AUXCLK_DIV_PRE_DIV4;
    uint16_t auxclk_div = 0x20;
    bool auxclk_div_byp = false;

    if(aux->enable)
    {
        uint8_t pre_div_min;

        if(in <= AUXCLK_DIV_INP_MAX)
            pre_div_min = AUXCLK_DIV_PRE_DIV1;
        else if(in <= ((uint64_t)AUXCLK_DIV_INP_MAX << 1))
            pre_div_min = AUXCLK_DIV_PRE_DIV2;
        else
            pre_div_min = AUXCLK_DIV_PRE_DIV4;

        bool found = false;
        for(auxclk_div_pre = pre_div_min; auxclk_div_pre <= AUXCLK_DIV_PRE_DIV4; ++auxclk_div_pre)
        {
            if(auxclk_div_pre == AUXCLK_DIV_PRE_DIV4 - 1)
                continue;

            double fmid = (double)in / auxclk_div_pre;
            if(prec_mode && in != (uint64_t)(fmid + 0.5) * auxclk_div_pre)
                continue;

            if(fmid == aux->freq && auxclk_div_pre == AUXCLK_DIV_PRE_DIV1)
            {
                found = true;
                auxclk_div_byp = true;
                break;
            }

            if(auxclk_div_pre == AUXCLK_DIV_PRE_DIV1) //cannot use pre_div==1 without bypassing div
                continue;

            unsigned div = (unsigned)(fmid / aux->freq + 0.5);

            if(div < AUXCLK_DIV_MIN || div > AUXCLK_DIV_MAX)
                continue;

            double f = fmid / div;
            if(fabs(f - aux->freq) > FREQ_EPS)
                continue;

            if(prec_mode && fmid != aux->freq * div)
                continue;

            found = true;
            auxclk_div = div;
            break;
        }

        if(!found)
        {
            USDR_LOG("1214", USDR_LOG_ERROR, "AUXCLKOUT:%.4f cannot be solved with LMX1214 divs", aux->freq);
            return -EINVAL;
        }
    }

    //if we got here - the solution is found
    st->clkin = in;
    st->clk_mux = clk_mux;
    st->clk_div = clk_div;
    st->clkout = (double)in / clk_div;
    for(unsigned i = 0; i < LMX1214_OUT_CNT; ++i) st->clkout_enabled[i] = out_en[i];
    st->auxclk_div_pre = auxclk_div_pre;
    st->auxclk_div_byp = auxclk_div_byp;
    st->auxclk_div = auxclk_div;
    st->auxclkout = *aux;
    if(st->auxclkout.enable)
        st->auxclkout.freq = auxclk_div_byp ? (double)in / auxclk_div_pre : (double)in / auxclk_div_pre / auxclk_div;

    USDR_LOG("1214", USDR_LOG_INFO, "LMX1214 SOLUTION FOUND:");
    USDR_LOG("1214", USDR_LOG_INFO, "CLKIN:%" PRIu64 " CLK_DIV:%u CLKMUX:%s(%u) CLKOUT:%.4f",
             st->clkin, st->clk_div, lmx1214_decode_mux(st->clk_mux), st->clk_mux, st->clkout);
    USDR_LOG("1214", USDR_LOG_INFO, "CLKOUT enabled - OUT0:%u OUT1:%u OUT2:%u OUT3:%u",
             st->clkout_enabled[0], st->clkout_enabled[1], st->clkout_enabled[2], st->clkout_enabled[3]);

    if(st->auxclkout.enable)
        USDR_LOG("1214", USDR_LOG_INFO, "AUXCLC_DIV_PRE:%u AUXCLK_DIV_BYP:%u AUXCLK_DIV:%u AUXCLKOUT:%.4f AUXCLKOUT_FMT:%s(%u)",
                 st->auxclk_div_pre, st->auxclk_div_byp, st->auxclk_div, st->auxclkout.freq,
                 lmx1214_decode_auxfmt(st->auxclkout.fmt), st->auxclkout.fmt);
    else
        USDR_LOG("1214", USDR_LOG_INFO, "AUXCLKOUT:disabled");

    //Setting registers

    res = dry_run ? 0 : lmx1214_reset_main_divider(st, true);
    if(res)
    {
        USDR_LOG("1214", USDR_LOG_ERROR, "lmx1214_reset_main_divider(1) err:%d", res);
        return res;
    }

    uint32_t regs[] =
    {
        MAKE_LMX1214_R25(0x4, 0, st->clk_div - 1, st->clk_mux),
        MAKE_LMX1214_R14(0, 0, 1, 0),

        //according do doc: program R79 and R90 before setting logiclk_div_bypass
        //desc order is broken here!
        MAKE_LMX1214_R8 (0, st->auxclk_div_pre, 0, (st->auxclkout.enable ? 1 : 0), 0, st->auxclkout.fmt),
        MAKE_LMX1214_R79(0, st->auxclk_div_byp ? 0x5 : 0x104 /*0x205*/),
        MAKE_LMX1214_R90(0, 0, (st->auxclk_div_byp ? 1 : 0), (st->auxclk_div_byp ? 1 : 0), 0),
        MAKE_LMX1214_R9 (0, 0, 0, (st->auxclk_div_byp ? 1 : 0), 0, st->auxclk_div),

        MAKE_LMX1214_R3 (st->clkout_enabled[3] ? 1 : 0,
                         st->clkout_enabled[2] ? 1 : 0,
                         st->clkout_enabled[1] ? 1 : 0,
                         st->clkout_enabled[0] ? 1 : 0,
                         0xF86//0xFE
                        ),
    };

    res = dry_run ? common_print_registers_a8d16(regs, SIZEOF_ARRAY(regs), USDR_LOG_DEBUG) : lmx1214_spi_post(st, regs, SIZEOF_ARRAY(regs));
    if(res)
    {
        USDR_LOG("1214", USDR_LOG_ERROR, "Registers set lmx1214_spi_post() failed, err:%d", res);
        return res;
    }

    usleep(10000);

    res = dry_run ? 0 : lmx1214_reset_main_divider(st, false);
    if(res)
    {
        USDR_LOG("1214", USDR_LOG_ERROR, "lmx1214_reset_main_divider(0) err:%d", res);
        return res;
    }

    return 0;
}

int lmx1214_sysref_windowing_beforesync(lmx1214_state_t* st)
{
    int res;

    uint8_t delay_step_size = SYNC_DLY_STEP_28_PS_1_4GHZ_TO_2_7GHZ;
    if(st->clkin > 2400000000 && st->clkin <= 4700000000)
        delay_step_size = SYNC_DLY_STEP_15_PS__2_4GHZ_TO_4_7GHZ;
    if(st->clkin > 3100000000 && st->clkin <= 5700000000)
        delay_step_size = SYNC_DLY_STEP_11_PS_3_1GHZ_TO_5_7GHZ;
    if(st->clkin > 4500000000 && st->clkin <= 12800000000)
        delay_step_size = SYNC_DLY_STEP_8_PS_4_5GHZ_TO_12_8GHZ;

    USDR_LOG("1214", USDR_LOG_DEBUG, "DELAY_STEPSIZE:%u", delay_step_size);

    {
        uint32_t regs[] =
            {
                MAKE_LMX1214_R9 (0, 1/*SYNC_EN*/, 0, (st->auxclk_div_byp ? 1 : 0), 0, st->auxclk_div),
                MAKE_LMX1214_R14(0, 1/*CLKPOS_CAPTURE_EN*/, 1, 0),
                MAKE_LMX1214_R13(0, delay_step_size),
            };

        res = lmx1214_spi_post(st, regs, SIZEOF_ARRAY(regs));
        if(res)
            return res;
    }

    {
        uint16_t r15;
        res = lmx1214_spi_get(st, R15, &r15);
        if(res)
            return res;

        uint32_t regval_set = MAKE_LMX1214_REG_WR(R15, r15 |  SYNC_CLR_MSK);
        uint32_t regval_rst = MAKE_LMX1214_REG_WR(R15, r15 & ~SYNC_CLR_MSK);

        res = lmx1214_spi_post(st, &regval_set, 1);
        res = res ? res : lmx1214_spi_post(st, &regval_rst, 1);
    }

    return res;
}

int lmx1214_sysref_windowing_aftersync(lmx1214_state_t* st)
{
    uint32_t regs[] =
        {
            MAKE_LMX1214_R9 (0, 0/*SYNC_EN*/, 0, (st->auxclk_div_byp ? 1 : 0), 0, st->auxclk_div),
            MAKE_LMX1214_R14(0, 0/*CLKPOS_CAPTURE_EN*/, 1, 0),
        };
    return lmx1214_spi_post(st, regs, SIZEOF_ARRAY(regs));
}

int lmx1214_sysref_windowing_capture(lmx1214_state_t* st)
{
    int res;
    uint16_t r11, r12;

    res = lmx1214_spi_get(st, R11, &r11);
    res = res ? res : lmx1214_spi_get(st, R12, &r12);
    if(res)
        return res;

    uint32_t clkpos = ((uint32_t)r12 << 16) | r11;

    unsigned delay;
    res = common_ti_calc_sync_delay(clkpos, &delay);
    if(res)
        return res;

    {
        uint32_t reg = MAKE_LMX1214_R15(0, 0x16, delay, 0);
        res = lmx1214_spi_post(st, &reg, 1);
        if(res)
            return res;
    }

    return 0;
}
