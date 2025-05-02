// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <inttypes.h>
#include <math.h>

#include "def_lmx1204.h"
#include "lmx1204.h"
#include "usdr_logging.h"
#include "../cal/opt_func.h"

#include "../common/common.h"
#include "lmx1204_dump.h"

#define FREQ_EPS 1.0f

enum
{
    SYSREFOUT_MIN = 0,
    SYSREFOUT_MAX_GENMODE = 200000000ull,
    SYSREFOUT_MAX_RPTMODE = 100000000ull,

    CLKIN_MIN =   300000000ull,
    CLKIN_MAX = 12800000000ull,

    CLKOUT_MIN_DIV =  150000000ull,
    CLKOUT_MAX_DIV = 6400000000ull,

    CLKOUT_MIN_BUF = CLKIN_MIN,
    CLKOUT_MAX_BUF = CLKIN_MAX,

    CLKOUT_MIN_MUL = 3200000000ull,
    CLKOUT_MAX_MUL = 6400000000ull,

    LOGICLKOUT_MIN =   1000000ull,
    LOGICLKOUT_MAX = 800000000ull,

    SMCLK_DIV_PRE_OUT_MAX = 1600000000ull,
    SMCLK_DIV_OUT_MAX =       30000000ull,

    LOGICLK_DIV_PRE_OUT_MAX = 3200000000ull,

    SYSREF_DIV_PRE_OUT_MAX = 3200000000ull,

    CLK_DIV_MIN = 2,
    CLK_DIV_MAX = 8,
    CLK_MULT_MIN = 1,
    CLK_MULT_MAX = 4,
    LOGICLK_DIV_MIN = 2,
    LOGICLK_DIV_MAX = 1023,
    SYSREF_DIV_MIN = 2,
    SYSREF_DIV_MAX = 4095,
};

static int lmx1204_spi_post(lmx1204_state_t* obj, uint32_t* regs, unsigned count)
{
    return
        common_print_registers_a8d16(regs, count, USDR_LOG_DEBUG)
        ||
        common_spi_post(obj, regs, count);
}

static int lmx1204_spi_get(lmx1204_state_t* obj, uint16_t addr, uint16_t* out)
{
    return common_spi_get(obj, MAKE_LMX1204_REG_RD((uint32_t)addr), out);
}

int lmx1204_loaddump(lmx1204_state_t* st)
{
    int res = lmx1204_spi_post(st, lmx1204_rom_test, SIZEOF_ARRAY(lmx1204_rom_test));
    if(res)
    {
        USDR_LOG("1204", USDR_LOG_ERROR, "lmx1204_loaddump() err:%d", res);
    }
    else
    {
        USDR_LOG("1204", USDR_LOG_DEBUG, "lmx1204_loaddump() OK");
    }
    return res;
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

static int lmx1204_reset_main_divider(lmx1204_state_t* st, bool set_flag)
{
    uint16_t r25;

    int res = lmx1204_spi_get(st, R25, &r25);
    if(res)
        return res;

    uint32_t reg = MAKE_LMX1204_REG_WR(R25, set_flag ? (r25 | CLK_DIV_RST_MSK) : (r25 & ~CLK_DIV_RST_MSK));
    return lmx1204_spi_post(st, &reg, 1);
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

static inline const char* lmx1204_decode_lock_status(enum rb_ld_options ld)
{
    switch(ld)
    {
    case RB_LD_UNLOCKED_VTUNE_LOW:  return "UNLOCKED (VTUNE low)";
    case RB_LD_UNLOCKED_VTUNE_HIGH: return "UNLOCKED (VTUNE high)";
    case RB_LD_LOCKED:              return "LOCKED";
    }
    return "UNKNOWN";
}

static inline const char* lmx1204_decode_vco_core(enum rb_vco_sel_options c)
{
    switch(c)
    {
    case RB_VCO_SEL_VCO5: return "VCO5";
    case RB_VCO_SEL_VCO4: return "VCO4";
    case RB_VCO_SEL_VCO3: return "VCO3";
    case RB_VCO_SEL_VCO2: return "VCO2";
    case RB_VCO_SEL_VCO1: return "VCO1";
    }
    return "UNKNOWN";
}

int lmx1204_read_status(lmx1204_state_t* st, lmx1204_stats_t* status)
{
    if(!status)
        return -EINVAL;

    uint16_t r75, r65;

    int res = lmx1204_get_temperature(st, &status->temperature);
    res = res ? res : lmx1204_spi_get(st, R75, &r75);
    res = res ? res : lmx1204_spi_get(st, R65, &r65);
    if(res)
        return res;

    status->vco_sel = (r65 & RB_VCO_SEL_MSK) >> RB_VCO_SEL_OFF;
    status->lock_detect_status = (r75 & RB_LD_MSK) >> RB_LD_OFF;

    USDR_LOG("1204", USDR_LOG_DEBUG, "STATUS> Temp:%.2fC LOCK:%u(%s) VCO_SEL:%u(%s)",
             status->temperature,
             status->lock_detect_status, lmx1204_decode_lock_status(status->lock_detect_status),
             status->vco_sel, lmx1204_decode_vco_core(status->vco_sel));

    return 0;
}

int lmx1204_create(lldev_t dev, unsigned subdev, unsigned lsaddr, lmx1204_state_t* st)
{
    memset(st, 0, sizeof(*st));
    int res;

    st->dev = dev;
    st->subdev = subdev;
    st->lsaddr = lsaddr;

    //SYSREF individual channel delays defaults (according to TICS Pro)
    //Could be overriden before solver call (lmx1204_set_sysrefout_ch_delay())
    for(unsigned i = 0; i < SIZEOF_ARRAY(st->sysref_indiv_ch_delay); ++i)
    {
        lmx1204_sysrefout_channel_delay_t* d = &st->sysref_indiv_ch_delay[i];
        d->phase = SYSREFOUT0_DELAY_PHASE_QCLK0;
        d->i = 7;
        d->q = 0x7f - d->i;
    }

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

    lmx1204_stats_t lmx1204status;
    lmx1204_read_status(st, &lmx1204status); //just for log

    return 0;
}

int lmx1204_destroy(lmx1204_state_t* st)
{
    USDR_LOG("1204", USDR_LOG_DEBUG, "Destroy OK");
    return 0;
}

static int lmx1204_solver_prevalidate(lmx1204_state_t* st)
{
    if(st->clkin < CLKIN_MIN || st->clkin > CLKIN_MAX)
    {
        USDR_LOG("1204", USDR_LOG_ERROR, "CLKIN:%" PRIu64 " out of range [%" PRIu64 "; %" PRIu64 "]",
                 st->clkin, (uint64_t)CLKIN_MIN, (uint64_t)CLKIN_MAX);
        return -EINVAL;
    }

    if(st->ch_en[LMX1204_CH_LOGIC] && st->clkout_en[LMX1204_CH_LOGIC] && (st->logiclkout < LOGICLKOUT_MIN || st->logiclkout > LOGICLKOUT_MAX))
    {
        USDR_LOG("1204", USDR_LOG_ERROR, "LOGICLKOUT:%.4f out of range [%" PRIu64 "; %" PRIu64 "]",
                 st->logiclkout, (uint64_t)LOGICLKOUT_MIN, (uint64_t)LOGICLKOUT_MAX);
        return -EINVAL;
    }

    double fmin, fmax;

    switch(st->clk_mux)
    {
    case CLK_MUX_DIVIDER_MODE:    fmin = CLKOUT_MIN_DIV; fmax = CLKOUT_MAX_DIV; break;
    case CLK_MUX_MULTIPLIER_MODE: fmin = CLKOUT_MIN_MUL; fmax = CLKOUT_MAX_MUL; break;
    case CLK_MUX_BUFFER_MODE:     fmin = CLKOUT_MIN_BUF; fmax = CLKOUT_MAX_BUF; break;
    default:
        USDR_LOG("1204", USDR_LOG_ERROR, "CLK_MUX:%u unknown", st->clk_mux);
        return -EINVAL;
    }

    if(st->clkout < fmin || st->clkout > fmax)
    {
        USDR_LOG("1204", USDR_LOG_ERROR, "CLKOUT:%.4f out of range [%.0f; %.0f]", st->clkout, fmin, fmax);
        return -EINVAL;
    }

    if(st->sysref_en)
    {
        fmin = SYSREFOUT_MIN;
        switch(st->sysref_mode)
        {
        case LMX1204_CONTINUOUS: st->sysref_mode = SYSREF_MODE_CONTINUOUS_GENERATOR_MODE; fmax = SYSREFOUT_MAX_GENMODE; break;
        case LMX1204_REPEATER:   st->sysref_mode = SYSREF_MODE_REPEATER_REPEATER_MODE;    fmax = SYSREFOUT_MAX_RPTMODE; break;
        case LMX1204_PULSER:     st->sysref_mode = SYSREF_MODE_PULSER_GENERATOR_MODE;     fmax = 0.0f; break;
        default:
            USDR_LOG("1204", USDR_LOG_ERROR, "SYSREF_MODE:%u unknown", st->sysref_mode);
            return -EINVAL;
        }

        if(fmax != 0.0f && (st->sysrefout < fmin || st->sysrefout > fmax))
        {
            USDR_LOG("1204", USDR_LOG_ERROR, "SYSREFOUT:%.4f out of range [%.0f; %.0f]", st->sysrefout, fmin, fmax);
            return -EINVAL;
        }

        for(unsigned i = 0; i < SIZEOF_ARRAY(st->sysref_indiv_ch_delay); ++i)
        {
            if(!st->ch_en[i])
                continue; //do not check if channel is disabled

            lmx1204_sysrefout_channel_delay_t* d = &st->sysref_indiv_ch_delay[i];

            switch(d->phase)
            {
            case SYSREFOUT0_DELAY_PHASE_ICLK0:
            case SYSREFOUT0_DELAY_PHASE_QCLK0:
            case SYSREFOUT0_DELAY_PHASE_QCLK1:
            case SYSREFOUT0_DELAY_PHASE_ICLK1: break;
            default:
                USDR_LOG("1204", USDR_LOG_ERROR, "SYSREFOUT[%u] delay phase:%u is invalid", i, d->phase);
                return -EINVAL;
            }

            if(d->i + d->q != 0x7f)
            {
                USDR_LOG("1204", USDR_LOG_ERROR, "SYSREFOUT[%u] delay I:%u + Q:%u != 127", i, d->i, d->q);
                return -EINVAL;
            }
        }
    }

    if(st->ch_en[LMX1204_CH_LOGIC])
    {
        if(st->clkout_en[LMX1204_CH_LOGIC])
        {
            switch(st->logiclkout_fmt)
            {
            case LMX1204_FMT_LVDS:   st->logiclkout_fmt = LOGICLKOUT_FMT_LVDS; break;
            case LMX1204_FMT_LVPECL: st->logiclkout_fmt = LOGICLKOUT_FMT_LVPECL; break;
            case LMX1204_FMT_CML:    st->logiclkout_fmt = LOGICLKOUT_FMT_CML; break;
            default:
                USDR_LOG("1204", USDR_LOG_ERROR, "LOGICLKOUT_FMT:%u unknown", st->logiclkout_fmt);
                return -EINVAL;
            }
        }

        if(st->sysref_en && st->sysrefout_en[LMX1204_CH_LOGIC])
        {
            switch(st->logisysrefout_fmt)
            {
            case LMX1204_FMT_LVDS:   st->logisysrefout_fmt = LOGISYSREFOUT_FMT_LVDS; break;
            case LMX1204_FMT_LVPECL: st->logisysrefout_fmt = LOGISYSREFOUT_FMT_LVPECL; break;
            case LMX1204_FMT_CML:    st->logisysrefout_fmt = LOGISYSREFOUT_FMT_CML; break;
            default:
                USDR_LOG("1204", USDR_LOG_ERROR, "LOGISYSREFOUT_FMT:%u unknown", st->logisysrefout_fmt);
                return -EINVAL;
            }
        }
    }

    return 0;
}

static const char* lmx1204_decode_clkmux(enum clk_mux_options mux)
{
    switch(mux)
    {
    case CLK_MUX_BUFFER_MODE:     return "CLK_MUX_BUFFER_MODE";
    case CLK_MUX_MULTIPLIER_MODE: return "CLK_MUX_MULTIPLIER_MODE";
    case CLK_MUX_DIVIDER_MODE:    return "CLK_MUX_DIVIDER_MODE";
    }
    return "UNKNOWN";
}

static const char* lmx1204_decode_sysref_mode(enum sysref_mode_options sm)
{
    switch(sm)
    {
    case SYSREF_MODE_CONTINUOUS_GENERATOR_MODE: return "CONTINUOUS_GENERATOR";
    case SYSREF_MODE_PULSER_GENERATOR_MODE:     return "PULSER_GENERATOR";
    case SYSREF_MODE_REPEATER_REPEATER_MODE:    return "REPEATER";
    }
    return "UNKNOWN";
}

static const char* lmx1204_decode_fmt(enum logiclkout_fmt_options fmt)
{
    switch(fmt)
    {
    case LOGICLKOUT_FMT_LVDS :  return "LVDS";
    case LOGICLKOUT_FMT_LVPECL: return "LVPECL";
    case LOGICLKOUT_FMT_CML:    return "CML";
    }
    return "UNKNOWN";
}

static int lmx1204_reset(lmx1204_state_t* st)
{
    uint32_t regs[] =
    {
        MAKE_LMX1204_R0(0, 0, 0, 1),    //set RESET bit
        MAKE_LMX1204_R0(0, 0, 0, 0),    //reset RESET bit
    };

    int res = lmx1204_spi_post(st, regs, SIZEOF_ARRAY(regs));
    if(res)
        return res;

    usleep(10000);

    return 0;
}

static inline uint8_t lmx1204_get_delay_step_size(lmx1204_state_t* st)
{
    uint8_t delay_step_size = SYSREFREQ_DELAY_STEPSIZE_28_PS_1_4_GHZ_TO_2_7_GHZ;
    if(st->clkin > 2400000000 && st->clkin <= 4700000000)
        delay_step_size = SYSREFREQ_DELAY_STEPSIZE_15_PS_2_4_GHZ_TO_4_7_GHZ;
    if(st->clkin > 3100000000 && st->clkin <= 5700000000)
        delay_step_size = SYSREFREQ_DELAY_STEPSIZE_11_PS_3_1_GHZ_TO_5_7_GHZ;
    if(st->clkin > 4500000000 && st->clkin <= 12800000000)
        delay_step_size = SYSREFREQ_DELAY_STEPSIZE_8_PS_4_5_GHZ_TO_12_8_GHZ;

    return delay_step_size;
}

// all params are in lmx1204_state_t struct
int lmx1204_solver(lmx1204_state_t* st, bool prec_mode, bool dry_run)
{
    int res;
    enum clk_mux_options clk_mux;
    unsigned mult_div;

    if(st->clkin > st->clkout)
        clk_mux = CLK_MUX_DIVIDER_MODE;
    else if(st->clkin < st->clkout)
        clk_mux = CLK_MUX_MULTIPLIER_MODE;
    else
        clk_mux = st->filter_mode ? CLK_MUX_MULTIPLIER_MODE : CLK_MUX_BUFFER_MODE;

    st->clk_mux = clk_mux;

    res = lmx1204_solver_prevalidate(st);
    if(res)
        return res;

    double outclk_fact;

    switch(clk_mux)
    {
    case CLK_MUX_BUFFER_MODE:
    {
        mult_div = 1;
        outclk_fact = st->clkin;
        break;
    }
    case CLK_MUX_DIVIDER_MODE:
    {
        mult_div = (unsigned)((double)st->clkin / st->clkout + 0.5);
        if(mult_div < CLK_DIV_MIN || mult_div > CLK_DIV_MAX)
        {
            USDR_LOG("1204", USDR_LOG_ERROR, "CLC_DIV:%u out of range", mult_div);
            return -EINVAL;
        }

        outclk_fact = (double)st->clkin / mult_div;
        break;
    }
    case CLK_MUX_MULTIPLIER_MODE:
    {
        mult_div = (unsigned)(st->clkout / st->clkin + 0.5);
        if(mult_div < CLK_MULT_MIN || mult_div > CLK_MULT_MAX)
        {
            USDR_LOG("1204", USDR_LOG_ERROR, "CLC_MULT:%u out of range", mult_div);
            return -EINVAL;
        }

        outclk_fact = (double)st->clkin * mult_div;
        break;
    }
    }

    if(fabs(outclk_fact - st->clkout) > FREQ_EPS)
    {
        USDR_LOG("1204", USDR_LOG_ERROR, "Calculated CLKOUT:%.4f too rough", outclk_fact);
        return -EINVAL;
    }

    if(prec_mode && st->clkin != st->clkout * mult_div)
    {
        USDR_LOG("1204", USDR_LOG_ERROR, "Cannot solve CLKOUT:%.4f by int divider/multiplier", st->clkout);
        return -EINVAL;
    }

    st->clkout = outclk_fact;
    st->clk_mult_div = mult_div;

    USDR_LOG("1204", USDR_LOG_DEBUG, "CLKIN:%" PRIu64 " CLK_MUX:%s CLK_MULT_DIV:%u CLKOUT:%.4f [FILTER_MODE:%u] [PREC_MODE:%u]",
             st->clkin, lmx1204_decode_clkmux(clk_mux), mult_div, st->clkout, st->filter_mode, prec_mode);

    // need to setup VCO for mult
    if(clk_mux == CLK_MUX_MULTIPLIER_MODE)
    {
        unsigned div_pre = MAX(ceil((double)st->clkin / SMCLK_DIV_PRE_OUT_MAX), SMCLK_DIV_PRE_PL2);
        if(div_pre > SMCLK_DIV_PRE_PL2 && div_pre <= SMCLK_DIV_PRE_PL4)
            div_pre = SMCLK_DIV_PRE_PL4;
        else if(div_pre > SMCLK_DIV_PRE_PL4 && div_pre <= SMCLK_DIV_PRE_PL8)
            div_pre = SMCLK_DIV_PRE_PL8;
        else
            div_pre = SMCLK_DIV_PRE_PL2;

        double fdiv = (double)st->clkin / div_pre / SMCLK_DIV_OUT_MAX;
        unsigned n = MAX(ceil(log2(fdiv)), SMCLK_DIV_PL1);
        if(n > SMCLK_DIV_PL128)
        {
            return -EINVAL; //impossible
        }

        const unsigned div = 1u << n;

        USDR_LOG("1204", USDR_LOG_DEBUG, "[MULT VCO SMCLK] SMCLK_DIV_PRE:%u SMCLK_DIV:%u(%u) F_SM_CLK:%.4f",
                 div_pre, n, div, (double)st->clkin / div_pre / div);

        st->smclk_div_pre = div_pre;
        st->smclk_div = n;
    }

    //need to setup LOGICLKOUT
    if(st->ch_en[LMX1204_CH_LOGIC] && st->clkout_en[LMX1204_CH_LOGIC])
    {
        unsigned div_pre, div;
        double logiclkout_fact;

        if(st->logiclkout == st->clkin)
        {
            div_pre = LOGICLK_DIV_PRE_PL1;
            div = 0; // bypassed
            logiclkout_fact = st->clkin;
        }
        else
        {
            unsigned div_pre_min = MAX(ceil((double)st->clkin / LOGICLK_DIV_PRE_OUT_MAX), LOGICLK_DIV_PRE_PL2);

            bool found = false;
            for(div_pre = div_pre_min; div_pre <= LOGICLK_DIV_PRE_PL4; ++div_pre)
            {
                if(div_pre == LOGICLK_DIV_PRE_PL4 - 1)
                    continue;

                div = (unsigned)((double)st->clkin / div_pre / st->logiclkout + 0.5);
                if(div < LOGICLK_DIV_MIN)
                {
                    USDR_LOG("1204", USDR_LOG_ERROR, "LOGICLKOUT:%.4f too high", st->logiclkout);
                    return -EINVAL;
                }
                if(div > LOGICLK_DIV_MAX)
                    continue;

                logiclkout_fact = (double)st->clkin / div_pre / div;
                if(fabs(st->logiclkout - logiclkout_fact) > FREQ_EPS)
                    continue;
                if(prec_mode && st->clkin != st->logiclkout * div_pre * div)
                    continue;

                found = true;
                break;
            }
            if(!found)
            {
                USDR_LOG("1204", USDR_LOG_ERROR, "Cannot solve LOGICLKOUT:%.4f", st->logiclkout);
                return -EINVAL;
            }
        }

        st->logiclkout = logiclkout_fact;
        st->logiclk_div_pre = div_pre;
        st->logiclk_div_bypass = (div == 0);
        st->logiclk_div = div;

        USDR_LOG("1204", USDR_LOG_DEBUG, "[LOGICLKOUT] LOGICLK_DIV_PRE:%u LOGICLK_DIV:%u%s LOGICLKOUT:%.4f",
                 div_pre, div, div ? "" : "(BYPASSED)", logiclkout_fact);
    }
    else
    {
        USDR_LOG("1204", USDR_LOG_DEBUG, "[LOGICLKOUT]:disabled");
    }

    //sysref
    if(st->sysref_en)
    {
        uint64_t f_interpol;

        if(st->clkin <= 1600000000ull)
        {
            st->sysref_delay_div = SYSREF_DELAY_DIV_PL2_LE_1_6_GHZ;
            f_interpol = st->clkin >> 1;
        }
        else if(st->clkin <= 3200000000ull)
        {
            st->sysref_delay_div = SYSREF_DELAY_DIV_PL4_1_6_GHZ_TO_3_2_GHZ;
            f_interpol = st->clkin >> 2;
        }
        else if(st->clkin <= 6400000000ull)
        {
            st->sysref_delay_div = SYSREF_DELAY_DIV_PL8_3_2_GHZ_TO_6_4_GHZ;
            f_interpol = st->clkin >> 3;
        }
        else
        {
            st->sysref_delay_div = SYSREF_DELAY_DIV_PL16_6_4_GHZ_TO_12_8_GHZ;
            f_interpol = st->clkin >> 4;
        }

        if(f_interpol <= 200000000ull)
            st->sysref_delay_scale = SYSREFOUT0_DELAY_SCALE_150_MHZ_TO_200_MHZ;
        else if(f_interpol <= 400000000ull)
            st->sysref_delay_scale = SYSREFOUT0_DELAY_SCALE_200_MHZ_TO_400_MHZ;
        else
            st->sysref_delay_scale = SYSREFOUT0_DELAY_SCALE_400_MHZ_TO_800_MHZ;

        unsigned div_pre = SYSREF_DIV_PRE_PL4, div = 0x3; //defaults

        if(st->sysref_mode == SYSREF_MODE_REPEATER_REPEATER_MODE)
        {
            if(st->sysrefout != st->sysrefreq)
            {
                USDR_LOG("1204", USDR_LOG_ERROR, "[SYSREF] SYSREFREQ must be equal to SYSREFOUT for repeater mode");
                return -EINVAL;
            }
        }
        else
        {
            if(f_interpol % (uint64_t)st->sysrefout)
            {
                USDR_LOG("1204", USDR_LOG_ERROR, "[SYSREF] F_INTERPOL:%" PRIu64 " %% SYSREFOUTCLK:%.0f != 0", f_interpol, st->sysrefout);
                return -EINVAL;
            }

            unsigned min_div_pre = MAX(log2f(ceil((double)st->clkin / SYSREF_DIV_PRE_OUT_MAX)), SYSREF_DIV_PRE_PL1);

            bool found = true;

            for(div_pre = min_div_pre; div_pre <= SYSREF_DIV_PRE_PL4; ++div_pre)
            {
                div = (unsigned)((st->clkin >> div_pre) / st->sysrefout + 0.5);
                if(div < SYSREF_DIV_MIN)
                {
                    USDR_LOG("1204", USDR_LOG_ERROR, "SYSREFOUT:%.4f too high", st->sysrefout);
                    return -EINVAL;
                }
                if(div > SYSREF_DIV_MAX)
                    continue;

                if(st->clkin != (((uint64_t)st->sysrefout * div) << div_pre))
                {
                    USDR_LOG("1204", USDR_LOG_ERROR, "SYSREFOUT:%.4f cannot be solved in integers", st->sysrefout);
                    return -EINVAL;
                }

                found = true;
                break;
            }

            if(!found)
            {
                USDR_LOG("1204", USDR_LOG_ERROR, "Cannot solve SYSREFOUT:%.4f", st->sysrefout);
                return -EINVAL;
            }
        }

        st->sysref_div_pre = div_pre;
        st->sysref_div     = div;

        USDR_LOG("1204", USDR_LOG_DEBUG, "[SYSREFCLK] SYSREFREQ:%" PRIu64 " SYSREF_MODE:%s(%u) SYSREFOUT:%.4f",
                 st->sysrefreq, lmx1204_decode_sysref_mode(st->sysref_mode), st->sysref_mode, st->sysrefout);

        if(st->sysref_mode != SYSREF_MODE_REPEATER_REPEATER_MODE)
        {
            USDR_LOG("1204", USDR_LOG_DEBUG, "[SYSREFCLK] SYSREF_DELAY_DIV:%u F_INTERPOL:%" PRIu64 " DELAY_SCALE:%u SYSREF_DIV_PRE:%u(%u) SYSREF_DIV:%u",
                     st->sysref_delay_div, f_interpol, st->sysref_delay_scale, (1 << st->sysref_div_pre), st->sysref_div_pre, st->sysref_div);
        }
    }
    else
    {
        USDR_LOG("1204", USDR_LOG_DEBUG, "[SYSREFCLK]:disabled");
    }

    // succesfull solution
    USDR_LOG("1204", USDR_LOG_INFO, "LMX1204 SOLUTION FOUND:");
    USDR_LOG("1204", USDR_LOG_INFO, "CLKIN:%" PRIu64 " SYSREFREQ:%" PRIu64, st->clkin, st->sysrefreq);
    for(unsigned i = 0; i < LMX1204_OUT_CNT; ++i)
    {
        USDR_LOG("1204", USDR_LOG_INFO, "CLKOUT%u   :%.4f EN:%u", i, st->clkout, st->ch_en[0] && st->clkout_en[0]);
        USDR_LOG("1204", USDR_LOG_INFO, "SYSREFOUT%u:%.4f EN:%u", i, st->sysrefout, st->sysref_en && st->ch_en[0] && st->sysrefout_en[0]);
    }
    USDR_LOG("1204", USDR_LOG_INFO, "LOGICLKOUT    :%.4f EN:%u FMT:%s",
             st->logiclkout, st->ch_en[LMX1204_CH_LOGIC] && st->clkout_en[LMX1204_CH_LOGIC], lmx1204_decode_fmt(st->logiclkout_fmt));
    USDR_LOG("1204", USDR_LOG_INFO, "LOGICSYSREFOUT:%.4f EN:%u FMT:%s",
             st->sysrefout, st->sysref_en && st->ch_en[LMX1204_CH_LOGIC] && st->sysrefout_en[LMX1204_CH_LOGIC], lmx1204_decode_fmt(st->logisysrefout_fmt));
    USDR_LOG("1204", USDR_LOG_INFO, "--------------");

    //registers

    res = dry_run ? 0 : lmx1204_reset(st);
    if(res)
    {
        USDR_LOG("1204", USDR_LOG_ERROR, "lmx1204_reset() err:%d", res);
        return res;
    }

    res = dry_run ? 0 : lmx1204_reset_main_divider(st, true);
    if(res)
    {
        USDR_LOG("1204", USDR_LOG_ERROR, "lmx1204_reset_main_divider(1) err:%d", res);
        return res;
    }

    uint32_t regs[] =
    {
        MAKE_LMX1204_REG_WR(R90, st->logiclk_div_bypass ? 0x60 : 0x00),
        //MAKE_LMX1204_R86(0),                //MUXOUT_EN_OVRD=0
        MAKE_LMX1204_R79(0, st->logiclk_div_bypass ? 0x104 : 0x5),
        MAKE_LMX1204_REG_WR(0x4c, 0),
        MAKE_LMX1204_R72(0, 0, 0, 0, SYSREF_DELAY_BYPASS_ENGAGE_IN_GENERATOR_MODE__BYPASS_IN_REPEATER_MODE),

        // need for MULT VCO calibration
        MAKE_LMX1204_R67(st->clk_mux == CLK_MUX_MULTIPLIER_MODE ? 0x51cb : 0x50c8),
        MAKE_LMX1204_R34(0, st->clk_mux == CLK_MUX_MULTIPLIER_MODE ? 0x04c5 : 0),
        MAKE_LMX1204_R33(st->clk_mux == CLK_MUX_MULTIPLIER_MODE ? /*0x5666*/0x6666 : 0x7777),
        //

        MAKE_LMX1204_R25(0x4, 0, st->clk_mux == CLK_MUX_MULTIPLIER_MODE ? st->clk_mult_div : st->clk_mult_div - 1, st->clk_mux),
        MAKE_LMX1204_R24(0,0,0,1),          //enable temp sensor
        MAKE_LMX1204_R23(1, 1, 1, 0, 1, st->sysref_delay_scale, st->sysref_delay_scale, st->sysref_delay_scale),
        MAKE_LMX1204_R22(st->sysref_delay_scale, st->sysref_delay_scale, st->sysref_delay_div, 0, st->sysref_indiv_ch_delay[LMX1204_CH_LOGIC].q),
        MAKE_LMX1204_R21(st->sysref_indiv_ch_delay[LMX1204_CH_LOGIC].i, st->sysref_indiv_ch_delay[LMX1204_CH_LOGIC].phase, st->sysref_indiv_ch_delay[LMX1204_CH3].q),
        MAKE_LMX1204_R20(st->sysref_indiv_ch_delay[LMX1204_CH3].i, st->sysref_indiv_ch_delay[LMX1204_CH3].phase, st->sysref_indiv_ch_delay[LMX1204_CH2].q),
        MAKE_LMX1204_R19(st->sysref_indiv_ch_delay[LMX1204_CH2].i, st->sysref_indiv_ch_delay[LMX1204_CH2].phase, st->sysref_indiv_ch_delay[LMX1204_CH1].q),
        MAKE_LMX1204_R18(st->sysref_indiv_ch_delay[LMX1204_CH1].i, st->sysref_indiv_ch_delay[LMX1204_CH1].phase, st->sysref_indiv_ch_delay[LMX1204_CH0].q),
        MAKE_LMX1204_R17(0, st->sysref_indiv_ch_delay[LMX1204_CH0].i, st->sysref_indiv_ch_delay[LMX1204_CH0].phase, st->sysref_mode),
        MAKE_LMX1204_R16(0x1, st->sysref_div), //0x1 == sysref pulse count
        MAKE_LMX1204_R15(0, st->sysref_div_pre, 0x3, st->sysref_en ? 1 : 0, 0, 0),
        MAKE_LMX1204_R13(0, lmx1204_get_delay_step_size(st)),
/*
        //according do doc: program R79 and R90 before setting logiclk_div_bypass
        //desc order is broken here!
        MAKE_LMX1204_R8(0, st->logiclk_div_pre, 1, st->ch_en[LMX1204_CH_LOGIC] ? 1 : 0, st->logisysrefout_fmt, st->logiclkout_fmt),
        MAKE_LMX1204_R79(0, st->logiclk_div_bypass ? 0x5 : 0x104),
        MAKE_LMX1204_REG_WR(R90, st->logiclk_div_bypass ? 0x60 : 0x00),
        MAKE_LMX1204_R9(0, 0, 0, st->logiclk_div_bypass ? 1 : 0, 0, st->logiclk_div),
        //
*/
        MAKE_LMX1204_R9(0, 0, 0, st->logiclk_div_bypass ? 1 : 0, 0, st->logiclk_div),
        MAKE_LMX1204_R8(0, st->logiclk_div_pre, 1, st->ch_en[LMX1204_CH_LOGIC] ? 1 : 0, st->logisysrefout_fmt, st->logiclkout_fmt),
        MAKE_LMX1204_R7(0, 0, 0, 0, 0, 0, 0, st->sysrefout_en[LMX1204_CH_LOGIC] ? 1 : 0),
        MAKE_LMX1204_R6(st->clkout_en[LMX1204_CH_LOGIC], 0x3, 0x3, 0x3, 0x3, 0x4),
        MAKE_LMX1204_R4(0, 0x6, 0x6,
                        st->sysrefout_en[LMX1204_CH3], st->sysrefout_en[LMX1204_CH2], st->sysrefout_en[LMX1204_CH1], st->sysrefout_en[LMX1204_CH0],
                        st->clkout_en[LMX1204_CH3], st->clkout_en[LMX1204_CH2], st->clkout_en[LMX1204_CH1], st->clkout_en[LMX1204_CH0]),
        MAKE_LMX1204_R3(st->ch_en[LMX1204_CH3], st->ch_en[LMX1204_CH2], st->ch_en[LMX1204_CH1], st->ch_en[LMX1204_CH0], 1, 1, 1, 1, 1, 0, st->smclk_div),
        MAKE_LMX1204_R2(0,0,st->smclk_div_pre, st->clk_mux == CLK_MUX_MULTIPLIER_MODE ? 1 : 0, 0x3),
        //
        MAKE_LMX1204_R0(0, 0, 0, 0),    //reset RESET bit last
    };

    res = dry_run ? common_print_registers_a8d16(regs, SIZEOF_ARRAY(regs), USDR_LOG_DEBUG) : lmx1204_spi_post(st, regs, SIZEOF_ARRAY(regs));
    if(res)
        return res;

    usleep(10000);

    res = dry_run ? 0 : lmx1204_reset_main_divider(st, false);
    if(res)
    {
        USDR_LOG("1204", USDR_LOG_ERROR, "lmx1204_reset_main_divider(0) err:%d", res);
        return res;
    }

    return 0;
}

int lmx1204_calibrate(lmx1204_state_t* st)
{
    if(st->clk_mux != CLK_MUX_MULTIPLIER_MODE)
    {
        USDR_LOG("1204", USDR_LOG_DEBUG, "VCO calibration not needed for BUFFER & DIV modes");
        return 0;
    }

    uint32_t regs[] =
    {
        MAKE_LMX1204_R67(0x51cb),
        MAKE_LMX1204_R34(0, 0x04c5),
        MAKE_LMX1204_R33(0x5666),
        MAKE_LMX1204_R0(0, 0, 0, 0),
    };

    int res = lmx1204_spi_post(st, regs, SIZEOF_ARRAY(regs));
    if(res)
        return res;

    return 0;
}

int lmx1204_wait_pll_lock(lmx1204_state_t* st, unsigned timeout)
{
    int res = 0;
    unsigned elapsed = 0;

    if(st->clk_mux != CLK_MUX_MULTIPLIER_MODE)
    {
        USDR_LOG("1204", USDR_LOG_DEBUG, "VCO lock not needed for BUFFER & DIV modes");
        return 0;
    }

    uint16_t r75;
    while(timeout == 0 || elapsed < timeout)
    {
        uint64_t tk = clock_get_time();

        res = lmx1204_spi_get(st, R75, &r75);
        if(res)
            return res;

        const uint16_t lock_detect_status = (r75 & RB_LD_MSK) >> RB_LD_OFF;
        switch(lock_detect_status)
        {
        case RB_LD_LOCKED: return 0;
        default:
            usleep(100);
            elapsed += (clock_get_time() - tk);
        }
    }

    return -ETIMEDOUT;
}

int lmx1204_reload_sysrefout_ch_delay(lmx1204_state_t* st)
{
    uint32_t regs[] =
    {
        MAKE_LMX1204_R22(st->sysref_delay_scale, st->sysref_delay_scale, st->sysref_delay_div, 0, st->sysref_indiv_ch_delay[LMX1204_CH_LOGIC].q),
        MAKE_LMX1204_R21(st->sysref_indiv_ch_delay[LMX1204_CH_LOGIC].i, st->sysref_indiv_ch_delay[LMX1204_CH_LOGIC].phase, st->sysref_indiv_ch_delay[LMX1204_CH3].q),
        MAKE_LMX1204_R20(st->sysref_indiv_ch_delay[LMX1204_CH3].i, st->sysref_indiv_ch_delay[LMX1204_CH3].phase, st->sysref_indiv_ch_delay[LMX1204_CH2].q),
        MAKE_LMX1204_R19(st->sysref_indiv_ch_delay[LMX1204_CH2].i, st->sysref_indiv_ch_delay[LMX1204_CH2].phase, st->sysref_indiv_ch_delay[LMX1204_CH1].q),
        MAKE_LMX1204_R18(st->sysref_indiv_ch_delay[LMX1204_CH1].i, st->sysref_indiv_ch_delay[LMX1204_CH1].phase, st->sysref_indiv_ch_delay[LMX1204_CH0].q),
        MAKE_LMX1204_R17(0, st->sysref_indiv_ch_delay[LMX1204_CH0].i, st->sysref_indiv_ch_delay[LMX1204_CH0].phase, st->sysref_mode),
    };

    return lmx1204_spi_post(st, regs, SIZEOF_ARRAY(regs));
}

int lmx1204_sysref_windowing_beforesync(lmx1204_state_t* st)
{
    int res;

    uint8_t delay_step_size = lmx1204_get_delay_step_size(st);
    USDR_LOG("1204", USDR_LOG_DEBUG, "DELAY_STEPSIZE:%u", delay_step_size);

    {
        uint32_t regs[] =
        {
            MAKE_LMX1204_R9(0, 1/*SYNC_EN*/, 0, st->logiclk_div_bypass ? 1 : 0, 0, st->logiclk_div),
            MAKE_LMX1204_R14(0, 0, 0, 1/*CLKPOS_CAPTURE_EN*/, 1, 0),
            MAKE_LMX1204_R13(0, delay_step_size),
        };

        res = lmx1204_spi_post(st, regs, SIZEOF_ARRAY(regs));
        if(res)
            return res;
    }

    {
        uint16_t r15;
        res = lmx1204_spi_get(st, R15, &r15);
        if(res)
            return res;

        uint32_t regval_set = MAKE_LMX1204_REG_WR(R15, r15 |  SYSREFREQ_CLR_MSK);
        uint32_t regval_rst = MAKE_LMX1204_REG_WR(R15, r15 & ~SYSREFREQ_CLR_MSK);

        res = lmx1204_spi_post(st, &regval_set, 1);
        res = res ? res : lmx1204_spi_post(st, &regval_rst, 1);
    }

    return res;
}

int lmx1204_sysref_windowing_aftersync(lmx1204_state_t* st)
{
    uint32_t regs[] =
    {
        MAKE_LMX1204_R9(0, 0/*SYNC_EN*/, 0, st->logiclk_div_bypass ? 1 : 0, 0, st->logiclk_div),
        MAKE_LMX1204_R14(0, 0, 0, 0/*CLKPOS_CAPTURE_EN*/, 1, 0),
    };
    return lmx1204_spi_post(st, regs, SIZEOF_ARRAY(regs));
}

int lmx1204_sysref_windowing_capture(lmx1204_state_t* st)
{
    int res;
    uint16_t r11, r12;

    res = lmx1204_spi_get(st, R11, &r11);
    res = res ? res : lmx1204_spi_get(st, R12, &r12);
    if(res)
        return res;

    uint32_t clkpos = ((uint32_t)r12 << 16) | r11;

    unsigned delay;
    res = common_ti_calc_sync_delay(clkpos, &delay);
    if(res)
        return res;

    {
        uint32_t reg = MAKE_LMX1204_R15(0, st->sysref_div_pre, 0x3, st->sysref_en ? 1 : 0, delay, 0);
        res = lmx1204_spi_post(st, &reg, 1);
        if(res)
            return res;
    }

    return 0;
}
