// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "def_lmk05318.h"
#include "lmk05318.h"
#include "lmk05318_rom.h"

#include <usdr_logging.h>
#include "../../xdsp/attribute_switch.h"

enum {
    VCO_APLL1 = 2500000000ull,
    VCO_APLL1_MIN = 2499750000ull,
    VCO_APLL1_MAX = 2500250000ull,

    VCO_APLL2_MIN = 5500000000ull,
    VCO_APLL2_MAX = 6250000000ull,

    APLL1_PD_MIN = 1000000,
    APLL1_PD_MAX = 50000000,

    APLL2_PD_MIN = 10000000,
    APLL2_PD_MAX = 150000000,

    OUT_FREQ_MAX = 800000000ull,

    XO_FREF_MAX = 100000000ull,
    XO_FREF_MIN = 10000000ull,

    APLL1_DIVIDER_MIN = 1,
    APLL1_DIVIDER_MAX = 32,

    APLL2_PDIV_MIN = 2,
    APLL2_PDIV_MAX = 7,
};


int lmk05318_reg_wr(lmk05318_state_t* d, uint16_t reg, uint8_t out)
{
    uint8_t data[3] = { reg >> 8, reg, out };
    return lowlevel_ls_op(d->dev, d->subdev,
                          USDR_LSOP_I2C_DEV, d->lsaddr,
                          0, NULL, 3, data);
}

int lmk05318_reg_rd(lmk05318_state_t* d, uint16_t reg, uint8_t* val)
{
    uint8_t addr[2] = { reg >> 8, reg };
    return lowlevel_ls_op(d->dev, d->subdev,
                          USDR_LSOP_I2C_DEV, d->lsaddr,
                          1, val, 2, addr);
}

int lmk05318_reg_get_u32(lmk05318_state_t* d,
                         uint16_t reg, uint8_t* val)
{
    uint8_t addr[2] = { reg >> 8, reg };
    return lowlevel_ls_op(d->dev, d->subdev,
                          USDR_LSOP_I2C_DEV, d->lsaddr,
                          4, val, 2, &addr[0]);
}

int lmk05318_reg_wr_n(lmk05318_state_t* d, const uint32_t* regs, unsigned count)
{
    int res;
    for (unsigned j = 0; j < count; j++) {
        uint16_t addr = regs[j] >> 8;
        uint8_t data = regs[j];

        res = lmk05318_reg_wr(d, addr, data);
        if (res)
            return res;
    }

    return 0;
}

int lmk05318_create(lldev_t dev, unsigned subdev, unsigned lsaddr, lmk05318_state_t* out)
{
    int res;
    uint8_t dummy[4];

    out->dev = dev;
    out->subdev = subdev;
    out->lsaddr = lsaddr;

    res = lmk05318_reg_get_u32(out, 0, &dummy[0]);
    if (res)
        return res;

    USDR_LOG("5318", USDR_LOG_INFO, "LMK05318 DEVID[0/1/2/3] = %02x %02x %02x %02x\n", dummy[3], dummy[2], dummy[1], dummy[0]);

    if ( dummy[3] != 0x10 || dummy[2] != 0x0b || dummy[1] != 0x35 || dummy[0] != 0x42 ) {
        return -ENODEV;
    }

    // Do the initialization
    res = lmk05318_reg_wr_n(out, lmk05318_rom, SIZEOF_ARRAY(lmk05318_rom));
    if (res)
        return res;

    // Reset
    uint32_t regs[] = {
        lmk05318_rom[0] | (1 << RESET_SW_OFF),
        lmk05318_rom[0] | (0 << RESET_SW_OFF),
        MAKE_LMK05318_PLL1_CTRL0(0),
        MAKE_LMK05318_PLL1_CTRL0(1),
        MAKE_LMK05318_PLL1_CTRL0(0),
    };
    res = lmk05318_reg_wr_n(out, regs, SIZEOF_ARRAY(regs));
    if (res)
        return res;

    out->fref_pll2_div_rp = 3;
    out->fref_pll2_div_rs = (((VCO_APLL1 + APLL2_PD_MAX - 1) / APLL2_PD_MAX) + out->fref_pll2_div_rp - 1) / out->fref_pll2_div_rp;

    out->xo.fref = 0;
    out->xo.doubler_enabled = false;
    out->xo.fdet_bypass = false;
    out->xo.type = XO_TYPE_DC_DIFF_EXT;

    USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 initialized\n");
    return 0;
}


int lmk05318_tune_apll2(lmk05318_state_t* d, uint32_t freq, unsigned *last_div)
{
    const unsigned apll2_post_div = 2;

    unsigned fref = VCO_APLL1 / d->fref_pll2_div_rp / d->fref_pll2_div_rs;
    if (fref < APLL2_PD_MIN || fref > APLL2_PD_MAX) {
        USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 APLL2 PFD should be in range [%" PRIu64 ";%" PRIu64 "] but got %d!\n",
                 (uint64_t)APLL2_PD_MIN, (uint64_t)APLL2_PD_MAX, fref);
        return -EINVAL;
    }
    if (freq < 1e6) {
        // Disable
        uint32_t regs[] = {
            MAKE_LMK05318_PLL2_CTRL0(d->fref_pll2_div_rs - 1, d->fref_pll2_div_rp - 3, 1),
        };
        return lmk05318_reg_wr_n(d, regs, SIZEOF_ARRAY(regs));;
    }

    unsigned div = ((VCO_APLL2_MAX / apll2_post_div)) / freq;
    uint64_t fvco = (uint64_t)freq * div * apll2_post_div;
    unsigned n = fvco / fref;
    unsigned num = (fvco - n * (uint64_t)fref) * (1ull << 24) / fref;
    int res;

    USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 APLL2 FREQ=%u FVCO=%" PRIu64 " N=%d NUM=%d DIV=%d\n", freq, fvco, n, num, div);

    uint32_t regs[] = {
        MAKE_LMK05318_PLL2_CTRL0(d->fref_pll2_div_rs - 1, d->fref_pll2_div_rp - 3, 1),
        MAKE_LMK05318_PLL2_CTRL2(apll2_post_div - 1, apll2_post_div - 1),
        MAKE_LMK05318_PLL2_NDIV_BY0(n),
        MAKE_LMK05318_PLL2_NDIV_BY1(n),
        MAKE_LMK05318_PLL2_NUM_BY0(num),
        MAKE_LMK05318_PLL2_NUM_BY1(num),
        MAKE_LMK05318_PLL2_NUM_BY2(num),
        MAKE_LMK05318_PLL2_CTRL0(d->fref_pll2_div_rs - 1, d->fref_pll2_div_rp - 3, 0),
    };

    res = lmk05318_reg_wr_n(d, regs, SIZEOF_ARRAY(regs));
    if (res)
        return res;

    *last_div = div;
    return 0;
}

int lmk05318_set_xo_fref(lmk05318_state_t* d, uint32_t xo_fref, int xo_type,
                         bool xo_doubler_enabled, bool xo_fdet_bypass)
{
    if(xo_fref < XO_FREF_MIN || xo_fref > XO_FREF_MAX)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 XO input fref should be in range [%" PRIu64 ";%" PRIu64 "] but got %d!\n",
                 (uint64_t)XO_FREF_MIN, (uint64_t)XO_FREF_MAX, xo_fref);
        return -EINVAL;
    }

    switch((int)xo_type)
    {
    case XO_TYPE_DC_DIFF_EXT:
    case XO_TYPE_AC_DIFF_EXT:
    case XO_TYPE_AC_DIFF_INT_100:
    case XO_TYPE_HCSL_INT_50:
    case XO_TYPE_CMOS:
    case XO_TYPE_SE_INT_50:
        break;
    default:
        USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 XO input type %d is not supported!\n", (int)xo_type);
        return -EINVAL;
    }

    uint32_t regs[] = {
        MAKE_LMK05318_XO_CLKCTL1(xo_doubler_enabled ? 1 : 0, xo_fdet_bypass ? 1 : 0),
        MAKE_LMK05318_XO_CLKCTL2(xo_type)
    };

    int res = lmk05318_reg_wr_n(d, regs, SIZEOF_ARRAY(regs));
    if(res)
        return res;

    d->xo.fref = xo_fref;
    d->xo.doubler_enabled = xo_doubler_enabled;
    d->xo.type = xo_type;
    d->xo.fdet_bypass = xo_fdet_bypass;

    return 0;
}

int lmk05318_tune_apll1(lmk05318_state_t* d,
                        uint32_t xo_fref, int xo_type,
                        bool xo_doubler_enabled, bool xo_fdet_bypass, bool dpll_mode)
{
    int res = lmk05318_set_xo_fref(d, xo_fref, xo_type, xo_doubler_enabled, xo_fdet_bypass);
    if(res)
        return res;

    unsigned fref = (d->xo.fref / APLL1_DIVIDER_MAX) * (d->xo.doubler_enabled ? 2 : 1);
    uint64_t fvco = VCO_APLL1;
    unsigned n = fvco / fref;
    uint64_t num = (fvco - n * (uint64_t)fref) * (1ull << 40) / fref;

    USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 APLL1 FVCO=%" PRIu64 " N=%d NUM=%" PRIu64 "\n", fvco, n, num);

    uint32_t regs[] = {
        MAKE_LMK05318_PLL1_CTRL0(1),
        MAKE_LMK05318_XO_CONFIG(APLL1_DIVIDER_MAX - 1),
        MAKE_LMK05318_PLL1_MODE(dpll_mode ? 1 : 0),
        MAKE_LMK05318_PLL1_NDIV_BY0(n),
        MAKE_LMK05318_PLL1_NDIV_BY1(n),
        MAKE_LMK05318_PLL1_NUM_BY0(num),
        MAKE_LMK05318_PLL1_NUM_BY1(num),
        MAKE_LMK05318_PLL1_NUM_BY2(num),
        MAKE_LMK05318_PLL1_NUM_BY3(num),
        MAKE_LMK05318_PLL1_NUM_BY4(num),
        MAKE_LMK05318_PLL1_CTRL0(0),
    };

    res = lmk05318_reg_wr_n(d, regs, SIZEOF_ARRAY(regs));
    if (res)
        return res;

    return 0;
}


int lmk05318_set_out_div(lmk05318_state_t* d, unsigned port, uint64_t udiv)
{
    if (port > 7)
        return -EINVAL;
    if (udiv == 0)
        return -EINVAL;

    unsigned div = udiv - 1;
    uint32_t regs[] = {
        (port == 7) ? MAKE_LMK05318_OUTDIV_7(div) :
        (port == 6) ? MAKE_LMK05318_OUTDIV_6(div) :
        (port == 5) ? MAKE_LMK05318_OUTDIV_5(div) :
        (port == 4) ? MAKE_LMK05318_OUTDIV_4(div) :
        (port == 3 || port == 2) ? MAKE_LMK05318_OUTDIV_2_3(div) : MAKE_LMK05318_OUTDIV_0_1(div),
    };
    return lmk05318_reg_wr_n(d, regs, SIZEOF_ARRAY(regs));
}

int lmk05318_set_out_mux(lmk05318_state_t* d, unsigned port, bool pll1, unsigned otype)
{
    unsigned ot;
    switch (otype) {
    case LVDS: ot = OUT_OPTS_AC_LVDS; break;
    case CML: ot = OUT_OPTS_AC_CML; break;
    case LVPECL: ot = OUT_OPTS_AC_LVPECL; break;
    case LVCMOS: ot = OUT_OPTS_LVCMOS_P_N; break;
    default: ot = OUT_OPTS_Disabled; break;
    }
    unsigned mux = (pll1) ? OUT_PLL_SEL_APLL1_P1 : OUT_PLL_SEL_APLL2_P1;

    if (port > 7)
        return -EINVAL;

    uint32_t regs[] = {
        (port == 0) ? MAKE_LMK05318_OUTCTL_0(mux, ot) :
        (port == 1) ? MAKE_LMK05318_OUTCTL_1(ot) :
        (port == 2) ? MAKE_LMK05318_OUTCTL_2(mux, ot) :
        (port == 3) ? MAKE_LMK05318_OUTCTL_3(ot) :
        (port == 4) ? MAKE_LMK05318_OUTCTL_4(mux, ot) :
        (port == 5) ? MAKE_LMK05318_OUTCTL_5(mux, ot) :
        (port == 6) ? MAKE_LMK05318_OUTCTL_6(mux, ot) : MAKE_LMK05318_OUTCTL_7(mux, ot),
    };
    return lmk05318_reg_wr_n(d, regs, SIZEOF_ARRAY(regs));
}





static int lmk05318_comp_port(const void * elem1, const void * elem2)
{
    const lmk05318_out_config_t* f = (lmk05318_out_config_t*)elem1;
    const lmk05318_out_config_t* s = (lmk05318_out_config_t*)elem2;

    if(f->port < s->port) return -1;
    if(f->port > s->port) return  1;
    return 0;
}


static uint64_t lmk05318_max_odiv(unsigned port)
{
    assert(port < MAX_OUT_PORTS);

    switch(port)
    {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6: return 256ull;
    case 7: return 256ull * 256 * 256 * 256;
    }
    return 1;
}

static range_t lmk05318_get_freq_range(const lmk05318_out_config_t* cfg)
{
    range_t r;

    if(cfg->wanted.freq >= cfg->wanted.freq_delta_minus)
        r.min = cfg->wanted.freq - cfg->wanted.freq_delta_minus;
    else
        r.min = 1;

    r.max = cfg->wanted.freq + cfg->wanted.freq_delta_plus;

    return r;
}

/*
 * The main formula for the valid div:
 *      (int)[f_in/(f_out + eps) != (int)[f_in/(f_out - eps)]
 * it means that we have a natural int divider value somewhere between [f-eps; f+eps] - the most obvious it's (int)(f_out + eps) + 1
 *
 * This function returns:
 *      odiv value - if it can be solved
 *      0 - otherwise
 */

enum {
    freq_too_low = -1,
    freq_too_high = 1,
    freq_ok = 0,
    freq_invalid = 42,
};

VWLT_ATTRIBUTE(optimize("-Ofast"))
static inline int lmk05318_get_output_divider(const lmk05318_out_config_t* cfg, uint64_t ifreq, uint64_t* div)
{
    if(cfg->freq_min == cfg->freq_max)
    {
        const uint32_t f = cfg->freq_min;

        *div = ifreq / f;

        if(*div > cfg->max_odiv)
            return freq_too_high;

        if(*div < 1)
            return freq_too_low;

        if(*div * f == ifreq)
            return freq_ok;

        return freq_invalid;
    }

    const uint64_t div_min = ifreq / cfg->freq_max;
    const uint64_t div_max = ifreq / cfg->freq_min;

    if(div_min > cfg->max_odiv)
        return freq_too_high;
    if(div_max < 1)
        return freq_too_low;

    if(div_min != div_max)
    {
        *div = div_min + 1;
        return freq_ok;
    }

    return freq_invalid;
}

struct pd_array
{
    range_t pd_range;
    unsigned port_count;
    unsigned port_ids[MAX_REAL_PORTS];
};
typedef struct pd_array pd_array_t;

VWLT_ATTRIBUTE(optimize("-Ofast"))
static inline int bisect_finder(lmk05318_out_config_t* outs,
                                pd_array_t* pd_arr, unsigned pd_arr_num, unsigned pd1, unsigned* pd2,
                                uint64_t f_in,
                                bool* all_freqs_invalid)
{
    int direction_flag = 0;
    bool invalid_freq = false;

    pd_array_t* p1 = pd_arr + 0;
    pd_array_t* p2 = pd_arr_num == 2 ? pd_arr + 1 : NULL;

    for(unsigned i = 0; i < p1->port_count; ++i)
    {
        lmk05318_out_config_t* out = outs + p1->port_ids[i];

        uint64_t odiv = 0;
        int res = lmk05318_get_output_divider(out, f_in, &odiv);

        if(res == freq_invalid)
        {
            invalid_freq = true;
        }
        else
            direction_flag += res;

        if(!res)
        {
            out->result.out_div = odiv;
            out->result.freq = (double)f_in / odiv;
            out->result.mux = OUT_PLL_SEL_APLL2_P1;

//            USDR_LOG("5318", USDR_LOG_WARNING, "found PD1 port:%d solution freq:%.2f OD:%lu PD1:%d VCO2:%lu",
//                     out->port, out->result.freq, odiv, pd1, f_in * pd1);
        }
    }

    //found solution for PD1 -> check PD2
    if(direction_flag == 0 && !invalid_freq && p2)
    {
        const uint32_t fvco2 = f_in * pd1;
        for(unsigned _pd2 = p2->pd_range.min; _pd2 <= p2->pd_range.max; ++_pd2)
        {
            for(unsigned j = 0; j < p2->port_count; ++j)
            {
                lmk05318_out_config_t* out = outs + p2->port_ids[j];

                uint64_t odiv = 0;
                uint32_t f = fvco2 / _pd2;
                int res = lmk05318_get_output_divider(out, f, &odiv);

//                USDR_LOG("5318", USDR_LOG_WARNING, "\tsearch PD2 port:%d solution freq:%d PD2:%d VCO2:%lu... RES=%d",
//                         out->port, out->wanted.freq, _pd2, f_in * pd1, res);

                if(res == freq_invalid)
                {
                    invalid_freq = true;
                }

                if(!res)
                {
                    *pd2 = _pd2;
                    out->result.out_div = odiv;
                    out->result.freq = (double)f / odiv;
                    out->result.mux = OUT_PLL_SEL_APLL2_P2;
                }
            }
        }
    }

    *all_freqs_invalid = (direction_flag == 0 && invalid_freq);
    return direction_flag;
}

VWLT_ATTRIBUTE(optimize("-Ofast"))
int lmk05318_solver(lmk05318_state_t* d, lmk05318_out_config_t* _outs, unsigned n_outs)
{
    if(!_outs || !n_outs || n_outs > MAX_OUT_PORTS)
        return -EINVAL;

    // internally we have only _6_ out divs and freqs (0==1 and 2==3), except output type, but it does not matter here
    lmk05318_out_config_t outs[MAX_OUT_PORTS - 2];
    memset(outs, 0, sizeof(outs));

    for(unsigned i = 0; i < n_outs; ++i)
    {
        lmk05318_out_config_t* out = _outs + i;

        if(out->port > MAX_OUT_PORTS - 1)
            return -EINVAL;

        unsigned port;
        if(out->port < 2)
            port = 0;
        else if(out->port < 4)
            port = 1;
        else
            port = out->port - 2;

        lmk05318_out_config_t* norm_out = outs + port;

        // check dup ports and 0-1 2-3 equality
        if(norm_out->wanted.freq &&
            (norm_out->wanted.freq != out->wanted.freq ||
             norm_out->wanted.freq_delta_plus != out->wanted.freq_delta_plus ||
             norm_out->wanted.freq_delta_minus != out->wanted.freq_delta_minus ||
             norm_out->wanted.revert_phase != out->wanted.revert_phase
            ))
            return -EINVAL;

        range_t r = lmk05318_get_freq_range(out);
        out->freq_min = r.min;
        out->freq_max = r.max;
        out->max_odiv = lmk05318_max_odiv(out->port);

        *norm_out = *out;
        norm_out->solved = false;
    }

    //now outs[] contains effective ports ordered (0..5) config.
    //some elems may be not initialized (wanted.freq == 0) and should not be processed.
    for(unsigned i = 0; i < MAX_REAL_PORTS; ++i)
    {
        outs[i].solved = outs[i].wanted.freq == 0;
        USDR_LOG("5318", USDR_LOG_DEBUG, "port:%d freq:%d (-%d, +%d) *%s*",
                 outs[i].port, outs[i].wanted.freq, outs[i].wanted.freq_delta_minus, outs[i].wanted.freq_delta_plus,
                 outs[i].solved ? "not used" : "active");
    }

    //first we try routing ports to APLL1
    //it's easy
    for(unsigned i = 0; i < MAX_REAL_PORTS; ++i)
    {
        lmk05318_out_config_t* out = outs + i;
        if(out->solved)
            continue;

        uint64_t odiv = 0;
        int res = lmk05318_get_output_divider(out, VCO_APLL1, &odiv);
        if(!res)
        {
            out->solved = true;
            out->result.out_div = odiv;
            out->result.freq = (double)VCO_APLL1 / odiv;
            out->result.mux = out->wanted.revert_phase ? OUT_PLL_SEL_APLL1_P1_INV : OUT_PLL_SEL_APLL1_P1;

            USDR_LOG("5318", USDR_LOG_ERROR, "port:%d solved via APLL1 [OD:%" PRIu64 " freq:%.2f mux:%d]",
                     out->port, out->result.out_div, out->result.freq, out->result.mux);
        }
        else
        {
            USDR_LOG("5318", USDR_LOG_WARNING, "port:%d cannot solve it via APLL1, will try APLL2", out->port);
        }

        //we cannot revert phase for ports NOT linked to APLL1
        if(!out->solved && out->wanted.revert_phase)
            return -EINVAL;
    }


    //second - try routing to APLL2
    //it's HELL
    unsigned cnt_to_solve = 0;
    USDR_LOG("5318", USDR_LOG_DEBUG,"Need to solve via APLL2:");
    for(unsigned i = 0; i < MAX_REAL_PORTS; ++i)
    {
        if(outs[i].solved)
            continue;

        ++cnt_to_solve;

        USDR_LOG("5318", USDR_LOG_DEBUG, "\tport:%d freq:%d (-%d, +%d)",
                 outs[i].port, outs[i].wanted.freq, outs[i].wanted.freq_delta_minus, outs[i].wanted.freq_delta_plus);
    }

    if(!cnt_to_solve)
        return 0;

    static const uint64_t fvco2_pd_min = VCO_APLL2_MIN / APLL2_PDIV_MAX;
    static const uint64_t fvco2_pd_max = VCO_APLL2_MAX / APLL2_PDIV_MIN;

    //try to determine valid PD ranges for our frequencies
    for(unsigned i = 0; i < MAX_REAL_PORTS; ++i)
    {
        lmk05318_out_config_t* out = outs + i;
        if(out->solved)
            continue;

        const range_t r = lmk05318_get_freq_range(out);
        const range_t ifreq = {MAX(r.min, fvco2_pd_min) , MIN(r.max * lmk05318_max_odiv(out->port), fvco2_pd_max)};

        if(ifreq.min > ifreq.max)
            return -EINVAL;

        const int pd_min = VCO_APLL2_MAX / ifreq.max;
        const int pd_max = VCO_APLL2_MAX / ifreq.min;

        out->pd_min = pd_min;
        out->pd_max = pd_max;

        USDR_LOG("5318", USDR_LOG_DEBUG, "port:%d pre-OD freq range:[%" PRIu64", %" PRIu64"], PD:[%d, %d]",
                 out->port, ifreq.min, ifreq.max, pd_min, pd_max);
    }

    int pd1_index = -1;
    bool found_sect = true;
    range_t section;

    unsigned pd2_ids[MAX_REAL_PORTS];
    unsigned num_pd2 = 0;

    //then we make an intersection of these PD ranges
    for(unsigned i = 0; i < MAX_REAL_PORTS; ++i)
    {
        lmk05318_out_config_t* out = outs + i;
        if(out->solved)
            continue;

        section.min =  0;
        section.max = -1;

        USDR_LOG("5318", USDR_LOG_DEBUG,"Assume PD1=%d [%d, %d]", out->port, out->pd_min, out->pd_max);

        unsigned j;
        found_sect = true;
        num_pd2 = 0;

        for(j = 0; j < MAX_REAL_PORTS; ++j)
        {
            if(i == j)
                continue;

            lmk05318_out_config_t* out2 = outs + j;
            if(out2->solved)
                continue;

            section.min = MAX(section.min, out2->pd_min);
            section.max = MIN(section.max, out2->pd_max);

            USDR_LOG("5318", USDR_LOG_DEBUG,"\t port %d [%d, %d] -> section:[%lu, %lu]",
                     out2->port, out2->pd_min, out2->pd_max, section.min, section.max);

            if(section.min > section.max)
            {
                found_sect = false;
                break;
            }

            pd2_ids[num_pd2++] = j;
        }

        if(found_sect)
        {
            pd1_index = i;
            break;
        }
    }

    //means we need more than 2 post-dividers for our setup
    if(!found_sect)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "No possible solutions for 2 PDs");
        return -EINVAL;
    }

    pd_array_t pd_group[2];
    unsigned pd_group_count = 0;

    //and fill the PD groups. the count of PD groups must be 1 or 2.
    if(num_pd2)
    {
        range_t r = {MAX(outs[pd1_index].pd_min, section.min), MIN(outs[pd1_index].pd_max, section.max)};
        if(r.max >= r.min)
        {
            pd_array_t* pd1 = &pd_group[pd_group_count++];
            pd1->pd_range = r;
            pd1->port_count = 0;
            pd1->port_ids[pd1->port_count++] = pd1_index;

            for(unsigned i = 0; i < num_pd2; ++i)
            {
                pd1->port_ids[pd1->port_count++] = pd2_ids[i];
            }
        }
        else
        {
            pd_array_t* pd1 = &pd_group[pd_group_count++];
            pd1->pd_range.min = outs[pd1_index].pd_min;
            pd1->pd_range.max = outs[pd1_index].pd_max;
            pd1->port_count = 0;
            pd1->port_ids[pd1->port_count++] = pd1_index;

            pd_array_t* pd2 = &pd_group[pd_group_count++];
            pd2->pd_range = section;
            pd2->port_count = 0;
            for(unsigned i = 0; i < num_pd2; ++i)
            {
                pd2->port_ids[pd2->port_count++] = pd2_ids[i];
            }
        }
    }
    else
    {
        pd_array_t* pd1 = &pd_group[pd_group_count++];
        pd1->pd_range.min = outs[pd1_index].pd_min;
        pd1->pd_range.max = outs[pd1_index].pd_max;
        pd1->port_count = 0;
        pd1->port_ids[pd1->port_count++] = pd1_index;
    }

    for(unsigned i = 0; i < pd_group_count; ++i)
    {
        pd_array_t* pd = pd_group + i;
        USDR_LOG("5318", USDR_LOG_DEBUG, "PD%d:[%lu, %lu]", i + 1, pd->pd_range.min, pd->pd_range.max);
        for(unsigned j = 0; j < pd->port_count; ++j)
            USDR_LOG("5318", USDR_LOG_DEBUG, "\t port:%d", outs[pd->port_ids[j]].port);
    }

    if(pd_group_count > 1)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "Solution via two PDs is not yet supported properly!");
    }


    bool all_done = false;
    uint64_t f_in;
    unsigned pd1 = 0;
    unsigned pd2 = 0;

    // Bisect search for PD1
    // Here is the Abyss
    pd_array_t* pPD1 = &pd_group[0];

    for(pd1 = pPD1->pd_range.min; !all_done && pd1 <= pPD1->pd_range.max; ++pd1)
    {
        uint64_t f_in_min = VCO_APLL2_MIN / pd1;
        uint64_t f_in_max = VCO_APLL2_MAX / pd1;

        USDR_LOG("5318", USDR_LOG_DEBUG, "***** PD:%d f_in:[%" PRIu64 ", %" PRIu64 "] *****", pd1, f_in_min, f_in_max);

        while (f_in_min <= f_in_max)
        {
            f_in = f_in_min + ((f_in_max - f_in_min) >> 1);
            USDR_LOG("5318", USDR_LOG_DEBUG, "BISECT f_mid:%" PRIu64 " fvco2:%" PRIu64 "", f_in, f_in * pd1);

            int direction_flag = 0;
            bool all_freqs_invalid = false;

            direction_flag = bisect_finder(outs, pd_group, pd_group_count, pd1, &pd2, f_in, &all_freqs_invalid);

            USDR_LOG("5318", USDR_LOG_DEBUG, "BISECT direction_flag:%d, %s", direction_flag,
                     all_freqs_invalid ? "invalid point!" : (direction_flag == 0 ? "all found" : (direction_flag > 0 ? "go left(down)" : "go right(up)")));

            uint64_t f = 0;
            if(all_freqs_invalid)
            {
                USDR_LOG("5318", USDR_LOG_DEBUG, "BISECT we're near, gonna do spiral search in [%" PRIu64 " <- %" PRIu64 " -> %" PRIu64 "]",
                         f_in_min, f_in, f_in_max);

                int cnt = 2;
                while(1)
                {
                    // +1 -1 +2 -2 +3 -3 etc
                    f = f_in + (cnt % 2 ? (-1) : (1)) * (cnt >> 1);

                    if(f > f_in_max || f < f_in_min)
                        break;

                    direction_flag = bisect_finder(outs, pd_group, pd_group_count, pd1, &pd2, f, &all_freqs_invalid);

                    if(!all_freqs_invalid)
                        break;

                    ++cnt;
                }
            }

            if(all_freqs_invalid)
                break; // go to the next PD

            //all solved
            if(direction_flag == 0)
            {
                f_in = f;
                all_done = true;
                break;
            }

            //too high, go to the left (down)
            if(direction_flag > 0)
            {
                f_in_max = f_in - 1;
            }

            //too low, go to the right (up)
            if(direction_flag < 0)
            {
                f_in_min = f_in + 1;
            }
        }
    }

    uint64_t fvco2 = f_in * (--pd1);


    if(all_done)
    {
        USDR_LOG("5318", USDR_LOG_WARNING, "=== SOLUTION via APLL2 FOUND @ VCO2:%" PRIu64 " and PD1:%d PD2:%d ===",
                fvco2, pd1, pd2);

        for(unsigned i = 0; i < MAX_REAL_PORTS; ++i)
        {
            lmk05318_out_config_t* out = outs + i;
            if(out->solved)
                continue;

            USDR_LOG("5318", USDR_LOG_WARNING, "port:%d solved via APLL2 [OD:%" PRIu64 " freq:%.2f mux:%d]",
                     out->port, out->result.out_div, out->result.freq, out->result.mux);

            out->solved = true;
            --cnt_to_solve;
        }
    }
    else
    {
        return -EINVAL;
    }

    //and the FINAL solution
    assert(cnt_to_solve == 0);

    qsort(outs, MAX_REAL_PORTS, sizeof(lmk05318_out_config_t), lmk05318_comp_port);
    qsort(_outs, n_outs, sizeof(lmk05318_out_config_t), lmk05318_comp_port);

    USDR_LOG("5318", USDR_LOG_ERROR, "=== COMPLETE SOLUTION @ VCO2:%" PRIu64 " and PD1:%d PD2:%d ===", fvco2, pd1, pd2);
    for(unsigned i = 0; i < n_outs; ++i)
    {
        lmk05318_out_config_t* out_dst = _outs + i;
        lmk05318_out_config_t* out_src = NULL;

        if(out_dst->port < 2)
            out_src = outs + 0;
        else if(out_dst->port < 4)
            out_src = outs + 1;
        else
            out_src = outs + (out_dst->port - 2);

        out_dst->solved = out_src->solved;
        out_dst->result = out_src->result;

        USDR_LOG("5318", USDR_LOG_ERROR, "port:%d solved [OD:%" PRIu64 " freq:%.2f mux:%d]",
                 out_dst->port, out_dst->result.out_div, out_dst->result.freq, out_dst->result.mux);
    }


    return 0;
}

