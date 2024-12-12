// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "lmk05318.h"
#include "lmk05318_rom.h"

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

int lmk05318_set_xo_fref(lmk05318_state_t* d, uint32_t xo_fref, enum xo_type_options xo_type,
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
                        uint32_t xo_fref, enum xo_type_options xo_type,
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

static unsigned max_odiv(unsigned port)
{
    switch(port)
    {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6: return 256;
    case 7: return 4 * 256;
    }
    return 1;
}

#define MAX(a, b) (a) > (b) ? (a) : (b)
#define MIN(a, b) (a) < (b) ? (a) : (b)

int lmk05318_solver(lmk05318_state_t* d, lmk05318_out_config_t* outs, unsigned n_outs)
{
    struct range
    {
        uint64_t min, max;
    };
    typedef struct range range_t;

    int res = 0;

    if(!outs || !n_outs || n_outs > MAX_OUT_PORTS)
        return -EINVAL;

    //validate dup ports
    {
        bool p[MAX_OUT_PORTS] = {0,0,0,0,0,0,0,0};
        for(unsigned i = 0; i < n_outs; ++i)
        {
            lmk05318_out_config_t* out = outs + i;
            if(out->port > MAX_OUT_PORTS - 1)
                return -EINVAL;
            if(p[out->port])
                return -EINVAL;
            p[out->port] = true;
        }
    }

    //validate port0/port1 & port2/port3 have equal freqs
    {
        range_t* port[4] = {NULL, NULL, NULL, NULL};
        for(unsigned i = 0; i < n_outs; ++i)
        {
            lmk05318_out_config_t* out = outs + i;
            const range_t r = {out->wanted.freq - out->wanted.freq_delta_minus, out->wanted.freq + out->wanted.freq_delta_plus};
            switch(out->port)
            {
            case 0:
            case 1:
            case 2:
            case 3:
                port[out->port] = malloc(sizeof(range_t)); *port[out->port] = r; break;
            }
        }

        res = (port[0] && port[1] && memcmp(port[0], port[1], 16)) || (port[2] && port[3] && memcmp(port[2], port[3], 16)) ?
            -EINVAL : 0;

        for(unsigned i = 0; i < 4; ++i) free(port[i]);
        if(res)
            return res;
    }

    //first we try routing ports to APLL1
    for(unsigned i = 0; i < n_outs; ++i)
    {
        lmk05318_out_config_t* out = outs + i;
        out->solved = false;

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

    //second - try routing to APLL2

    //check if all the requested freqs can be potentially solved
    range_t f_vco2[MAX_OUT_PORTS] = {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}};

    for(unsigned i = 0; i < n_outs; ++i)
    {
        lmk05318_out_config_t* out = outs + i;
        if(out->solved)
            continue;

        const range_t out_freq = {out->wanted.freq - out->wanted.freq_delta_minus, out->wanted.freq + out->wanted.freq_delta_plus};
        const range_t out_pre_div_freq = {out_freq.min * APLL2_PDIV_MIN, out_freq.max * max_odiv(out->port) * APLL2_PDIV_MAX};

        uint64_t eff_min = MAX(out_pre_div_freq.min, VCO_APLL2_MIN);
        uint64_t eff_max = MIN(out_pre_div_freq.max, VCO_APLL2_MAX);

        if(eff_min > eff_max)
            return -EINVAL;

        //narrow fvco2 band
        f_vco2[out->port].min = eff_min;
        f_vco2[out->port].max = eff_max;
    }

    //now we find the complete fvco2 intersection
    uint64_t f_vco2_min = VCO_APLL2_MIN;
    uint64_t f_vco2_max = VCO_APLL2_MAX;

    for(unsigned i = 0; i < n_outs; ++i)
    {
        if(!f_vco2[i].min && !f_vco2[i].min)
            continue;

        f_vco2_min = MAX(f_vco2_min, f_vco2[i].min);
        f_vco2_max = MIN(f_vco2_max, f_vco2[i].max);
    }

    if(f_vco2_min > f_vco2_max)
        return -EINVAL;

    //next we go to bisect, first with PD1, second - with PD2


    return res;
}

