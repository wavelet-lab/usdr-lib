// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <inttypes.h>

#include "lmk05318.h"
#include "def_lmk05318.h"
#include "lmk05318_rom.h"

#include <usdr_logging.h>

enum {
    VCO_APLL1 = 2500000000ull,
    VCO_APLL2_MIN = 5500000000ull,
    VCO_APLL2_MAX = 6250000000ull,

    APLL1_PD_MIN = 1000000,
    APLL1_PD_MAX = 50000000,

    APLL2_PD_MIN = 10000000,
    APLL2_PD_MAX = 150000000,

    OUT_FREQ_MAX = 800000000ull,
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

int lmk05318_create(lldev_t dev, unsigned subdev, unsigned lsaddr, unsigned int flags, lmk05318_state_t* out)
{
    int res;
    uint8_t dummy[4];

    const uint32_t* lmk_init = flags ? lmk05318_rom_49152_12288_384 : lmk05318_rom;
    unsigned lmk_init_sz = flags ? SIZEOF_ARRAY(lmk05318_rom_49152_12288_384) : SIZEOF_ARRAY(lmk05318_rom);

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
    res = lmk05318_reg_wr_n(out, lmk_init, lmk_init_sz);
    if (res)
        return res;

    // Reset
    uint32_t regs[] = {
        lmk05318_rom[0] | (1 << RESET_SW_OFF),
        lmk05318_rom[0] | (0 << RESET_SW_OFF),

        MAKE_LMK05318_XO_CONFIG(flags > 1 ? 1 : 0),

        MAKE_LMK05318_PLL1_CTRL0(0),
        MAKE_LMK05318_PLL1_CTRL0(1),
        MAKE_LMK05318_PLL1_CTRL0(0),

    };
    res = lmk05318_reg_wr_n(out, regs, SIZEOF_ARRAY(regs));
    if (res)
        return res;

    out->fref_pll2_div_rp = 3;
    out->fref_pll2_div_rs = (((VCO_APLL1 + APLL2_PD_MAX - 1) / APLL2_PD_MAX) + out->fref_pll2_div_rp - 1) / out->fref_pll2_div_rp;
    USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 initialized\n");
    return 0;
}


int lmk05318_tune_apll2(lmk05318_state_t* d, uint32_t freq, unsigned *last_div)
{
    const unsigned pre_div = 2;
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

    unsigned div = ((VCO_APLL2_MAX / pre_div)) / freq;
    uint64_t fvco = (uint64_t)freq * div * pre_div;
    unsigned n = fvco / fref;
    unsigned num = (fvco - n * (uint64_t)fref) * (1ull << 24) / fref;
    int res;

    USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 FREQ=%u FVCO=%lld N=%d NUM=%d DIV=%d\n", freq, (long long)fvco, n, num, div);

    uint32_t regs[] = {
        MAKE_LMK05318_PLL2_CTRL0(d->fref_pll2_div_rs - 1, d->fref_pll2_div_rp - 3, 1),
        MAKE_LMK05318_PLL2_CTRL2(pre_div - 1, pre_div - 1),
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


int lmk05318_set_out_div(lmk05318_state_t* d, unsigned port, unsigned udiv)
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

int lmk05318_check_lock(lmk05318_state_t* d, unsigned* los_msk)
{
    uint8_t los[3];
    int res = 0;
    unsigned losval;

    res = res ? res : lmk05318_reg_rd(d, INT_FLAG0, &los[0]);
    res = res ? res : lmk05318_reg_rd(d, INT_FLAG1, &los[1]);
    res = res ? res : lmk05318_reg_rd(d, BAW_LOCKDET_PPM_MAX_BY1, &los[2]);

    if (res)
        return res;

    losval = ((los[0] & LOS_XO_POL_MSK) ? LMK05318_LOS_XO : 0) |
             ((los[0] & LOL_PLL1_POL_MSK) ? LMK05318_LOL_PLL1 : 0) |
             ((los[0] & LOL_PLL2_POL_MSK) ? LMK05318_LOL_PLL2 : 0) |
             ((los[0] & LOS_FDET_XO_POL_MSK) ? LMK05318_LOS_FDET_XO : 0) |
             ((los[1] & LOPL_DPLL_POL_MSK) ? LMK05318_LOPL_DPLL : 0) |
             ((los[1] & LOFL_DPLL_POL_MSK) ? LMK05318_LOFL_DPLL : 0);


    USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 LOS_MAK=[%s%s%s%s%s%s%s] %02x:%02x:%02x\n",
            (los[0] & LOS_XO_POL_MSK) ? "XO" : "",
            (los[0] & LOL_PLL1_POL_MSK) ? " PLL1" : "",
            (los[0] & LOL_PLL2_POL_MSK) ? " PLL2" : "",
            (los[0] & LOS_FDET_XO_POL_MSK) ? " XO_FDET" : "",
            (los[1] & LOPL_DPLL_POL_MSK) ? " DPLL_P" : "",
            (los[1] & LOFL_DPLL_POL_MSK) ? " DPLL_F" : "",
            (los[2] & BAW_LOCK_MSK) ? "" : " BAW",
            los[0], los[1], los[2]);

    *los_msk = losval;
    return 0;
}
