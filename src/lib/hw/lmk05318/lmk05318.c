// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "def_lmk05318.h"
#include "lmk05318.h"
#include "lmk05318_rom.h"
#include "lmk05318_solver.h"

#include <usdr_logging.h>
#include "../../xdsp/attribute_switch.h"
#include "../cal/opt_func.h"

//#define LMK05318_SOLVER_DEBUG

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
    APLL2_PDIV_COUNT = 2, //PD1 & PD2

    F_TDC_MIN = 1,
    F_TDC_MAX = 26000000,

    DPLL_REF_R_DIV_MIN = 1,
    DPLL_REF_R_DIV_MAX = UINT16_MAX - 1,

    DPLL_PRE_DIV_MIN = 2,
    DPLL_PRE_DIV_MAX = 17,
};

int lmk05318_dpll_config(lmk05318_state_t* d, lmk05318_dpll_settings_t* dpll)
{
    if(!dpll->enabled)
    {
        d->dpll.enabled = false;
        USDR_LOG("5318", USDR_LOG_INFO, "[DPLL] DPLL disabled");
        return 0;
    }

    //Validate REF inputs
    for(unsigned i = LMK05318_PRIREF; i <= LMK05318_SECREF; ++i)
    {
        if(!dpll->en[i])
            continue;

        switch(dpll->dc_mode[i])
        {
        case DPLL_REF_AC_COUPLED_INT: dpll->dc_mode[i] = REF_DC_MODE_AC_COUPLED_INT; break;
        case DPLL_REF_DC_COUPLED_INT: dpll->dc_mode[i] = REF_DC_MODE_DC_COUPLED_INT; break;
        default:
            USDR_LOG("5318", USDR_LOG_ERROR, "[DPLL] %sREF DC_MODE:%u unsupported",
                     (i == LMK05318_PRIREF) ? "PRI" : "SEC", dpll->dc_mode[i]);
            return -EINVAL;
        }

        switch(dpll->buf_mode[i])
        {
        case DPLL_REF_AC_BUF_HYST50_DC_EN: dpll->buf_mode[i] = REF_BUF_MODE_AC_HYST50_DC_EN; break;
        case DPLL_REF_AC_BUF_HYST200_DC_DIS: dpll->buf_mode[i] = REF_BUF_MODE_AC_HYST200_DC_DIS; break;
        default:
            USDR_LOG("5318", USDR_LOG_ERROR, "[DPLL] %sREF BUF_MODE:%u unsupported",
                     (i == LMK05318_PRIREF) ? "PRI" : "SEC", dpll->buf_mode[i]);
            return -EINVAL;
        }

        switch(dpll->type[i])
        {
        case DPLL_REF_TYPE_DIFF_NOTERM:  dpll->type[i] = REF_INPUT_TYPE_DIFF_NOTERM; break;
        case DPLL_REF_TYPE_DIFF_100:     dpll->type[i] = REF_INPUT_TYPE_DIFF_100; break;
        case DPLL_REF_TYPE_DIFF_50:      dpll->type[i] = REF_INPUT_TYPE_DIFF_50; break;
        case DPLL_REF_TYPE_SE_NOTERM:    dpll->type[i] = REF_INPUT_TYPE_SE_NOTERM; break;
        case DPLL_REF_TYPE_SE_50:        dpll->type[i] = REF_INPUT_TYPE_SE_50; break;
        default:
            USDR_LOG("5318", USDR_LOG_ERROR, "[DPLL] %sREF TYPE:%u unsupported",
                     (i == LMK05318_PRIREF) ? "PRI" : "SEC", dpll->type[i]);
            return -EINVAL;
        }
    }

    d->dpll.enabled = true;

    if(dpll->en[LMK05318_PRIREF] && dpll->en[LMK05318_SECREF])
    {
        unsigned max_ref_id, min_ref_id;
        if(dpll->fref[LMK05318_PRIREF] > dpll->fref[LMK05318_SECREF])
        {
            max_ref_id = LMK05318_PRIREF; min_ref_id = LMK05318_SECREF;
        } else
        {
            max_ref_id = LMK05318_SECREF; min_ref_id = LMK05318_PRIREF;
        }

        uint64_t max_div = dpll->fref[max_ref_id];
        uint64_t min_div = dpll->fref[min_ref_id];
        uint64_t gcd = find_gcd(min_div, max_div);
        if(gcd > 1)
        {
            min_div /= gcd;
            max_div /= gcd;
        }

        const unsigned min_div_required = ceil((double)dpll->fref[max_ref_id] / F_TDC_MAX);

        if(max_div > DPLL_REF_R_DIV_MAX || min_div < min_div_required)
        {
            USDR_LOG("5318", USDR_LOG_ERROR, "[DPLL] incorrect PRIREF/SECREF ratio (%.2f)", (double)min_div / max_div);
            return -EINVAL;
        }

        d->dpll.ref_en[LMK05318_PRIREF] = d->dpll.ref_en[LMK05318_SECREF] = true;
        d->dpll.rdiv[min_ref_id] = min_div;
        d->dpll.rdiv[max_ref_id] = max_div;
        d->dpll.ftdc = (double)dpll->fref[min_ref_id] / d->dpll.rdiv[min_ref_id];

        const double ftdc2 = (double)dpll->fref[max_ref_id] / d->dpll.rdiv[max_ref_id];
        if(fabs(ftdc2 - d->dpll.ftdc) > 1E-6)
        {
            USDR_LOG("5318", USDR_LOG_ERROR, "[DPLL] PRIREF/SECREF cannot be resolved to one TDC (%.6f != %.6f)", d->dpll.ftdc, ftdc2);
        }
    }
    else if(dpll->en[LMK05318_PRIREF] || dpll->en[LMK05318_SECREF])
    {
        uint8_t id = dpll->en[LMK05318_PRIREF] ? LMK05318_PRIREF : LMK05318_SECREF;
        uint64_t div = ceil((double)dpll->fref[id] / F_TDC_MAX);
        if(div > DPLL_REF_R_DIV_MAX)
        {
            USDR_LOG("5318", USDR_LOG_ERROR, "[DPLL] PRIREF or SECREF value too high");
            return -EINVAL;
        }

        d->dpll.ref_en[id] = true;
        d->dpll.rdiv[id] = div;
        d->dpll.ftdc = (double)dpll->fref[id] / div;
    }
    else
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "[DPLL] PRIREF and SECREF are disabled, cannot configure DPLL");
        return -EINVAL;
    }

    USDR_LOG("5318", USDR_LOG_DEBUG, "[DPLL] PRIREF:%" PRIu64 " EN:%u RDIV:%u",
             dpll->fref[LMK05318_PRIREF], d->dpll.ref_en[LMK05318_PRIREF], d->dpll.rdiv[LMK05318_PRIREF]);
    USDR_LOG("5318", USDR_LOG_DEBUG, "[DPLL] SECREF:%" PRIu64 " EN:%u RDIV:%u",
             dpll->fref[LMK05318_SECREF], d->dpll.ref_en[LMK05318_SECREF], d->dpll.rdiv[LMK05318_SECREF]);
    USDR_LOG("5318", USDR_LOG_DEBUG, "[DPLL] FTDC:%.8f", d->dpll.ftdc);


    const uint64_t max_fbdiv = ((uint64_t)1 << 30) - 1;
    const uint64_t min_fbdiv = 1;

    unsigned max_pre_div = MIN(VCO_APLL1 / d->dpll.ftdc / 2 / min_fbdiv, DPLL_PRE_DIV_MAX);
    unsigned min_pre_div = MAX(VCO_APLL1 / d->dpll.ftdc / 2 / max_fbdiv, DPLL_PRE_DIV_MIN);
    if(max_pre_div < min_pre_div)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "[DPLL] cannot calculate PRE_DIV");
        return -EINVAL;
    }

    const unsigned pre_div = max_pre_div;
    double fbdiv = (double)VCO_APLL1 / d->dpll.ftdc / 2.0 / pre_div;
    USDR_LOG("5318", USDR_LOG_DEBUG, "[DPLL] PRE_DIV:%u FB_DIV:%.8f", pre_div, fbdiv);
    if(fbdiv < min_fbdiv || fbdiv > max_fbdiv)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "[DPLL] FB_DIV:%.8f out of range", fbdiv);
        return -EINVAL;
    }

    uint32_t fb_int = (uint32_t)fbdiv;
    double fb_frac = fbdiv - fb_int;
    uint64_t fb_den = VCO_APLL1 * 2 * pre_div; //(uint64_t)1 << 40;
    uint64_t fb_num = (uint64_t)(fb_frac * fb_den + 0.5);
    uint64_t gcd = find_gcd(fb_num, fb_den);
    if(gcd > 1)
    {
        fb_num /= gcd;
        fb_den /= gcd;
    }

    //check
    const double vco1_fact = d->dpll.ftdc * 2.0 * pre_div * (fb_int + (double)fb_num / fb_den);
    const double delta = fabs((double)VCO_APLL1 - vco1_fact);
    USDR_LOG("5318", USDR_LOG_DEBUG, "[DPLL] N:%u NUM:%" PRIu64 " DEN:%" PRIu64 " VCO1_FACT:%.8f DELTA:%.8fHz",
             fb_int, fb_num, fb_den, vco1_fact, delta);
    if(delta > 1E-6)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "[DPLL] VCO1_FACT too rough");
        return -EINVAL;
    }

    d->dpll.pre_div = pre_div;
    d->dpll.n = fb_int;
    d->dpll.num = fb_num;
    d->dpll.den = fb_den;

    //DPLL BW
    if((dpll->en[LMK05318_PRIREF] && dpll->fref[LMK05318_PRIREF] == 1) || (dpll->en[LMK05318_SECREF] && dpll->fref[LMK05318_SECREF] == 1))
    {
        d->dpll.lbw = 0.01;
    }
    else
        d->dpll.lbw = 100;

    USDR_LOG("5318", USDR_LOG_DEBUG, "[DPLL] LBW:%.2fHz", d->dpll.lbw);

    return 0;
}


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


enum
{
    LMK05318_REGADDR_MIN = 0x0000,
    LMK05318_REGADDR_MAX = 0x019B,
};

static uint32_t registers_map[LMK05318_REGADDR_MAX - LMK05318_REGADDR_MIN + 1];

void lmk05318_registers_map_reset()
{
    memset(registers_map, 0xFF, sizeof(registers_map));
}

static int lmk05318_add_reg_to_map(lmk05318_state_t* d, const uint32_t* regs, unsigned count)
{
    for (unsigned j = 0; j < count; j++)
    {
        const uint16_t regaddr = (uint16_t)(regs[j] >> 8) & ~0x8000;
        if(regaddr < LMK05318_REGADDR_MIN || regaddr > LMK05318_REGADDR_MAX)
        {
            USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 REGADDR 0x%04x out of range", regaddr);
            return -EINVAL;
        }

        const unsigned idx = regaddr - LMK05318_REGADDR_MIN;
        uint32_t* ptr = registers_map + idx;
        if(*ptr != (uint32_t)(-1) && (uint8_t)(*ptr) != (uint8_t)regs[j])
        {
            USDR_LOG("5318", USDR_LOG_WARNING, "LMK05318 Rewriting REGADDR 0x%04x : 0x%02x -> 0x%02x", regaddr, (uint8_t)(*ptr), (uint8_t)regs[j]);
        }
        *ptr = regs[j];
    }
    return 0;
}

int lmk05318_reg_wr_from_map(lmk05318_state_t* d, bool dry_run)
{
    for(unsigned j = 0; j < SIZEOF_ARRAY(registers_map); ++j)
    {
        if(registers_map[j] == (uint32_t)(-1))
            continue;

        uint16_t addr = registers_map[j] >> 8;
        uint8_t  data = registers_map[j];

        USDR_LOG("5318", USDR_LOG_DEBUG, "LMK05318 Writing register R%03u: 0x%04x = 0x%02x [0x%06x]", j, addr, data, registers_map[j]);

        int res = dry_run ? 0 : lmk05318_reg_wr(d, addr, data);
        if (res)
            return res;
    }

    lmk05318_registers_map_reset();
    return 0;
}




int lmk05318_softreset(lmk05318_state_t* out)
{
    uint8_t reg_ctrl;
    const uint8_t mask = ((uint8_t)1 << RESET_SW_OFF);

    int res = lmk05318_reg_rd(out, DEV_CTL, &reg_ctrl);
    if(res)
        return res;

    uint32_t regs[] =
    {
        MAKE_LMK05318_REG_WR(DEV_CTL, reg_ctrl |  mask),
        MAKE_LMK05318_REG_WR(DEV_CTL, reg_ctrl & ~mask),
    };

    return lmk05318_reg_wr_n(out, regs, SIZEOF_ARRAY(regs));;
}

int lmk05318_sync(lmk05318_state_t* out)
{
    uint8_t reg_ctrl;
    const uint8_t mask = ((uint8_t)1 << SYNC_SW_OFF);

    int res = lmk05318_reg_rd(out, DEV_CTL, &reg_ctrl);
    if(res)
        return res;

    uint32_t regs[] =
        {
            MAKE_LMK05318_REG_WR(DEV_CTL, reg_ctrl |  mask),
            MAKE_LMK05318_REG_WR(DEV_CTL, reg_ctrl & ~mask),
        };

    return lmk05318_reg_wr_n(out, regs, SIZEOF_ARRAY(regs));;
}

int lmk05318_mute(lmk05318_state_t* out, uint8_t chmask)
{
    for(unsigned ch = 0; ch < 8; ++ch)
    {
        bool muted = ((chmask >> ch) & 0x1) == 0x1;
        if(muted)
        {
            USDR_LOG("5318", USDR_LOG_WARNING, "LMK05318 OUT CH%u is MUTED", ch);
        }
    }

    uint32_t reg = MAKE_LMK05318_REG_WR(OUT_MUTE, chmask);
    return lmk05318_reg_wr_n(out, &reg, 1);
}

int lmk05318_reset_los_flags(lmk05318_state_t* d)
{
    uint32_t regs[] =
        {
            MAKE_LMK05318_INT_FLAG0(0,0,0,0),                //R19
            MAKE_LMK05318_INT_FLAG1(0,0,0,0,0,0,0,0),        //R20
        };

    return lmk05318_reg_wr_n(d, regs, SIZEOF_ARRAY(regs));
}

static int lmk05318_init(lmk05318_state_t* d, lmk05318_dpll_settings_t* dpll, bool zdm)
{
    int res = lmk05318_dpll_config(d, dpll);
    if(res)
        return res;

    if(d->dpll.enabled == false)
    {
        uint32_t dpll_regs[] =
        {
            MAKE_LMK05318_DEV_CTL(0, 0, 1, 1, 1, 1, 1),    //R12   set APLL1 mode - DPLL
            MAKE_LMK05318_SPARE_NVMBASE2_BY2(0, 1),        //R39   set fixed APLL1 denumerator for DPLL en, programmed den otherwise
            MAKE_LMK05318_SPARE_NVMBASE2_BY1(0, 0, 1),     //R40   set programmed APPL2 denumerator always
            MAKE_LMK05318_DPLL_GEN_CTL(0, 0, 0, 0, 0, 0, 0),  //R252   disable DPLL
            MAKE_LMK05318_PLL1_CALCTRL0(1, 0, 1),          //R79 BAW_LOCKDET_EN=1 PLL1_VCOWAIT=1
            MAKE_LMK05318_BAW_LOCKDET_PPM_MAX_BY1(1, 0),   //R80 BAW_LOCK=1
        };
        res = lmk05318_add_reg_to_map(d, dpll_regs, SIZEOF_ARRAY(dpll_regs));
    }
    else
    {
        const bool one_pps[] =
        {
            (dpll->en[LMK05318_PRIREF] && dpll->fref[LMK05318_PRIREF] == 1),
            (dpll->en[LMK05318_SECREF] && dpll->fref[LMK05318_SECREF] == 1),
        };

        const bool lt2k[] =
        {
            !one_pps[LMK05318_PRIREF] && (dpll->en[LMK05318_PRIREF] && dpll->fref[LMK05318_PRIREF] < 2000),
            !one_pps[LMK05318_SECREF] && (dpll->en[LMK05318_SECREF] && dpll->fref[LMK05318_SECREF] < 2000),
        };

        const bool ge2k[] =
        {
            !one_pps[LMK05318_PRIREF] && !lt2k[LMK05318_PRIREF],
            !one_pps[LMK05318_SECREF] && !lt2k[LMK05318_SECREF],
        };

        unsigned meas_time[] =
        {
            (unsigned)(log2f(10000.f / dpll->fref[LMK05318_PRIREF]) + 2.5),
            (unsigned)(log2f(10000.f / dpll->fref[LMK05318_SECREF]) + 2.5),
        };

        uint32_t dpll_regs[] =
        {
            MAKE_LMK05318_DEV_CTL(0, 0, 0, 1, 1, 1, 1),   //R12   set APLL1 mode - Free-run
            MAKE_LMK05318_SPARE_NVMBASE2_BY2(0, 0),       //R39   set fixed APLL1 denumerator for DPLL en, programmed den otherwise
            MAKE_LMK05318_SPARE_NVMBASE2_BY1(dpll->dc_mode[LMK05318_SECREF], dpll->dc_mode[LMK05318_PRIREF], 1), //R40   set programmed APPL2 denumerator always
            MAKE_LMK05318_REF_CLKCTL1(!dpll->en[LMK05318_SECREF] || (dpll->fref[LMK05318_SECREF] >= 5000000 && dpll->type[LMK05318_SECREF] != IN_OPTS_CMOS && dpll->type[LMK05318_SECREF] != IN_OPTS_SE_INT_50) ? 0 : 1,
                                      !dpll->en[LMK05318_PRIREF] || (dpll->fref[LMK05318_PRIREF] >= 5000000 && dpll->type[LMK05318_PRIREF] != IN_OPTS_CMOS && dpll->type[LMK05318_PRIREF] != IN_OPTS_SE_INT_50) ? 0 : 1,
                                      dpll->en[LMK05318_SECREF] ? dpll->buf_mode[LMK05318_SECREF] : DPLL_REF_AC_BUF_HYST200_DC_DIS,
                                      dpll->en[LMK05318_PRIREF] ? dpll->buf_mode[LMK05318_PRIREF] : DPLL_REF_AC_BUF_HYST200_DC_DIS),   //R45
            MAKE_LMK05318_REF_CLKCTL2(dpll->type[LMK05318_SECREF], dpll->type[LMK05318_PRIREF]),                 //R46

            MAKE_LMK05318_PLL1_CALCTRL0(0, 0, 1),                        //R79 BAW_LOCKDET_EN=0 PLL1_VCOWAIT=1
            MAKE_LMK05318_BAW_LOCKDET_PPM_MAX_BY1(0, 0),                 //R80 BAW_LOCK=0

            dpll->en[LMK05318_PRIREF] ?
            MAKE_LMK05318_REF0_DETEN(ge2k[LMK05318_PRIREF],
                                     lt2k[LMK05318_PRIREF] || one_pps[LMK05318_PRIREF],
                                     1, //validation timer en
                                     ge2k[LMK05318_PRIREF],
                                     ge2k[LMK05318_PRIREF],
                                     1 /* amp_det_en ge2k[LMK05318_PRIREF]*/) :
            MAKE_LMK05318_REF0_DETEN(0,0,0,0,0,0)
            , //R193

            dpll->en[LMK05318_SECREF] ?
            MAKE_LMK05318_REF1_DETEN(ge2k[LMK05318_SECREF],
                                     lt2k[LMK05318_SECREF] || one_pps[LMK05318_SECREF],
                                     1, //validation timer en,
                                     ge2k[LMK05318_SECREF],
                                     ge2k[LMK05318_SECREF],
                                     1 /* amp_det_en ge2k[LMK05318_SECREF]*/) :
            MAKE_LMK05318_REF1_DETEN(0,0,0,0,0,0)
            , //R194

            MAKE_LMK05318_REG_WR(REF0_VLDTMR, meas_time[LMK05318_PRIREF] & 0b00011111), //R233
            MAKE_LMK05318_REG_WR(REF1_VLDTMR, meas_time[LMK05318_SECREF] & 0b00011111), //R234

            MAKE_LMK05318_REF0_PH_VALID_THR(lt2k[LMK05318_PRIREF] || one_pps[LMK05318_PRIREF] ? 63 : 0), //R243 *********TODO
            MAKE_LMK05318_REF1_PH_VALID_THR(lt2k[LMK05318_SECREF] || one_pps[LMK05318_SECREF] ? 63 : 0), //R244 *********TODO

            MAKE_LMK05318_DPLL_REF01_PRTY(2, 1),                    //R249 set PRIREF 1st, SECREF 2nd priority
            MAKE_LMK05318_DPLL_REF_SWMODE(0,
                                          (d->dpll.ref_en[LMK05318_PRIREF] ? LMK05318_PRIREF : LMK05318_SECREF),
                                          (d->dpll.ref_en[LMK05318_PRIREF] && d->dpll.ref_en[LMK05318_SECREF]) ? 0x0 : 0x3), //R251
            MAKE_LMK05318_DPLL_GEN_CTL(zdm ? 1 : 0, 0, 1/*DPLL_SWITCHOVER_ALWAYS*/, !one_pps[LMK05318_PRIREF] && !one_pps[LMK05318_SECREF], 1, 0, 1), //R252  enable ZDM & enable DPLL
            MAKE_LMK05318_DPLL_REF0_RDIV_BY0(d->dpll.rdiv[LMK05318_PRIREF]), //R256
            MAKE_LMK05318_DPLL_REF0_RDIV_BY1(d->dpll.rdiv[LMK05318_PRIREF]),
            MAKE_LMK05318_DPLL_REF1_RDIV_BY0(d->dpll.rdiv[LMK05318_SECREF]),
            MAKE_LMK05318_DPLL_REF1_RDIV_BY1(d->dpll.rdiv[LMK05318_SECREF]), //R259
            MAKE_LMK05318_DPLL_REF_FB_PREDIV(d->dpll.pre_div - 2), //R304
            MAKE_LMK05318_DPLL_REF_FB_DIV_BY0(d->dpll.n), //R305
            MAKE_LMK05318_DPLL_REF_FB_DIV_BY1(d->dpll.n),
            MAKE_LMK05318_DPLL_REF_FB_DIV_BY2(d->dpll.n),
            MAKE_LMK05318_DPLL_REF_FB_DIV_BY3(d->dpll.n), //R308
            MAKE_LMK05318_DPLL_REF_NUM_BY0(d->dpll.num),  //R309
            MAKE_LMK05318_DPLL_REF_NUM_BY1(d->dpll.num),
            MAKE_LMK05318_DPLL_REF_NUM_BY2(d->dpll.num),
            MAKE_LMK05318_DPLL_REF_NUM_BY3(d->dpll.num),
            MAKE_LMK05318_DPLL_REF_NUM_BY4(d->dpll.num),  //R313
            MAKE_LMK05318_DPLL_REF_DEN_BY0(d->dpll.den),  //R314
            MAKE_LMK05318_DPLL_REF_DEN_BY1(d->dpll.den),
            MAKE_LMK05318_DPLL_REF_DEN_BY2(d->dpll.den),
            MAKE_LMK05318_DPLL_REF_DEN_BY3(d->dpll.den),
            MAKE_LMK05318_DPLL_REF_DEN_BY4(d->dpll.den),  //R318

            0x00C300, // PRIREF Missing Clock Detection
            0x00C400, // PRIREF Missing Clock Detection
            0x00C51D, // PRIREF Missing Clock Detection
            // 0x00C600, // SECREF Missing Clock Detection
            // 0x00C700, // SECREF Missing Clock Detection
            // 0x00C800, // SECREF Missing Clock Detection
            0x00C900, // PRI/SECREF Window Detection
            0x00CA00, // PRIREF Early Clock Detection
            0x00CB00, // PRIREF Early Clock Detection
            0x00CC15, // PRIREF Early Clock Detection
            // 0x00CD00, // SECREF Early Clock Detection
            // 0x00CE00, // SECREF Early Clock Detection
            // 0x00CF00, // SECREF Early Clock Detection
            0x00D000, // PRIREF Frequency Detection
            0x00D100, // PRIREF Frequency Detection
            0x00D200, // PRIREF Frequency Detection
            0x00D300, // PRIREF Frequency Detection
            // 0x00D400, // SECREF Frequency Detection
            // 0x00D500, // SECREF Frequency Detection
            // 0x00D600, // SECREF Frequency Detection
            // 0x00D700, // SECREF Frequency Detection
            0x00D900, // PRIREF Frequency Detection
            0x00DA00, // PRIREF Frequency Detection
            0x00DB00, // PRIREF Frequency Detection
            0x00DC00, // PRIREF Frequency Detection
            0x00DD00, // PRIREF Frequency Detection
            0x00DE00, // PRIREF Frequency Detection
            0x00DF00, // PRIREF Frequency Detection
            0x00E000, // PRIREF Frequency Detection
            // 0x00E100, // SECREF Frequency Detection
            // 0x00E200, // SECREF Frequency Detection
            // 0x00E300, // SECREF Frequency Detection
            // 0x00E400, // SECREF Frequency Detection
            // 0x00E500, // SECREF Frequency Detection
            // 0x00E600, // SECREF Frequency Detection
            // 0x00E700, // SECREF Frequency Detection
            // 0x00E800, // SECREF Frequency Detection
        };
        res = lmk05318_add_reg_to_map(d, dpll_regs, SIZEOF_ARRAY(dpll_regs));
    }

    if(res)
        return res;

    uint32_t regs[] =
    {
        MAKE_LMK05318_PLL_CLK_CFG(0, 0b111),                         //R47   APLL cascade mode + set PLL clock cfg
        MAKE_LMK05318_OUTSYNCCTL(1, 1, 1),                           //R70   enable APLL1/APLL2 channel sync
        MAKE_LMK05318_OUTSYNCEN(1, 1, 1, 1, 1, 1),                   //R71   enable ch0..ch7 out sync

        MAKE_LMK05318_MUTELVL1(CH3_MUTE_LVL_DIFF_LOW_P_LOW_N_LOW,
                               CH2_MUTE_LVL_DIFF_LOW_P_LOW_N_LOW,
                               CH1_MUTE_LVL_DIFF_LOW_P_LOW_N_LOW,
                               CH0_MUTE_LVL_DIFF_LOW_P_LOW_N_LOW),   //R23   set ch0..3 mute levels
        MAKE_LMK05318_MUTELVL2(CH7_MUTE_LVL_DIFF_LOW_P_LOW_N_LOW,
                               CH6_MUTE_LVL_DIFF_LOW_P_LOW_N_LOW,
                               CH5_MUTE_LVL_DIFF_LOW_P_LOW_N_LOW,
                               CH4_MUTE_LVL_DIFF_LOW_P_LOW_N_LOW),   //R24   set ch4..7 mute levels

        MAKE_LMK05318_INT_FLAG0(0,0,0,0),                            //R19   |
        MAKE_LMK05318_INT_FLAG1(0,0,0,0,0,0,0,0),                    //R20   | reset interrupt LOS flags

        0x00510A,   //R81
        0x005200,   //      |
        0x00530E,   //      |
        0x0054A6,   //      |
        0x005500,   //      |
        0x005600,   //      |
        0x00571E,   //      |
        0x005884,   //      |
        0x005980,   //      | BAW lock&unlock detection
        0x005A00,   //      |
        0x005B14,   //      |
        0x005C00,   //      |
        0x005D0E,   //      |
        0x005EA6,   //      |
        0x005F00,   //      |
        0x006000,   //      |
        0x00611E,   //      |
        0x006284,   //      |
        0x006380,   //R99

        MAKE_LMK05318_PLL1_MASHCTRL(0,0,0,0,3), //R115 PLL1 MASHORD=3
    };

    return lmk05318_add_reg_to_map(d, regs, SIZEOF_ARRAY(regs));
}


int lmk05318_create_ex(lldev_t dev, unsigned subdev, unsigned lsaddr,
                       const lmk05318_xo_settings_t* xo, lmk05318_dpll_settings_t* dpll,
                       lmk05318_out_config_t* out_ports_cfg, unsigned out_ports_len,
                       lmk05318_state_t* out, bool dry_run)
{
    int res;
    uint8_t dummy[4] = {0,0,0,0};
    memset(out, 0, sizeof(lmk05318_state_t));

    lmk05318_registers_map_reset();

    out->dev = dev;
    out->subdev = subdev;
    out->lsaddr = lsaddr;
    out->vco2_freq = 0;
    out->pd1 = 0;
    out->pd2 = 0;
    out->fref_pll2_div_rp = 3;
    out->fref_pll2_div_rs = (((VCO_APLL1 + APLL2_PD_MAX - 1) / APLL2_PD_MAX) + out->fref_pll2_div_rp - 1) / out->fref_pll2_div_rp;
    out->xo = *xo;

    res = dry_run ? 0 : lmk05318_reg_get_u32(out, 0, &dummy[0]);
    if (res)
        return res;

    USDR_LOG("5318", USDR_LOG_INFO, "LMK05318 DEVID[0/1/2/3] = %02x %02x %02x %02x\n", dummy[3], dummy[2], dummy[1], dummy[0]);

    if (!dry_run && (dummy[3] != 0x10 || dummy[2] != 0x0b || dummy[1] != 0x35 || dummy[0] != 0x42)) {
        return -ENODEV;
    }

#if 0
    out->dpll.enabled = true;
    res = lmk05318_reg_wr_n(out, lmk05318_rom_dpll, SIZEOF_ARRAY(lmk05318_rom_dpll));
    if (res)
        return res;
#endif

#if 1

    //detect ZDM mode -> if 1)OUT7 = 1Hz 2) DPLL input ref = 1Hz
    bool zdm = false;
    for(unsigned i = 0; i < out_ports_len; ++i)
    {
        lmk05318_out_config_t* p = out_ports_cfg + i;
        if(p->port == 7 && p->wanted.freq == 1)
        {
            zdm = true;
            break;
        }
    }
    zdm &= (dpll->enabled && ((dpll->en[LMK05318_PRIREF] && dpll->fref[LMK05318_PRIREF] == 1)
                           || (dpll->en[LMK05318_SECREF] && dpll->fref[LMK05318_SECREF] == 1)));
    if(zdm)
        USDR_LOG("5318", USDR_LOG_DEBUG, "[DPLL] ZDM enabled");
    //

    res = lmk05318_init(out, dpll, zdm);
    if(res)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 error %d on init()", res);
        return res;
    }

    res = lmk05318_set_xo_fref(out);
    if(res)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 error %d setting XO", res);
        return res;
    }

    res = lmk05318_tune_apll1(out);
    if(res)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 error %d tuning APLL1", res);
        return res;
    }

    res = lmk05318_solver(out, out_ports_cfg, out_ports_len, false /*dry_run*/);
    if(res)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 error %d solving output frequencies", res);
        return res;
    }

    res = lmk05318_reg_wr_from_map(out, dry_run);
    if(res)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 error %d writing registers", res);
        return res;
    }
#endif

    res = dry_run ? 0 : lmk05318_softreset(out);
    if(res)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 error %d lmk05318_softreset()", res);
        return res;
    }

    USDR_LOG("5318", USDR_LOG_INFO, "LMK05318 initialized\n");
    return 0;
}

/*
 * Legacy function, remove it later
 */
int lmk05318_create(lldev_t dev, unsigned subdev, unsigned lsaddr, unsigned int flags, lmk05318_state_t* out)
{
    int res;
    uint8_t dummy[4];

    const uint32_t* lmk_init = flags ? lmk05318_rom_49152_12288_384 : lmk05318_rom;
    unsigned lmk_init_sz = flags ? SIZEOF_ARRAY(lmk05318_rom_49152_12288_384) : SIZEOF_ARRAY(lmk05318_rom);

    out->dev = dev;
    out->subdev = subdev;
    out->lsaddr = lsaddr;
    out->vco2_freq = 0;
    out->pd1 = 0;
    out->pd2 = 0;

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

    out->xo.fref = 0;
    out->xo.doubler_enabled = false;
    out->xo.fdet_bypass = false;
    out->xo.type = XO_CMOS;

    USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 initialized\n");
    return 0;
}
/**/

/*
 * Legacy function, remove it later
 */
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
/**/

VWLT_ATTRIBUTE(optimize("-Ofast"))
static inline double lmk05318_calc_vco2_div(lmk05318_state_t* d, uint64_t fvco2, unsigned* pn, unsigned* pnum, unsigned* pden)
{
    const uint64_t pll2_tot_prediv = d->fref_pll2_div_rp * d->fref_pll2_div_rs;

    uint64_t den64 = VCO_APLL1 * pll2_tot_prediv;
    double r = (double)(fvco2 * pll2_tot_prediv) / VCO_APLL1;
    unsigned n = (unsigned)r;
    double n_frac = r - n;
    uint64_t num64 = (uint64_t)(n_frac * den64 + 0.5);

    uint64_t nod = find_gcd(num64, den64);
    if(nod > 1)
    {
#ifdef LMK05318_SOLVER_DEBUG
        USDR_LOG("5318", USDR_LOG_DEBUG, "PLL2 NUM/DEN reduced NOD:%" PRIu64 ": %" PRIu64 "/%" PRIu64" -> %" PRIu64 "/%" PRIu64,
                 nod, num64, den64, num64/nod, den64/nod);
#endif
        num64 /= nod;
        den64 /= nod;
    }

    if(den64 > 0xFFFFFF)
    {
#ifdef LMK05318_SOLVER_DEBUG
        USDR_LOG("5318", USDR_LOG_ERROR, "PLL2_DEN overflow, cannot solve in integer values");
#endif
        return -EINVAL;
    }

    uint32_t num = num64;
    uint32_t den = den64;

    const double fvco2_fact = (double)VCO_APLL1 * (n + (double)num / den) / pll2_tot_prediv;

#ifdef LMK05318_SOLVER_DEBUG
    USDR_LOG("5318", USDR_LOG_ERROR, "WANTED_VCO2:%" PRIu64 "  N:%u NUM:%u DEN:%u VCO2:%.8f", fvco2, n, num, den, fvco2_fact);
#endif

    if(pn)
        *pn = n;
    if(pnum)
        *pnum = num;
    if(pden)
        *pden = den;

    return fvco2_fact;
}

static int lmk05318_tune_apll2_ex(lmk05318_state_t* d)
{
    double fpd2 = (double)VCO_APLL1 / d->fref_pll2_div_rp / d->fref_pll2_div_rs;
    if (fpd2 < APLL2_PD_MIN || fpd2 > APLL2_PD_MAX) {
        USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 APLL2 PFD should be in range [%" PRIu64 ";%" PRIu64 "] but got %.8f!\n",
                 (uint64_t)APLL2_PD_MIN, (uint64_t)APLL2_PD_MAX, fpd2);
        return -EINVAL;
    }

    if(d->vco2_freq < VCO_APLL2_MIN || d->vco2_freq > VCO_APLL2_MAX ||
        ((d->pd1 < APLL2_PDIV_MIN || d->pd1 > APLL2_PDIV_MAX) && (d->pd2 < APLL2_PDIV_MIN || d->pd2 > APLL2_PDIV_MAX))
        )
    {
        USDR_LOG("5318", USDR_LOG_WARNING, "LMK05318 APLL2: either FVCO2[%" PRIu64"] or (PD1[%d] && PD2[%d]) is out of range, APLL2 will be disabled",
                 d->vco2_freq, d->pd1, d->pd2);
        // Disable
        uint32_t regs[] = {
            MAKE_LMK05318_PLL2_CTRL0(d->fref_pll2_div_rs - 1, d->fref_pll2_div_rp - 3, 1), //R100 Deactivate APLL2
        };
        return lmk05318_add_reg_to_map(d, regs, SIZEOF_ARRAY(regs));
    }

    const unsigned n   = d->vco2_n;
    const unsigned num = d->vco2_num;
    const unsigned den = d->vco2_den;
    int res;

    USDR_LOG("5318", USDR_LOG_INFO, "LMK05318 APLL2 RS=%u RP=%u FPD2=%.8f FVCO2=%" PRIu64 " N=%d NUM=%d DEN=%d PD1=%d PD2=%d\n",
             d->fref_pll2_div_rs, d->fref_pll2_div_rp, fpd2, d->vco2_freq, n, num, den, d->pd1, d->pd2);

    // one of PDs may be unused (==0) -> we should fix it before registers set
    if(d->pd1 < APLL2_PDIV_MIN || d->pd1 > APLL2_PDIV_MAX)
    {
        d->pd1 = d->pd2;
    }
    else if(d->pd2 < APLL2_PDIV_MIN || d->pd2 > APLL2_PDIV_MAX)
    {
        d->pd2 = d->pd1;
    }

    uint32_t regs[] = {
        MAKE_LMK05318_PLL2_CTRL2(d->pd2 - 1, d->pd1 - 1),                               //R102
        MAKE_LMK05318_PLL2_NDIV_BY0(n),                                                 //R135
        MAKE_LMK05318_PLL2_NDIV_BY1(n),                                                 //R134
        MAKE_LMK05318_PLL2_NUM_BY0(num),                                                //R138
        MAKE_LMK05318_PLL2_NUM_BY1(num),                                                //R137
        MAKE_LMK05318_PLL2_NUM_BY2(num),                                                //R136
        MAKE_LMK05318_PLL2_DEN_BY0(den),                                                //R333
        MAKE_LMK05318_PLL2_DEN_BY1(den),                                                //R334
        MAKE_LMK05318_PLL2_DEN_BY2(den),                                                //R335
        MAKE_LMK05318_PLL2_CTRL0(d->fref_pll2_div_rs - 1, d->fref_pll2_div_rp - 3, 0),  //R100  Activate APLL2
    };

    res = lmk05318_add_reg_to_map(d, regs, SIZEOF_ARRAY(regs));
    if (res)
        return res;

    return 0;
}

int lmk05318_set_xo_fref(lmk05318_state_t* d)
{
    const uint32_t xo_fref = d->xo.fref;
    const int xo_type = d->xo.type;
    const bool xo_doubler_enabled = d->xo.doubler_enabled;
    const bool xo_fdet_bypass = d->xo.fdet_bypass;

    if(xo_fref < XO_FREF_MIN || xo_fref > XO_FREF_MAX)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 XO input fref should be in range [%" PRIu64 ";%" PRIu64 "] but got %d!\n",
                 (uint64_t)XO_FREF_MIN, (uint64_t)XO_FREF_MAX, xo_fref);
        return -EINVAL;
    }

    if(d->xo.pll1_fref_rdiv < APLL1_DIVIDER_MIN || d->xo.pll1_fref_rdiv > APLL1_DIVIDER_MAX)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 APPL1_RDIV:%d out of range [%d;%d]", d->xo.pll1_fref_rdiv, (int)APLL1_DIVIDER_MIN, (int)APLL1_DIVIDER_MAX);
        return -EINVAL;
    }

    int xo_type_raw;
    switch((int)xo_type)
    {
    case XO_DC_DIFF_EXT: xo_type_raw = XO_TYPE_DC_DIFF_EXT; break;
    case XO_AC_DIFF_EXT: xo_type_raw = XO_TYPE_AC_DIFF_EXT; break;
    case XO_AC_DIFF_INT_100: xo_type_raw = XO_TYPE_AC_DIFF_INT_100; break;
    case XO_HCSL_INT_50: xo_type_raw = XO_TYPE_HCSL_INT_50; break;
    case XO_CMOS: xo_type_raw = XO_TYPE_CMOS; break;
    case XO_SE_INT_50: xo_type_raw = XO_TYPE_SE_INT_50; break;
    default:
        USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 XO input type %d is not supported!\n", (int)xo_type);
        return -EINVAL;
    }

    uint32_t regs[] = {
        MAKE_LMK05318_XO_CLKCTL1(xo_doubler_enabled ? 1 : 0, xo_fdet_bypass ? 1 : 0, 0, 1),   //R42
        MAKE_LMK05318_XO_CLKCTL2(1, xo_type_raw, 2),                                          //R43
        MAKE_LMK05318_XO_CONFIG(d->xo.pll1_fref_rdiv - 1),                                    //R44
    };

    int res = lmk05318_add_reg_to_map(d, regs, SIZEOF_ARRAY(regs));
    if(res)
        return res;

    return 0;
}

int lmk05318_tune_apll1(lmk05318_state_t* d)
{
    unsigned fpd1 = (d->xo.fref / d->xo.pll1_fref_rdiv) * (d->xo.doubler_enabled ? 2 : 1);
    uint64_t fvco = VCO_APLL1;
    unsigned n = fvco / fpd1;

    //in DPLL mode we use FIXED 40-bit APLL1 denominator and programmed 40-bit numerator
    if(d->dpll.enabled)
    {
        uint64_t num = (fvco - n * (uint64_t)fpd1) * (1ull << 40) / fpd1;

        USDR_LOG("5318", USDR_LOG_INFO, "LMK05318 APLL1 FVCO=%" PRIu64 " N=%d NUM=%" PRIu64 " DEN=FIXED\n", fvco, n, num);

        uint32_t regs[] = {
            MAKE_LMK05318_PLL1_MODE(0, 0, 1),               //R116  DPLL mode
            MAKE_LMK05318_PLL1_NDIV_BY0(n),                 //R109  NDIV
            MAKE_LMK05318_PLL1_NDIV_BY1(n),                 //R108  NDIV
            MAKE_LMK05318_PLL1_NUM_BY0(num),                //R110 |
            MAKE_LMK05318_PLL1_NUM_BY1(num),                //R111 |
            MAKE_LMK05318_PLL1_NUM_BY2(num),                //R112 | 40-bit NUM
            MAKE_LMK05318_PLL1_NUM_BY3(num),                //R113 |
            MAKE_LMK05318_PLL1_NUM_BY4(num),                //R114 |
            MAKE_LMK05318_PLL1_CTRL0(0),                    //R74   Activate APLL1
        };

        int res = lmk05318_add_reg_to_map(d, regs, SIZEOF_ARRAY(regs));
        if (res)
            return res;
    }
    // without DPLL we use programmed 24-bit numerator & programmed 24-bit denominator
    else
    {
        double frac = (double)fvco / fpd1 - n;
        const uint32_t den = ((uint32_t)1 << 24) - 1; //max 24-bit
        const uint32_t num = (frac * den + 0.5);

        USDR_LOG("5318", USDR_LOG_INFO, "LMK05318 APLL1 FVCO=%" PRIu64 " N=%d NUM=%" PRIu32 " DEN=%" PRIu32 "\n", fvco, n, num, den);

        uint32_t regs[] = {
            MAKE_LMK05318_PLL1_MODE(0, 0, 0),                         //R116  free-run mode
            MAKE_LMK05318_PLL1_NDIV_BY0(n),                           //R109  NDIV
            MAKE_LMK05318_PLL1_NDIV_BY1(n),                           //R108  NDIV

            MAKE_LMK05318_REG_WR(PLL1_NUM_BY4, (uint8_t)den),         //R114
            MAKE_LMK05318_REG_WR(PLL1_NUM_BY3, (uint8_t)(den >> 8)),  //R113 | 24-bit DEN
            MAKE_LMK05318_REG_WR(PLL1_NUM_BY2, (uint8_t)(den >> 16)), //R112

            MAKE_LMK05318_REG_WR(PLL1_NUM_BY1, (uint8_t)num),         //R111
            MAKE_LMK05318_REG_WR(PLL1_NUM_BY0, (uint8_t)(num >> 8)),  //R110 | 24-bit NUM
            MAKE_LMK05318_PLL1_24B_NUM_23_16((uint8_t)(num >> 16)),   //R339

            MAKE_LMK05318_PLL1_CTRL0(0),                              //R74   Activate APLL1
        };

        int res = lmk05318_add_reg_to_map(d, regs, SIZEOF_ARRAY(regs));
        if (res)
            return res;
    }

    return 0;
}

static inline uint64_t lmk05318_max_odiv(unsigned port)
{
    switch(port)
    {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6: return ((uint64_t)1 <<  8);
    case 7: return ((uint64_t)1 << 32);
    }
    return 1;
}

int lmk05318_set_out_div(lmk05318_state_t* d, unsigned port, uint64_t udiv)
{
    if (port > (LMK05318_MAX_OUT_PORTS - 1) || udiv < 1 || udiv > lmk05318_max_odiv(port))
        return -EINVAL;

    //out7 is special
    if(port == 7)
    {
        uint64_t div_stage2 = udiv >> 8;
        div_stage2 = div_stage2 ? div_stage2 : 1;
        uint8_t div_stage1 = udiv / div_stage2;

        --div_stage1;
        --div_stage2;

        uint32_t regs[] =
        {
            MAKE_LMK05318_OUTDIV_7(div_stage1),
            MAKE_LMK05318_OUTDIV_7_STG2_BY0(div_stage2),
            MAKE_LMK05318_OUTDIV_7_STG2_BY1(div_stage2),
            MAKE_LMK05318_OUTDIV_7_STG2_BY2(div_stage2),
        };

        return lmk05318_add_reg_to_map(d, regs, SIZEOF_ARRAY(regs));
    }

    uint32_t reg = 0;
    switch(port)
    {
    case 6: reg = MAKE_LMK05318_OUTDIV_6(udiv - 1); break;
    case 5: reg = MAKE_LMK05318_OUTDIV_5(udiv - 1); break;
    case 4: reg = MAKE_LMK05318_OUTDIV_4(udiv - 1); break;
    case 3:
    case 2: reg = MAKE_LMK05318_OUTDIV_2_3(udiv - 1); break;
    case 1:
    case 0: reg = MAKE_LMK05318_OUTDIV_0_1(udiv-1); break;
    default:
        return -EINVAL;
    }

    return lmk05318_add_reg_to_map(d, &reg, 1);
}

static inline const char* lmk05318_decode_fmt(unsigned f)
{
    switch (f) {
    case LVDS: return "OUT_OPTS_AC_LVDS";
    case CML: return "OUT_OPTS_AC_CML";
    case LVPECL: return "OUT_OPTS_AC_LVPECL";
    case LVCMOS: return "OUT_OPTS_LVCMOS_P_N";
    default: return "OUT_OPTS_Disabled";
    }
    return "UNKNOWN";
}

static int lmk05318_set_out_mux_ex(lmk05318_state_t* d, unsigned port, unsigned mux, unsigned otype)
{
    unsigned ot;
    switch (otype) {
    case LVDS: ot = OUT_OPTS_AC_LVDS; break;
    case CML: ot = OUT_OPTS_AC_CML; break;
    case LVPECL: ot = OUT_OPTS_AC_LVPECL; break;
    case LVCMOS: ot = OUT_OPTS_LVCMOS_P_N; break;
    default: ot = OUT_OPTS_Disabled; break;
    }

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
    return lmk05318_add_reg_to_map(d, regs, SIZEOF_ARRAY(regs));
}

int lmk05318_set_out_mux(lmk05318_state_t* d, unsigned port, bool pll1, unsigned otype)
{
    return lmk05318_set_out_mux_ex(d, port, (pll1 ? OUT_PLL_SEL_APLL1_P1 : OUT_PLL_SEL_APLL2_P1), otype);
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

VWLT_ATTRIBUTE(optimize("-Ofast"))
static inline int lmk05318_get_output_divider(const lmk05318_out_config_t* cfg, double ifreq, uint64_t* div)
{
    *div = (uint64_t)((double)ifreq / cfg->wanted.freq + 0.5);

    if(*div == 0 || *div > cfg->max_odiv)
        return 1;

    double factf = ifreq / (*div);
    return (factf == cfg->wanted.freq) ? 0 : 1;
}

VWLT_ATTRIBUTE(optimize("-Ofast"))
static inline int lmk05318_solver_helper(lmk05318_out_config_t* outs, unsigned cnt_to_solve, uint64_t f_in,
                                         lmk05318_state_t* lmkst)
{
#ifdef LMK05318_SOLVER_DEBUG
    USDR_LOG("5318", USDR_LOG_DEBUG, "Solver iteration FVCO2:%" PRIu64 "", f_in);
#endif

    struct fvco2_range
    {
        int port_idx;
        int pd;
        uint64_t od;
        range_t fvco2;
    };
    typedef struct fvco2_range fvco2_range_t;

    fvco2_range_t fvco2_ranges[(APLL2_PDIV_MAX - APLL2_PDIV_MIN + 1) * 2 * LMK05318_MAX_REAL_PORTS];
    int fvco2_ranges_count = 0;

    // find FVCO2 ranges for all PDs and all ports
    for(unsigned i = 0; i < LMK05318_MAX_REAL_PORTS; ++i)
    {
        lmk05318_out_config_t* out = outs + i;
        if(out->solved)
            continue;

        for(int pd = out->pd_min; pd <= out->pd_max; ++pd)
        {
            uint64_t f_pd = (uint64_t)((double)f_in / pd + 0.5);
            uint64_t divs[2];

            lmk05318_get_output_divider(out, f_pd, &divs[0]);
            divs[1] = (divs[0] < out->max_odiv) ? divs[0] + 1 : 0;

            for(int d = 0; d < 2; ++d)
            {
                const uint64_t div = divs[d];
                if(!div)
                    continue;

                if(div <= out->max_odiv)
                {
                    uint64_t fvco2_min = MAX(pd * div * out->freq_min, VCO_APLL2_MIN);
                    uint64_t fvco2_max = MIN(pd * div * out->freq_max, VCO_APLL2_MAX);

                    if(fvco2_min <= fvco2_max)
                    {
                        fvco2_range_t* rr = &fvco2_ranges[fvco2_ranges_count++];
                        rr->port_idx = i;
                        rr->pd = pd;
                        rr->od = div;
                        rr->fvco2.min = fvco2_min;
                        rr->fvco2.max = fvco2_max;
                    }
                }
            }
        }
    }

    if(!fvco2_ranges_count)
    {
#ifdef LMK05318_SOLVER_DEBUG
        USDR_LOG("5318", USDR_LOG_ERROR, "For FVCO2:%" PRIu64 " all possible bands are out of range", f_in);
#endif
        return -EINVAL;
    }

#ifdef LMK05318_SOLVER_DEBUG
    for(int i = 0; i < fvco2_ranges_count; ++i)
    {
        fvco2_range_t* rr = &fvco2_ranges[i];

        USDR_LOG("5318", USDR_LOG_DEBUG, "\t[%d]\tPort#%d PD:%d OD:%" PRIu64 " FVCO2 range:[%" PRIu64 "; %" PRIu64 "]",
                 i, outs[rr->port_idx].port, rr->pd, rr->od, rr->fvco2.min, rr->fvco2.max);
    }
#endif

    struct intersects
    {
        int prim_idx;
        range_t intersection;
        int sect_counter;
        int sects[(APLL2_PDIV_MAX - APLL2_PDIV_MIN + 1) * 2 * LMK05318_MAX_REAL_PORTS];
    };
    typedef struct intersects intersects_t;

    int intersects_array_count = 0;
    intersects_t intersects_array[fvco2_ranges_count];

    // find FVCO2 ranges intersections
    for(int i = 0; i < fvco2_ranges_count; ++i)
    {
        fvco2_range_t* rr_prim = &fvco2_ranges[i];
        range_t intersection = rr_prim->fvco2;

        int sect_counter = 0;
        int sects[fvco2_ranges_count];

        for(int j = i + 1; j < fvco2_ranges_count; ++j)
        {
            fvco2_range_t* rr_sec = &fvco2_ranges[j];

            //ignore equal port variants with different PDs
            if(outs[rr_sec->port_idx].port == outs[rr_prim->port_idx].port && cnt_to_solve != 1)
                continue;

            uint64_t nmin = MAX(intersection.min, rr_sec->fvco2.min);
            uint64_t nmax = MIN(intersection.max, rr_sec->fvco2.max);

            //ignore not-intersected ranges
            if(nmin > nmax)
                continue;

            intersection.min = nmin;
            intersection.max = nmax;
            sects[sect_counter++] = j;
        }

        if(sect_counter)
        {
            intersects_t* isect = &intersects_array[intersects_array_count++];
            isect->sect_counter = 0;
            isect->prim_idx = i;
            isect->intersection = intersection;

            for(int i = 0; i < sect_counter; ++i)
                isect->sects[isect->sect_counter++] = sects[i];
        }
    }

    if(!intersects_array_count)
    {
#ifdef LMK05318_SOLVER_DEBUG
        USDR_LOG("5318", USDR_LOG_ERROR, "FVCO2 bands have no intersections");
#endif
        return -EINVAL;
    }

#ifdef LMK05318_SOLVER_DEBUG
    for(int i = 0; i < intersects_array_count; ++i)
    {
        const intersects_t* isect = &intersects_array[i];
        fvco2_range_t* rr_prim = &fvco2_ranges[isect->prim_idx];

        USDR_LOG("5318", USDR_LOG_DEBUG, "Found sects for [%d]\tPort#%d PD:%d OD:%" PRIu64 " FVCO2 range:[%" PRIu64 "; %" PRIu64 "]:",
                 isect->prim_idx, outs[rr_prim->port_idx].port, rr_prim->pd, rr_prim->od, isect->intersection.min, isect->intersection.max);

        for(int j = 0; j < isect->sect_counter; ++j)
        {
            const fvco2_range_t* rr = &fvco2_ranges[isect->sects[j]];
            USDR_LOG("5318", USDR_LOG_DEBUG, "\twith [%d] port#%d PD:%d OD:%" PRIu64 "",
                     isect->sects[j], outs[rr->port_idx].port, rr->pd, rr->od);
        }
    }
#endif

    struct solution_var_div
    {
        int pd;
        uint64_t od;
    };
    typedef struct solution_var_div solution_var_div_t;

    struct solution_var
    {
        int port_idx;
        solution_var_div_t divs[(APLL2_PDIV_MAX - APLL2_PDIV_MIN + 1)];
        int divs_count;
    };
    typedef struct solution_var solution_var_t;

    struct solution
    {
        range_t fvco2;
        solution_var_t vars[LMK05318_MAX_REAL_PORTS];
        int vars_count;
        bool is_valid;
    };
    typedef struct solution solution_t;


    solution_t solutions[intersects_array_count];
    int solutions_count = 0;
    bool has_valid_solution = false;

    // reduce intersections to solutions, filtering out invalid ones
    for(int i = 0; i < intersects_array_count; ++i)
    {
        intersects_t* isect = &intersects_array[i];
        solution_t* sol = &solutions[solutions_count++];
        fvco2_range_t* rr = &fvco2_ranges[isect->prim_idx];

        sol->vars_count = 0;
        sol->is_valid = false;
        sol->fvco2 = isect->intersection;

        solution_var_t* var = &sol->vars[sol->vars_count++];
        var->port_idx = rr->port_idx;
        var->divs_count = 0;

        solution_var_div_t* div = &var->divs[var->divs_count++];
        div->pd = rr->pd;
        div->od = rr->od;

        for(int j = 0; j < isect->sect_counter; ++j)
        {
            rr = &fvco2_ranges[isect->sects[j]];
            var = NULL;

            for(int k = 0; k < sol->vars_count; ++k)
            {
                solution_var_t* vv = &sol->vars[k];
                if(vv->port_idx == rr->port_idx)
                {
                    var = vv;
                    break;
                }
            }

            if(!var)
            {
                var = &sol->vars[sol->vars_count++];
                var->port_idx = rr->port_idx;
                var->divs_count = 0;
            }

            div = NULL;
            for(int k = 0; k < var->divs_count; ++k)
            {
                solution_var_div_t* dd = &var->divs[k];
                if(dd->pd == rr->pd)
                {
                    div = dd;
                    break;
                }
            }

            if(!div)
            {
                div = &var->divs[var->divs_count++];
                div->pd = rr->pd;
                div->od = rr->od;
            }
        }

        sol->is_valid = (sol->vars_count == cnt_to_solve);
        if(sol->is_valid)
            has_valid_solution = true;
    }

#ifdef LMK05318_SOLVER_DEBUG
    for(int i = 0; i < solutions_count; ++i)
    {
        const solution_t* sol = &solutions[i];
        if(!sol->is_valid)
            continue;

        USDR_LOG("5318", USDR_LOG_DEBUG, "Solution [%d] in FVCO2 range [%" PRIu64 "; %" PRIu64 "]:",
                 i, sol->fvco2.min, sol->fvco2.max);

        for(int j = 0; j < sol->vars_count; ++j)
        {
            const solution_var_t* var = &sol->vars[j];
            char tmp[1024];
            int tmp_len = sprintf(tmp, "\t Port#%d PD:[", outs[var->port_idx].port);

            for(int k = 0; k < var->divs_count; ++k)
            {
                const solution_var_div_t* div = &var->divs[k];
                tmp_len += sprintf(tmp + tmp_len, "%d(OD:%" PRIu64 "),", div->pd, div->od);
            }

            tmp_len += sprintf(tmp + tmp_len, "]");

            USDR_LOG("5318", USDR_LOG_DEBUG, "%s", tmp);
        }
    }
#endif

    if(!has_valid_solution)
    {
#ifdef LMK05318_SOLVER_DEBUG
        USDR_LOG("5318", USDR_LOG_ERROR, "We have NO solutions containing all ports required");
#endif
        return -EINVAL;
    }

    struct pd_bind
    {
        int pd;
        int ports[LMK05318_MAX_REAL_PORTS];
        uint64_t odivs[LMK05318_MAX_REAL_PORTS];
        int ports_count;
    };
    typedef struct pd_bind pd_bind_t;

    //transform solitions to PD bindings -> PD1:[ports:ODs], PD2:[ports:ODs]
    for(int i = 0; i < solutions_count; ++i)
    {
        solution_t* sol = &solutions[i];
        if(!sol->is_valid)
            continue;

        pd_bind_t pd_binds[APLL2_PDIV_COUNT];
        memset(pd_binds, 0, sizeof(pd_binds));
        int pd_binds_count = 0;
        int pd1 = 0, pd2 = 0;

        //first var is always one, assume it's PD1
        pd1 = sol->vars[0].divs[0].pd;
        pd_binds[0].pd = pd1;
        pd_binds[0].ports_count = 1;
        pd_binds[0].ports[0] = sol->vars[0].port_idx;
        pd_binds[0].odivs[0] = sol->vars[0].divs[0].od;
        pd_binds_count = 1;

        int unmapped_ports[LMK05318_MAX_REAL_PORTS];
        int unmapped_ports_count = 0;

        //scan all other vars, try to find variants with PD==PD1 and add them to PD1 binding
        //otherwise - add var to an unmapped_ports[]
        for(int j = 1; j < sol->vars_count; ++j)
        {
            const solution_var_t* var = &sol->vars[j];
            bool port_mapped = false;
            for(int k = 0; k < var->divs_count; ++k)
            {
                const solution_var_div_t* div = &var->divs[k];
                if(div->pd == pd1)
                {
                    pd_binds[0].ports[pd_binds[0].ports_count] = var->port_idx;
                    pd_binds[0].odivs[pd_binds[0].ports_count] = div->od;
                    pd_binds[0].ports_count++;
                    port_mapped = true;
                    break;
                }
            }

            if(!port_mapped)
            {
                unmapped_ports[unmapped_ports_count++] = j;
            }
        }

        if(unmapped_ports_count)
        {
            //step on first unmapped_ports[] var
            const solution_var_t* var = &sol->vars[unmapped_ports[0]];

            //iterate through its' divs and try to find equal PDs in the unmapped_ports[] below
            for(int d = 0; d < var->divs_count; ++d)
            {
                const solution_var_div_t* div = &var->divs[d];

                //assume it is PD2
                pd2 = div->pd;
                pd_binds[1].pd = pd2;
                pd_binds[1].ports[0] = var->port_idx;
                pd_binds[1].odivs[0] = div->od;
                pd_binds[1].ports_count = 1;

                //iterate unmapped ports below and try to find vars with PD==PD2 and add them to PD2 binding
                for(int u = 1; u < unmapped_ports_count; ++u)
                {
                    const solution_var_t* var2 = &sol->vars[unmapped_ports[u]];
                    bool found = false;
                    for(int dd = 0; dd < var2->divs_count; ++dd)
                    {
                        const solution_var_div_t* div2 = &var2->divs[dd];
                        if(div2->pd == pd2)
                        {
                            pd_binds[1].ports[pd_binds[1].ports_count] = var2->port_idx;
                            pd_binds[1].odivs[pd_binds[1].ports_count] = div2->od;
                            pd_binds[1].ports_count++;

                            found = true;
                            break;
                        }
                    }

                    //if this var does not contain the assumed PD2, no need to continue - break and try next PD2
                    if(!found)
                    {
                        break;
                    }
                }

                //check if we mapped all the ports needed
                int binded_ports = pd_binds[0].ports_count + pd_binds[1].ports_count;
                if(binded_ports == cnt_to_solve)
                {
                    pd_binds_count = pd_binds[1].ports_count ? 2 : 1;
                    sol->is_valid = true;
                }
                else
                {
                    sol->is_valid = false;
                    continue;
                }
            }
        }

        if(!sol->is_valid)
            continue;

        USDR_LOG("5318", USDR_LOG_DEBUG, "SOLUTION#%d valid:%d FVCO2[%" PRIu64 "; %" PRIu64 "]->", i, sol->is_valid, sol->fvco2.min, sol->fvco2.max);

        for(uint64_t f = sol->fvco2.min; f <= sol->fvco2.max; ++f)
        {
            unsigned n, num, den;
            double fvco2 = lmk05318_calc_vco2_div(lmkst, f, &n, &num, &den);

            if(fvco2 < sol->fvco2.min || fvco2 > sol->fvco2.max)
                continue;

            if(fvco2 != (uint64_t)fvco2)
                continue;

            bool ok_flag = true;

            for(int ii = 0; ii < pd_binds_count; ++ii)
            {
                const pd_bind_t* b = &pd_binds[ii];

                for(int j = 0; j < b->ports_count; ++j)
                {
                    lmk05318_out_config_t* out = &outs[b->ports[j]];

                    uint64_t div;
                    const uint64_t fdiv_in = (uint64_t)(fvco2 / b->pd + 0.5);
                    int res = lmk05318_get_output_divider(out, fdiv_in, &div);
                    if(res)
                    {
                        ok_flag = false;
                        break;
                    }

                    out->result.out_div = div;
                    out->result.freq = fvco2 / b->pd / div;
                    out->result.mux = (b->pd == pd1) ? OUT_PLL_SEL_APLL2_P1 : OUT_PLL_SEL_APLL2_P2;
                    out->solved = true;
                }

                if(!ok_flag)
                    break;
            }

            if(ok_flag)
            {
                lmkst->vco2_freq = fvco2;
                lmkst->vco2_n = n;
                lmkst->vco2_num = num;
                lmkst->vco2_den = den;
                lmkst->pd1 = pd1;
                lmkst->pd2 = pd2;
                return 0;
            }
        }
    }

#ifdef LMK05318_SOLVER_DEBUG
    USDR_LOG("5318", USDR_LOG_ERROR, "We have NO solutions using 2 PDs (need more pre-dividers)");
#endif
    return -EINVAL;
}


static int lmk05318_comp_port(const void * elem1, const void * elem2)
{
    const lmk05318_out_config_t* f = (lmk05318_out_config_t*)elem1;
    const lmk05318_out_config_t* s = (lmk05318_out_config_t*)elem2;

    if(f->port < s->port) return -1;
    if(f->port > s->port) return  1;
    return 0;
}


static const char* lmk05318_decode_mux(enum lmk05318_out_pll_sel_t mux)
{
    switch(mux)
    {
    case OUT_PLL_SEL_APLL1_P1:      return "APLL1";
    case OUT_PLL_SEL_APLL1_P1_INV:  return "APLL1 inv";
    case OUT_PLL_SEL_APLL2_P1:      return "APLL2 PD1";
    case OUT_PLL_SEL_APLL2_P2:      return "APLL2 PD2";
    }
    return "UNKNOWN";
}

VWLT_ATTRIBUTE(optimize("-Ofast"))
int lmk05318_solver(lmk05318_state_t* d, lmk05318_out_config_t* _outs, unsigned n_outs, bool dry_run)
{
    if(!_outs || !n_outs || n_outs > LMK05318_MAX_OUT_PORTS)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "input data is incorrect");
        return -EINVAL;
    }

    // internally we have only _6_ out divs and freqs (0==1 and 2==3) - except the output type, but it does not matter here
    lmk05318_out_config_t outs[LMK05318_MAX_REAL_PORTS];
    memset(outs, 0, sizeof(outs));

    for(unsigned i = 0; i < n_outs; ++i)
    {
        lmk05318_out_config_t* out = _outs + i;

        if(out->wanted.freq == 0)
        {
            USDR_LOG("5318", USDR_LOG_DEBUG, "skipping port#%d freq=0", out->port);
            continue;
        }

        if(out->port > LMK05318_MAX_OUT_PORTS - 1)
        {
            USDR_LOG("5318", USDR_LOG_ERROR, "port value should be in [0; %d] diap", (LMK05318_MAX_OUT_PORTS - 1));
            return -EINVAL;
        }

        if(out->wanted.type == LVCMOS && out->port < 4)
        {
            USDR_LOG("5318", USDR_LOG_ERROR, "LVCMOS output type supported for ports# 4..7 only");
            return -EINVAL;
        }

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
             norm_out->wanted.revert_phase != out->wanted.revert_phase ||
             norm_out->wanted.pll_affinity != out->wanted.pll_affinity
            ))
        {
            USDR_LOG("5318", USDR_LOG_ERROR, "dup ports values detected, or ports #0:1 & #2:3 differ");
            return -EINVAL;
        }

        range_t r = lmk05318_get_freq_range(out);
        out->freq_min = r.min;
        out->freq_max = r.max;
        out->max_odiv = lmk05318_max_odiv(out->port);

        *norm_out = *out;
        norm_out->solved = false;
    }

    //now outs[] contains effective ports ordered (0..5) config.
    //some elems may be not initialized (wanted.freq == 0) and should not be processed.
    for(unsigned i = 0; i < LMK05318_MAX_REAL_PORTS; ++i)
    {
        outs[i].solved = outs[i].wanted.freq == 0;
        USDR_LOG("5318", USDR_LOG_DEBUG, "port:%d freq:%d (-%d, +%d) *%s*",
                 outs[i].port, outs[i].wanted.freq, outs[i].wanted.freq_delta_minus, outs[i].wanted.freq_delta_plus,
                 outs[i].solved ? "not used" : "active");
    }

    //first we try routing ports to APLL1
    //it's easy
    for(unsigned i = 0; i < LMK05318_MAX_REAL_PORTS; ++i)
    {
        lmk05318_out_config_t* out = outs + i;
        if(out->solved)
            continue;

        if(out->wanted.pll_affinity == AFF_ANY || out->wanted.pll_affinity == AFF_APLL1)
        {
            uint64_t odiv = 0;
            int res = lmk05318_get_output_divider(out, VCO_APLL1, &odiv);
            if(!res)
            {
                out->solved = true;
                out->result.out_div = odiv;
                out->result.freq = (double)VCO_APLL1 / odiv;
                out->result.mux = out->wanted.revert_phase ? OUT_PLL_SEL_APLL1_P1_INV : OUT_PLL_SEL_APLL1_P1;

                USDR_LOG("5318", USDR_LOG_DEBUG, "port:%d solved via APLL1 [OD:%" PRIu64 " freq:%.2f mux:%d]",
                         out->port, out->result.out_div, out->result.freq, out->result.mux);
            }
            else
            {
                USDR_LOG("5318", USDR_LOG_DEBUG, "port:%d cannot solve it via APLL1, will try APLL2", out->port);
            }
        }
        else
        {
            USDR_LOG("5318", USDR_LOG_DEBUG, "port:%d forbidden to solve via APLL1 by config, will try APLL2", out->port);
        }

        //we cannot revert phase for ports NOT linked to APLL1
        if(!out->solved && out->wanted.revert_phase)
        {
            USDR_LOG("5318", USDR_LOG_ERROR, "port#%d specified as phase-reverted and cannot be solved via APLL1", out->port);
            return -EINVAL;
        }
    }

    //second - try routing to APLL2
    unsigned cnt_to_solve = 0;
    USDR_LOG("5318", USDR_LOG_DEBUG,"Need to solve via APLL2:");
    for(unsigned i = 0; i < LMK05318_MAX_REAL_PORTS; ++i)
    {
        if(outs[i].solved)
            continue;

        ++cnt_to_solve;

        USDR_LOG("5318", USDR_LOG_DEBUG, "\tport:%d freq:%d (-%d, +%d)",
                 outs[i].port, outs[i].wanted.freq, outs[i].wanted.freq_delta_minus, outs[i].wanted.freq_delta_plus);
    }

    if(!cnt_to_solve)
        goto have_complete_solution;

    static const uint64_t fvco2_pd_min = VCO_APLL2_MIN / APLL2_PDIV_MAX;
    static const uint64_t fvco2_pd_max = VCO_APLL2_MAX / APLL2_PDIV_MIN;

    //determine valid PD ranges for our frequencies
    for(unsigned i = 0; i < LMK05318_MAX_REAL_PORTS; ++i)
    {
        lmk05318_out_config_t* out = outs + i;
        if(out->solved)
            continue;

        const range_t r = lmk05318_get_freq_range(out);
        const range_t ifreq = {MAX(r.min, fvco2_pd_min) , MIN(r.max * lmk05318_max_odiv(out->port), fvco2_pd_max)};

        if(ifreq.min > ifreq.max)
        {
            USDR_LOG("5318", USDR_LOG_ERROR, "port#%d freq:%d (-%d, +%d) is totally out of available range",
                     out->port, out->wanted.freq, out->wanted.freq_delta_minus, out->wanted.freq_delta_plus);
            return -EINVAL;
        }

        const int pd_min = VCO_APLL2_MAX / ifreq.max;
        const int pd_max = VCO_APLL2_MAX / ifreq.min;

        out->pd_min = pd_min;
        out->pd_max = pd_max;

        USDR_LOG("5318", USDR_LOG_DEBUG, "port:%d pre-OD freq range:[%" PRIu64", %" PRIu64"], PD:[%d, %d]",
                 out->port, ifreq.min, ifreq.max, pd_min, pd_max);
    }

    const uint64_t f_mid = (VCO_APLL2_MAX + VCO_APLL2_MIN) / 2;
    int res = lmk05318_solver_helper(outs, cnt_to_solve, f_mid, d);
    if(res)
        return res;

have_complete_solution:

    //if ok - update the results

    qsort(_outs, n_outs, sizeof(lmk05318_out_config_t), lmk05318_comp_port);
    bool complete_solution_check = true;

    USDR_LOG("5318", USDR_LOG_DEBUG, "=== COMPLETE SOLUTION @ VCO1:%" PRIu64 " VCO2:%" PRIu64 " PD1:%d PD2:%d ===", VCO_APLL1, d->vco2_freq, d->pd1, d->pd2);
    for(unsigned i = 0; i < n_outs; ++i)
    {
        lmk05318_out_config_t* out_dst = _outs + i;
        lmk05318_out_config_t* out_src = NULL;

        if(out_dst->wanted.freq == 0)
            continue;

        if(out_dst->port < 2)
            out_src = outs + 0;
        else if(out_dst->port < 4)
            out_src = outs + 1;
        else
            out_src = outs + (out_dst->port - 2);

        out_dst->solved = out_src->solved;
        out_dst->result = out_src->result;

        //check it
        const range_t r = lmk05318_get_freq_range(out_dst);
        const double f = out_dst->result.freq;
        const bool is_freq_ok = f >= r.min && f <= r.max;
        if(!is_freq_ok)
            complete_solution_check = false;

        USDR_LOG("5318", is_freq_ok ? USDR_LOG_DEBUG : USDR_LOG_ERROR, "port:%d solved [OD:%" PRIu64 " freq:%.8f mux:%d(%s) fmt:%u(%s)] %s",
                 out_dst->port, out_dst->result.out_div, out_dst->result.freq, out_dst->result.mux,
                 lmk05318_decode_mux(out_dst->result.mux), out_dst->wanted.type, lmk05318_decode_fmt(out_dst->wanted.type),
                 is_freq_ok ? "**OK**" : "**BAD**");
    }

    if(complete_solution_check == false)
        return -EINVAL;

    if(d)
    {
        for(unsigned i = 0; i < n_outs; ++i)
        {
            const lmk05318_out_config_t* out = _outs + i;
            d->outputs[out->port].freq = out->result.freq;
            d->outputs[out->port].odiv = out->result.out_div;
            d->outputs[out->port].mux  = out->result.mux;
        }
    }

    //update hw registers
    if(!dry_run)
    {
        //tune APLL2
        int res = lmk05318_tune_apll2_ex(d);
        if(res)
        {
            USDR_LOG("5318", USDR_LOG_ERROR, "error %d tuning APLL2", res);
            return res;
        }

        //set output ports
        for(unsigned i = 0; i < n_outs; ++i)
        {
            const lmk05318_out_config_t* out = _outs + i;

            if(out->wanted.freq == 0)
                continue;

            USDR_LOG("5318", USDR_LOG_DEBUG, "OUT%u port:%u div:%" PRIu64 " fmt:%u(%s)",
                     i, out->port, out->result.out_div, out->wanted.type, lmk05318_decode_fmt(out->wanted.type));

            res =             lmk05318_set_out_mux_ex(d, out->port, out->result.mux, out->wanted.type);
            res = res ? res : lmk05318_set_out_div(d, out->port, out->result.out_div);
            if(res)
            {
                USDR_LOG("5318", USDR_LOG_ERROR, "error %d setting hw parameters for port#%d", res, out->port);
                return res;
            }
        }
    }

    return 0;
}

static inline void lmk05318_decode_los_mask(unsigned m, char* s)
{
    if(!s)
        return;

    unsigned len = 0;
    *s = 0;

    if(m & LMK05318_LOS_XO)
        len += sprintf(s + len, "%s ", "XO");
    if(m & LMK05318_LOL_PLL1)
        len += sprintf(s + len, "%s ", "PLL1");
    if(m & LMK05318_LOL_PLL2)
        len += sprintf(s + len, "%s ", "PLL2");
    if(m & LMK05318_LOS_FDET_XO)
        len += sprintf(s + len, "%s ", "XO_FDET");
    if(m & LMK05318_LOPL_DPLL)
        len += sprintf(s + len, "%s ", "DPLL_P");
    if(m & LMK05318_LOFL_DPLL)
        len += sprintf(s + len, "%s ", "DPLL_F");
    if(m & LMK05318_BAW_LOCK)
        len += sprintf(s + len, "%s ", "BAW");
}

int lmk05318_check_lock(lmk05318_state_t* d, unsigned* los_msk, bool silent)
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
             ((los[1] & LOFL_DPLL_POL_MSK) ? LMK05318_LOFL_DPLL : 0) |
             ((los[2] & BAW_LOCK_MSK) ? LMK05318_BAW_LOCK : 0);

    if(!silent)
    {
        char ss[255];
        lmk05318_decode_los_mask(losval, ss);
        USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 LOS_MASK=[%s] %02x:%02x:%02x\n", ss, los[0], los[1], los[2]);
    }

    *los_msk = losval;
    return 0;
}

int lmk05318_wait_apll1_lock(lmk05318_state_t* d, unsigned timeout)
{
    int res = 0;
    unsigned elapsed = 0;
    bool locked = false;
    uint8_t reg;
    unsigned los_msk;
    bool pll1_vm_inside;

    while(timeout == 0 || elapsed < timeout)
    {
        res = lmk05318_reg_rd(d, PLL1_CALSTAT1, &reg);
        if(res)
        {
            USDR_LOG("SYNC", USDR_LOG_ERROR, "LMK05318 read(PLL1_CALSTAT1) error:%d", res);
            return res;
        }
        pll1_vm_inside = reg & PLL1_VM_INSIDE_MSK;

        res = lmk05318_check_lock(d, &los_msk, true/*silent*/);
        if(res)
        {
            USDR_LOG("SYNC", USDR_LOG_ERROR, "LMK05318 lmk05318_check_lock() error:%d", res);
            return res;
        }

        if(d->dpll.enabled)
        {
            locked = pll1_vm_inside && !(los_msk & LMK05318_LOPL_DPLL) && !(los_msk & LMK05318_LOFL_DPLL);
        }
        else
        {
            locked = pll1_vm_inside && (los_msk & LMK05318_BAW_LOCK);
        }

        if(locked)
            break;

        usleep(100);
        elapsed += 100;
    }

    if(!locked)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "APLL1 is not locked! [PLL1_CALSTAT1:%u PLL1_VM_INSIDE:0x%02x LOS_MASK:0x%02x LMK05318_BAW_LOCK:%u]",
                 reg, pll1_vm_inside ? 1 : 0, los_msk,(los_msk & LMK05318_BAW_LOCK) ? 1 : 0);

        return -ETIMEDOUT;
    }

    USDR_LOG("5318", USDR_LOG_INFO, "APLL1 locked OK");
    return 0;
}

int lmk05318_wait_apll2_lock(lmk05318_state_t* d, unsigned timeout)
{
    int res = 0;
    unsigned elapsed = 0;
    bool locked = false;
    uint8_t reg;
    unsigned los_msk;
    bool pll2_vm_inside;

    while(timeout == 0 || elapsed < timeout)
    {
        res = lmk05318_reg_rd(d, PLL2_CALSTAT1, &reg);
        if(res)
        {
            USDR_LOG("SYNC", USDR_LOG_ERROR, "LMK05318 read(PLL2_CALSTAT1) error:%d", res);
            return res;
        }
        pll2_vm_inside = reg & PLL2_VM_INSIDE_MSK;

        res = lmk05318_check_lock(d, &los_msk, true/*silent*/);
        if(res)
        {
            USDR_LOG("SYNC", USDR_LOG_ERROR, "LMK05318 lmk05318_check_lock() error:%d", res);
            return res;
        }

        locked = pll2_vm_inside && !(los_msk & LMK05318_LOL_PLL2);

        if(locked)
            break;

        usleep(100);
        elapsed += 100;
    }

    if(!locked)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "APLL2 is not locked! [PLL2_CALSTAT1:%u PLL2_VM_INSIDE:0x%02x LOS_MASK:0x%02x LMK05318_LOL_PLL2:%u]",
                 reg, pll2_vm_inside ? 1 : 0, los_msk,(los_msk & LMK05318_LOL_PLL2) ? 1 : 0);
        return -ETIMEDOUT;
    }

    USDR_LOG("5318", USDR_LOG_INFO, "APLL2 locked OK");
    return 0;
}

int lmk05318_wait_dpll_ref_stat(lmk05318_state_t* d, unsigned timeout)
{
    if(!d->dpll.enabled)
    {
        USDR_LOG("5318", USDR_LOG_DEBUG, "DPLL disabled, validating ref ignored");
        return 0;
    }

    int res = 0;
    unsigned elapsed = 0;
    bool valid = false;
    uint8_t reg, reg2;

    while(timeout == 0 || elapsed < timeout)
    {
        res = lmk05318_reg_rd(d, REFVALSTAT, &reg);
        res = res ? res : lmk05318_reg_rd(d, 0xa7, &reg2);
        if(res)
        {
            USDR_LOG("SYNC", USDR_LOG_ERROR, "LMK05318 read(REFVALSTAT) error:%d", res);
            return res;
        }

        bool pri_valid = d->dpll.ref_en[LMK05318_PRIREF] ? (reg & PRIREF_VALSTAT_MSK) : true;
        bool sec_valid = d->dpll.ref_en[LMK05318_SECREF] ? (reg & SECREF_VALSTAT_MSK) : true;
        valid = pri_valid && sec_valid;

        if(valid)
            break;

        usleep(100);
        elapsed += 100;
    }

    if(!valid)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "DPLL input reference NOT VALID! PRIREF_VALSTAT:%u SECREF_VALSTAT:%u R167:0x%02x",
                    reg & PRIREF_VALSTAT_MSK, reg & SECREF_VALSTAT_MSK, reg2);
        return -ETIMEDOUT;
    }

    USDR_LOG("5318", USDR_LOG_INFO, "DPLL input reference is valid");
    return 0;
}
