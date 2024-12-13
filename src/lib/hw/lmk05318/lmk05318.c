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
#include "lmk05318_solver.h"

#include <usdr_logging.h>

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

int lmk05318_tune_apll1(lmk05318_state_t* d, uint32_t freq,
                        uint32_t xo_fref, int xo_type,
                        bool xo_doubler_enabled, bool xo_fdet_bypass, bool dpll_mode,
                        unsigned *last_div)
{
    if (freq < 1e6) {
        // Disable
        uint32_t regs[] = {
            MAKE_LMK05318_PLL1_CTRL0(1),
        };
        return lmk05318_reg_wr_n(d, regs, SIZEOF_ARRAY(regs));;
    }

    int res = lmk05318_set_xo_fref(d, xo_fref, xo_type, xo_doubler_enabled, xo_fdet_bypass);
    if(res)
        return res;

    unsigned fref = (d->xo.fref / APLL1_DIVIDER_MAX) * (d->xo.doubler_enabled ? 2 : 1);
    unsigned div = VCO_APLL1_MAX / freq;
    uint64_t fvco = (uint64_t)freq * div;
    unsigned n = fvco / fref;
    uint64_t num = (fvco - n * (uint64_t)fref) * (1ull << 40) / fref;

    USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 APLL1 FREQ=%u FVCO=%" PRIu64 " N=%d NUM=%" PRIu64 " DIV=%d\n", freq, fvco, n, num, div);

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

    *last_div = div;
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

//idx is NOT a port but OD index (0..5)
static unsigned max_odiv(unsigned idx)
{
    assert(idx < 6);

    switch(idx)
    {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4: return 256;
    case 5: return 4 * 256;
    }
    return 1;
}

/***************************************************************************************************************************/

static int comp_u64(const void * elem1, const void * elem2)
{
    const uint64_t* f = (uint64_t*)elem1;
    const uint64_t* s = (uint64_t*)elem2;

    if(*f < *s) return -1;
    if(*f > *s) return  1;
    return 0;
}

static int comp_intersection_desc(const void * elem1, const void * elem2)
{
    const intersection_t* f = (intersection_t*)elem1;
    const intersection_t* s = (intersection_t*)elem2;

    if(f->count < s->count) return  1;
    if(f->count > s->count) return -1;
    return 0;
}

int lmk05318_range_solver(const range_t* diaps, unsigned diaps_num, range_solution_t* solution)
{
    assert(diaps_num <= DIAP_MAX);
    char logtmp[2048];
    unsigned loglen;

    USDR_LOG("5318", USDR_LOG_DEBUG, "input ranges:->");
    for(unsigned i = 0; i < diaps_num; ++i)
    {
        USDR_LOG("5318", USDR_LOG_DEBUG, "%d -> %lu : %lu", i, diaps[i].min, diaps[i].max);
    }

    // get wrapper points (ends of our diaps)
    uint64_t wrapper_points[diaps_num * 2];
    for(unsigned i = 0; i < diaps_num * 2; i += 2)
    {
        wrapper_points[i + 0] = diaps[i >> 1].min;
        wrapper_points[i + 1] = diaps[i >> 1].max;
    }

    qsort(wrapper_points, diaps_num * 2, sizeof(uint64_t), comp_u64);


    USDR_LOG("5318", USDR_LOG_DEBUG, "wrap points:-> ");
    for(unsigned i = 0; i < diaps_num * 2; ++i)
    {
        USDR_LOG("5318", USDR_LOG_DEBUG, "%lu, ", wrapper_points[i]);
    }

    //and remove dups
    uint64_t uniq_wrapper_points[diaps_num * 2];
    unsigned uniq_wrapper_points_num = 0;
    for(unsigned i = 0; i < diaps_num * 2; ++i)
    {
        if(i == 0 ||  wrapper_points[i] != wrapper_points[i-1])
        {
            uniq_wrapper_points[uniq_wrapper_points_num++] = wrapper_points[i];
        }
    }

    loglen = snprintf(logtmp, sizeof(logtmp), "uniq wrap points:-> ");
    for(unsigned i = 0; i < uniq_wrapper_points_num; ++i)
    {
        loglen += snprintf(logtmp + loglen, sizeof(logtmp) - loglen, "%lu, ", uniq_wrapper_points[i]);
    }
    USDR_LOG("5318", USDR_LOG_DEBUG, "%s", logtmp);

    //build intersections within the intervals between wrapper points
    const unsigned intervals_count = uniq_wrapper_points_num - 1;
    intersection_t intersections[intervals_count];
    for(unsigned i = 0; i < intervals_count; ++i)
    {
        intersection_t* is = intersections + i;
        const uint64_t min = uniq_wrapper_points[i + 0];
        const uint64_t max = uniq_wrapper_points[i + 1];

        //initialize the insersection struct
        is->count = 0;
        memset(is->diap_idxs, 0, sizeof(is->diap_idxs));
        is->range.min = min;
        is->range.max = max;

        //here is our moving point - in the middle of the diapason
        const uint64_t point = (max + min) >> 1;

        //check how many diaps it intersects
        for(unsigned j = 0; j < diaps_num; ++j)
        {
            if(point >= diaps[j].min && point <= diaps[j].max)
            {
                is->diap_idxs[is->count++] = j;
            }
        }
    }

    //sort it desc to get the most massive intersection on the top
    qsort(intersections, intervals_count, sizeof(intersection_t), comp_intersection_desc);

    USDR_LOG("5318", USDR_LOG_DEBUG, "intersects:->");
    loglen = 0;
    for(unsigned i = 0; i < intervals_count; ++i)
    {
        intersection_t* is = intersections + i;
        loglen += snprintf(logtmp + loglen, sizeof(logtmp) - loglen, "diaps {");
        for(unsigned j = 0; j < is->count; ++j)
            loglen += snprintf(logtmp + loglen, sizeof(logtmp) - loglen, "%d,", is->diap_idxs[j]);
        loglen += snprintf(logtmp + loglen, sizeof(logtmp) - loglen,"} have %d intersections in range[%lu, %lu]", is->count, is->range.min, is->range.max);
        USDR_LOG("5318", USDR_LOG_DEBUG, "%s", logtmp);
    }

    //reduce to find a complete solution
    for(unsigned z = 0; z < intervals_count; ++z)
    {
        intersection_t* main_is = intersections + z;

        if(!main_is->count)
            break;

        //after sort assume the first intersect is the best, and exclude these diaps from the others
        unsigned excludes[diaps_num];
        unsigned excludes_count = main_is->count;
        memcpy(excludes, intersections + z, excludes_count * sizeof(unsigned));

        for(unsigned i = z + 1; i < intervals_count; ++i)
        {
            intersection_t* is = intersections + i;
            unsigned tmp_arr[diaps_num];
            unsigned tmp_n = 0;

            for(unsigned j = 0; j < is->count; ++j)
            {
                bool is_in = false;
                for(unsigned k = 0; k < excludes_count; ++k)
                {
                    if(excludes[k] == is->diap_idxs[j])
                    {
                        is_in = true;
                        break;
                    }
                }
                if(!is_in)
                    tmp_arr[tmp_n++] = is->diap_idxs[j];
            }

            //hint - if we going to collapse this intersection part, and we see it is == to the current
            if(!tmp_n && main_is->count == is->count)
            {
                //and the diap ranges of them are adjacent
                if(main_is->range.min == is->range.max || main_is->range.max == is->range.min)
                {
                    //merge them to one diapason
                    main_is->range.min = MIN(main_is->range.min, is->range.min);
                    main_is->range.max = MAX(main_is->range.max, is->range.max);
                }
            }

            is->count = tmp_n;
            memcpy(is->diap_idxs, tmp_arr, tmp_n * sizeof(unsigned));
        }

        //sort it again to proceed for the next loop
        qsort(intersections + z, intervals_count - z, sizeof(intersection_t), comp_intersection_desc);

        USDR_LOG("5318", USDR_LOG_DEBUG, "intersects(%d):->", z);
        loglen = 0;
        for(unsigned i = 0; i < intervals_count; ++i)
        {
            intersection_t* is = intersections + i;
            loglen += snprintf(logtmp + loglen, sizeof(logtmp) - loglen, "diaps {");
            for(unsigned j = 0; j < is->count; ++j)
                loglen += snprintf(logtmp + loglen, sizeof(logtmp) - loglen, "%d,", is->diap_idxs[j]);
            loglen += snprintf(logtmp + loglen, sizeof(logtmp) - loglen,"} have %d intersections in range[%lu, %lu]",
                               is->count, is->range.min, is->range.max);

            USDR_LOG("5318", USDR_LOG_DEBUG, "%s", logtmp);
        }
    }

    //we need only 2 effective diapasons due to only 2 PDs - PD1 and PD2
    static const unsigned MAX_RANGES_IN_SOLUTION = 2;

    solution->count = 0;
    unsigned total = 0;

    for(unsigned i = 0; i < MAX_RANGES_IN_SOLUTION; ++i)
    {
        intersection_t* is = intersections + i;
        solution->is[i] = *is;
        ++solution->count;

        total += is->count;
        if(total >= diaps_num)
            break;
    }

    assert(total <= diaps_num);

    if(total == diaps_num)
    {
        USDR_LOG("5318", USDR_LOG_DEBUG, "SOLUTION FOUND!->");
        loglen = 0;
        for(unsigned j = 0; j < solution->count; ++j)
        {
            intersection_t* is = solution->is + j;
            loglen += snprintf(logtmp + loglen, sizeof(logtmp) - loglen, "diaps {");
            for(unsigned k = 0; k < is->count; ++k)
                loglen += snprintf(logtmp + loglen, sizeof(logtmp) - loglen, "%d,", is->diap_idxs[k]);
            loglen += snprintf(logtmp + loglen, sizeof(logtmp) - loglen,"} have %d intersections in range[%lu, %lu]",
                               is->count, is->range.min, is->range.max);

            USDR_LOG("5318", USDR_LOG_DEBUG, "%s", logtmp);
        }
        return 0;
    }

    USDR_LOG("5318", USDR_LOG_DEBUG, "NO SOLUTION FOUND!");
    return -EINVAL;
}

/***************************************************************************************************************************/

static int lmk05318_check_solution(lmk05318_out_config_t* out, uint64_t fvco2, unsigned pdiv, uint64_t odiv)
{
    const range_t wanted = {out->wanted.freq - out->wanted.freq_delta_minus, out->wanted.freq + out->wanted.freq_delta_plus};
    const uint32_t freq = fvco2 / pdiv / odiv;

    if(freq < wanted.min)
        return -1;
    else if(freq > wanted.max)
        return 1;
    else
        return 0;
}

int lmk05318_solver(lmk05318_state_t* d, lmk05318_out_config_t* _outs, unsigned n_outs)
{
    int res = 0;

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

        *norm_out = *out;
        norm_out->solved = false;
    }

    //now outs[] contains effective ports ordered (0..5) config.
    //some elems may be not initialized (wanted.freq == 0) and should not be processed.
    for(unsigned i = 0; i < MAX_OUT_PORTS - 2; ++i)
        outs[i].solved = outs[i].wanted.freq == 0;

    //first we try routing ports to APLL1
    for(unsigned i = 0; i < MAX_OUT_PORTS - 2; ++i)
    {
        lmk05318_out_config_t* out = outs + i;
        const range_t out_freq = {out->wanted.freq - out->wanted.freq_delta_minus, out->wanted.freq + out->wanted.freq_delta_plus};
        const range_t out_pre_div_freq = {out_freq.min, out_freq.max * max_odiv(out->port)};

        if(VCO_APLL1 >= out_pre_div_freq.min && VCO_APLL1 <= out_pre_div_freq.max)
        {
            const uint64_t div = (uint64_t)VCO_APLL1 / out->wanted.freq;
            const uint32_t freq = VCO_APLL1 / div;
            if(freq >= out_freq.min && freq <= out_freq.max)
            {
                out->solved = true;
                out->result.out_div = div;
                out->result.freq    = freq;
                out->result.mux = out->wanted.revert_phase ? OUT_PLL_SEL_APLL1_P1_INV : OUT_PLL_SEL_APLL1_P1;
            }
        }

        //we cannot revert phase for ports NOT linked to APLL1
        if(!out->solved && out->wanted.revert_phase)
            return -EINVAL;
    }

    /*
        Now we have up to 6 equations with 6 + 1 + 1 = 8 vars (6 * OD + PD + Fvco2)
        The base equation: Fout_i = Fvco2 / PD1_2 / ODi

        We'll have 2 large iterations - one for PD1 and second for PD2
        So 1) we find out all the possible solutions for PD1(/2../7) and Fvco2(VCO_APLL2_MIN..VCO_APLL2_MAX)
              if we have the whole solution -> return OK
              if we have NO new solutions -> abort, because PD1 and PD2 have equal params, and it's quite useless to continue
              if we have a partial solution -> commit Fvco2 and go to PD2
           2) we find out all the possible solutions for PD2(/2../7). Fvco2 is fixed.
              if we solved all the outs -> return ok
              abort otherwise
    */


    uint64_t fvco2_max = VCO_APLL2_MAX;
    uint64_t fvco2_min = VCO_APLL2_MIN;
    uint64_t fvco2 = VCO_APLL2_MAX;
    unsigned pdiv = APLL2_PDIV_MAX;

    for(unsigned i = 0; i < MAX_OUT_PORTS - 2; ++i)
    {
        if(outs[i].solved)
            continue;

        int res = lmk05318_check_solution(&outs[i], fvco2, pdiv, max_odiv(i));
        if(res > 0)
        {
            fvco2 = (fvco2_max - fvco2_min) / 2;
        }
    }

    //second - try routing to APLL2

    //check if all the requested freqs can be potentially solved
    //and narrow up Fvco2 effective range
    uint64_t f_vco2_min = VCO_APLL2_MIN;
    uint64_t f_vco2_max = VCO_APLL2_MAX;

    for(unsigned i = 0; i < n_outs; ++i)
    {
        lmk05318_out_config_t* out = outs + i;
        if(out->solved)
            continue;

        const range_t out_freq = {out->wanted.freq - out->wanted.freq_delta_minus, out->wanted.freq + out->wanted.freq_delta_plus};
        const range_t out_pre_div_freq = {out_freq.min * APLL2_PDIV_MIN, out_freq.max * max_odiv(out->port) * APLL2_PDIV_MAX};

        f_vco2_min = MAX(out_pre_div_freq.min, f_vco2_min);
        f_vco2_max = MIN(out_pre_div_freq.max, f_vco2_max);

        if(f_vco2_min > f_vco2_max)
            return -EINVAL;
    }

    uint64_t fvco = f_vco2_min;
    unsigned pdiv1 = APLL2_PDIV_MIN;

    for(; fvco <= f_vco2_max; ++fvco)
    {
        int res[MAX_OUT_PORTS - 2];

        for(unsigned i = 0; i < MAX_OUT_PORTS - 2; ++i)
        {
            lmk05318_out_config_t* out = outs + i;
            if(out->solved)
                continue;

            unsigned odiv = 1;
            for(; odiv <= max_odiv(i); ++odiv)
            {
                res[i] = lmk05318_check_solution(&outs[i], fvco, pdiv1, odiv);
                if(res[i] <= 0) //fvco2 matches or too low
                    break;
            }

            bool need_increase_pd1 = res[i] > 0; //fvco2 too high
        }



    }


    return res;
}

