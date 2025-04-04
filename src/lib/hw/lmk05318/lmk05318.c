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
    APLL2_PDIV_COUNT = 2, //PD1 & PD2
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

static int lmk05318_reg_wr_from_map(lmk05318_state_t* d, bool dry_run)
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




static int lmk05318_softreset(lmk05318_state_t* out)
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


static int lmk05318_init(lmk05318_state_t* d, bool dpllmode)
{
    uint32_t regs[] =
    {
        MAKE_LMK05318_DEV_CTL(0, 0, dpllmode ? 1 : 0, 1, 1, 1, 1),   //R12   set APLL1 mode - DPLL | Free-run
        MAKE_LMK05318_DPLL_GEN_CTL(0, 0, 0, 0, 0, dpllmode ? 1 : 0), //R252  enable/disable DPLL
        MAKE_LMK05318_SPARE_NVMBASE2_BY2(0, dpllmode ? 0 : 1),       //R39 ***  set fixed APLL1 denumerator for DPLL en, programmed den otherwise
        MAKE_LMK05318_SPARE_NVMBASE2_BY1(0, 0, 0),                   //R40 ***  set fixed APPL2 denumerator always
        MAKE_LMK05318_PLL_CLK_CFG(0, 0b111),                         //R47   set PLL clock cfg
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
    };

    return lmk05318_add_reg_to_map(d, regs, SIZEOF_ARRAY(regs));
}

#if 0

#undef LMK05380_DPLL_EN

static int lmk05318_init(lmk05318_state_t* d)
{
    uint32_t regs[] =
    {
        MAKE_LMK05318_DEV_CTL(0,0,0,1,1),                //R12 +
        MAKE_LMK05318_INT_FLAG_POL0(1,1,1,1),            //R17 +
        MAKE_LMK05318_INT_FLAG_POL1(1,1,1,1,1,1,1,1),    //R18 +
        MAKE_LMK05318_INT_FLAG0(0,1,0,0),                //R19
        MAKE_LMK05318_INT_FLAG1(0,0,1,0,0,0,0,0),        //R20
        MAKE_LMK05318_MUTELVL1(1,1,1,1),                 //R23
        MAKE_LMK05318_MUTELVL2(1,1,1,1),                 //R24
        MAKE_LMK05318_OUT_MUTE(0,0,0,0,0,0,0,0),         //R25 +
        MAKE_LMK05318_GPIO_OUT(1,1),                     //R36 +
        MAKE_LMK05318_SPARE_NVMBASE2_BY2(1,0),           //R39 +
        MAKE_LMK05318_SPARE_NVMBASE2_BY1(1,1,0),         //R40 +
        MAKE_LMK05318_REF_CLKCTL1(0,0,1,0),              //R45 +
        MAKE_LMK05318_REF_CLKCTL2(SECREF_TYPE_AC_DIFF_EXT, PRIREF_TYPE_AC_DIFF_EXT),    //R46 +
        MAKE_LMK05318_PLL_CLK_CFG(0, 0b111),             //R47 +++++++++++++
        MAKE_LMK05318_STAT0_SEL(0x50),                   //R48 +
        MAKE_LMK05318_STAT1_SEL(0x4a),                   //R49 +
        MAKE_LMK05318_PREDRIVER(0x08),                   //R68 +
        MAKE_LMK05318_BAW_LOCKDET_PPM_MAX_BY1(1,0),      //R80
        MAKE_LMK05318_BAW_LOCKDET_PPM_MAX_BY0(0x0a),     //R81
        0x005200,  //R82 BAW LOCKDET/UNLOCKDET begin
        0x005307,
        0x00549E,
        0x005500,
        0x005600,
        0x00571E,
        0x005884,
        0x005980,
        0x005A00,
        0x005B14,
        0x005C00,
        0x005D07,
        0x005E9E,
        0x005F00,
        0x006000,
        0x00611E,
        0x006284,
        0x006380,  //R99 BAW LOCKDET/UNLOCKDET end
        MAKE_LMK05318_PLL2_CTRL1(0x01),        //R101 PLL2 charge pump drain 0x1=3.2mA [default 4.8mA] +
        MAKE_LMK05318_PLL2_CTRL4(0x1F),        //R104 PLL2 bleed resisror 0x1F=766.96 Ohm [default open]+
        MAKE_LMK05318_PLL2_CALCTRL0(0x01),     //R105 0x1 = VCO calibration time per step (up to 7 steps) 0x1=3ms [default 0.3ms]+
        MAKE_LMK05318_PLL1_MASHCTRL(0, 0x03),  //R115 0x3 = APLL1 mash_order3 [default 0 (int mode)]+
        MAKE_LMK05318_PLL1_LF_R2(0x01),        //R129 0x1 = APLL1 Loop filter R2=414 Ohm [default 9650]+
        MAKE_LMK05318_PLL1_LF_R3(0x01),        //R131 0x1 = APLL1 Loop filter R3=200 Ohm [default 2400] +
        MAKE_LMK05318_PLL1_LF_R4(0x01),        //R132 0x1 = APLL1 Loop filter R4=200 Ohm [default 2400]+
        MAKE_LMK05318_PLL2_MASHCTRL(0, 0x03),  //R139 0x3 = APLL2 mash_order3 [default 0 (int mode)]+
        MAKE_LMK05318_PLL2_LF_R2(0x02),        //R140 APLL2 Loop Filter R2=300 Ohm [default 1867]+
        MAKE_LMK05318_PLL2_LF_R3(0x01),        //R142 APLL2 Loop filter R3=200 Ohm [default 2400]+
        MAKE_LMK05318_PLL2_LF_R4(0x01),        //R143 APLL2 Loop filter R4=200 Ohm [default 2400]+
        MAKE_LMK05318_PLL2_LF_C3C4(0x7, 0x7),  //R144 APLL2 Loop Filter C3 = 70pF, C4 = 70pF [defailt C3=0(open), C4=0(open)]+
        MAKE_LMK05318_XO_OFFSET_SW_TIMER(0x1), //R145 XO input wait timer 3.3ms [default 52.4ms]+
        0x00A0FC, //R160 MEMADR ?

#ifdef LMK05380_DPLL_EN
        0x00B9F5, //R185 DPLL_REF settings ???
        0x00BA01, //R186 DPLL REF Tuning history timer ???
#endif
        MAKE_LMK05318_REF01_DETAMP(1,1,0,0),        //R192
        MAKE_LMK05318_REF0_DETEN(0,1,0,0,0,0),      //R193
        MAKE_LMK05318_REF0_MISSCLK_DIV_BY0(0x00),   //R195
        MAKE_LMK05318_REF0_MISSCLK_DIV_BY1(0x00),   //R196
        MAKE_LMK05318_REF0_MISSCLK_DIV_BY2(0x1d),   //R197
        MAKE_LMK05318_REF1_MISSCLK_DIV_BY0(0x00),   //R198
        MAKE_LMK05318_REF1_MISSCLK_DIV_BY1(0x00),   //R199
        MAKE_LMK05318_REF1_MISSCLK_DIV_BY2(0x1d),   //R200
        MAKE_LMK05318_REF0_EARLY_CLK_DIV_BY2(0x00), //R202
        MAKE_LMK05318_REF0_EARLY_CLK_DIV_BY2(0x00), //R203
        MAKE_LMK05318_REF0_EARLY_CLK_DIV_BY2(0x15), //R204
        MAKE_LMK05318_REF1_EARLY_CLK_DIV_BY2(0x00), //R205
        MAKE_LMK05318_REF1_EARLY_CLK_DIV_BY2(0x00), //R206
        MAKE_LMK05318_REF1_EARLY_CLK_DIV_BY2(0x15), //R207
        0x00D000, //R208 REF0/1 settings begin
        0x00D114,
        0x00D200,
        0x00D316,
        0x00D400,
        0x00D514,
        0x00D600,
        0x00D716,
        0x00D900,
        0x00DA00,
        0x00DB19,
        0x00DC6E,
        0x00DD00,
        0x00DE03,
        0x00DF0D,
        0x00E047,
        0x00E100,
        0x00E200,
        0x00E319,
        0x00E46E,
        0x00E500,
        0x00E603,
        0x00E70D,
        0x00E847,
        0x00E90A,
        0x00EA0A, //R234 REF0/1 settings end
        MAKE_LMK05318_REF0_PH_VALID_CNT_BY0(0x01),  //R235
        MAKE_LMK05318_REF0_PH_VALID_CNT_BY1(0x8c),  //R236
        MAKE_LMK05318_REF0_PH_VALID_CNT_BY2(0xba),  //R237
        MAKE_LMK05318_REF0_PH_VALID_CNT_BY3(0x80),  //R238
        MAKE_LMK05318_REF1_PH_VALID_CNT_BY0(0x00),  //R239
        MAKE_LMK05318_REF1_PH_VALID_CNT_BY1(0xc3),  //R240
        MAKE_LMK05318_REF1_PH_VALID_CNT_BY2(0x50),  //R241
        MAKE_LMK05318_REF1_PH_VALID_CNT_BY3(0x00),  //R242
        MAKE_LMK05318_REF0_PH_VALID_THR(0x3f),      //R243
        MAKE_LMK05318_REF1_PH_VALID_THR(0x00),      //R244

#ifdef LMK05380_DPLL_EN
        MAKE_LMK05318_DPLL_REF01_PRTY(2,1), //R249 DPLL ref priority???
        MAKE_LMK05318_DPLL_REF_SWMODE(0,0,3), //R251 DPLL sw ctrls ???
        MAKE_LMK05318_DPLL_GEN_CTL(0,1,0,1,1), //R252 DPLL ctrls ???
        MAKE_LMK05318_DPLL_REF0_RDIV_BY0(0), //R256 DPLL RDIV ???
        MAKE_LMK05318_DPLL_REF0_RDIV_BY1(1), //R257 DPLL RDIV ???
        MAKE_LMK05318_DPLL_REF1_RDIV_BY0(0), //R258 ???
        MAKE_LMK05318_DPLL_REF1_RDIV_BY1(0), //R259 ???
        MAKE_LMK05318_DPLL_REF_TDC_CTL(1), //R260 DPLL cycle slip ???
        0x010580,
        0x010601,
        0x01072A,
        0x010805,
        0x0109F2,
        0x010A00,
        0x010BA0,
        0x010C04,
        0x010D00,
        0x010E02,
        0x010F8C,
        0x011000,
        0x011100,
        0x011200,
        0x011316,
        0x011416,
        0x011516,
        0x011600,
        0x011700,
        0x011800,
        0x011900,
        0x011A00,
        0x011B00,
        0x011C1E,
        0x011D1E,
        0x011E00,
        0x011F00,
        0x012000,
        0x012100,
        0x012203,
        0x012322,
        0x012409,
        0x012501,
        0x012600,
        0x01272C,
        0x012809,
        0x012909,
        0x012A09,
        0x012B01,
        0x012C00,
        0x012D1B,
        0x012E1E,
        0x012F01,
        0x01300F,
        0x013104,
        0x013261,
        0x0133F8,
        0x013443,
        0x0135C3,
        0x0136C3,
        0x0137C3,
        0x0138C3,
        0x0139C3,
        0x013AFF,
        0x013BFF,
        0x013CFF,
        0x013DFF,
        0x013EFF,
        0x013F03,
        0x014000,
        0x01410A,
        0x014200,
        0x014300,
        0x014400,
        0x014501,
        0x014606,
        0x014735,
        0x014875,
        0x01490B,
        0x014A00,
        0x014B64,
        0x014C00,   //R332 ^^DPLL ???
        0x015000,
        0x015198,
        0x015296,   //R338 DPLL ??
        0x015400,
        0x015500,
        0x015600,
        0x015700,
        0x015800,
        0x015900,
        0x015A02,
        0x015B00,
        0x015C00,
        0x015D00,
        0x015E00,
        0x015F00,   //R352 DPLL ???
#endif
    };

    int res = lmk05318_reg_wr_n(d, regs, SIZEOF_ARRAY(regs));
    if (res)
        return res;

    return 0;
}
#endif

int lmk05318_create_ex(lldev_t dev, unsigned subdev, unsigned lsaddr,
                       const lmk05318_xo_settings_t* xo, bool dpll_mode,
                       lmk05318_out_config_t* out_ports_cfg, unsigned out_ports_len,
                       lmk05318_state_t* out, bool dry_run)
{
    int res;
    uint8_t dummy[4] = {0,0,0,0};

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
    res = lmk05318_reg_wr_n(out, lmk05318_rom_test, SIZEOF_ARRAY(lmk05318_rom_test));
    if (res)
        return res;

    return 0;
#endif

#if 1
    res = lmk05318_init(out, dpll_mode);
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

    res = lmk05318_tune_apll1(out, dpll_mode);
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

    //res = dry_run ? 0 : lmk05318_softreset(out);
    if(res)
    {
        USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 error %d lmk05318_softreset()", res);
        return res;
    }

    USDR_LOG("5318", USDR_LOG_INFO, "LMK05318 initialized\n");
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

static int lmk05318_tune_apll2_ex(lmk05318_state_t* d, uint64_t fvco2, unsigned pd1, unsigned pd2)
{
    unsigned fref = VCO_APLL1 / d->fref_pll2_div_rp / d->fref_pll2_div_rs;
    if (fref < APLL2_PD_MIN || fref > APLL2_PD_MAX) {
        USDR_LOG("5318", USDR_LOG_ERROR, "LMK05318 APLL2 PFD should be in range [%" PRIu64 ";%" PRIu64 "] but got %d!\n",
                 (uint64_t)APLL2_PD_MIN, (uint64_t)APLL2_PD_MAX, fref);
        return -EINVAL;
    }

    if(fvco2 < VCO_APLL2_MIN || fvco2 > VCO_APLL2_MAX ||
        ((pd1 < APLL2_PDIV_MIN || pd1 > APLL2_PDIV_MAX) && (pd2 < APLL2_PDIV_MIN || pd2 > APLL2_PDIV_MAX))
        )
    {
        USDR_LOG("5318", USDR_LOG_WARNING, "LMK05318 APLL2: either FVCO2[%" PRIu64"] or (PD1[%d] && PD2[%d]) is out of range, APLL2 will be disabled",
                                          fvco2, pd1, pd2);
        // Disable
        uint32_t regs[] = {
            MAKE_LMK05318_PLL2_CTRL0(d->fref_pll2_div_rs - 1, d->fref_pll2_div_rp - 3, 1), //R100 Deactivate APLL2
        };
        return lmk05318_add_reg_to_map(d, regs, SIZEOF_ARRAY(regs));
    }

    unsigned n = fvco2 / fref;
    unsigned num = (fvco2 - n * (uint64_t)fref) * ((1ull << 24) - 1) / fref;
    int res;

    USDR_LOG("5318", USDR_LOG_INFO, "LMK05318 APLL2 FVCO=%" PRIu64 " N=%d NUM=%d PD1=%d PD2=%d\n", fvco2, n, num, pd1, pd2);

    // one of PDs may be unused (==0) -> we should fix it before registers set
    if(pd1 < APLL2_PDIV_MIN || pd1 > APLL2_PDIV_MAX)
    {
        pd1 = pd2;
    }
    else if(pd2 < APLL2_PDIV_MIN || pd2 > APLL2_PDIV_MAX)
    {
        pd2 = pd1;
    }

    uint32_t regs[] = {
        MAKE_LMK05318_PLL2_CTRL2(pd2 - 1, pd1 - 1),                                     //R102
        MAKE_LMK05318_PLL2_NDIV_BY0(n),                                                 //R135
        MAKE_LMK05318_PLL2_NDIV_BY1(n),                                                 //R134
        MAKE_LMK05318_PLL2_NUM_BY0(num),                                                //R138
        MAKE_LMK05318_PLL2_NUM_BY1(num),                                                //R137
        MAKE_LMK05318_PLL2_NUM_BY2(num),                                                //R136
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

int lmk05318_tune_apll1(lmk05318_state_t* d, bool dpll_mode)
{
    unsigned fref = (d->xo.fref / d->xo.pll1_fref_rdiv) * (d->xo.doubler_enabled ? 2 : 1);
    uint64_t fvco = VCO_APLL1;
    unsigned n = fvco / fref;

    //in DPLL mode we use FIXED 40-bit APLL1 denominator and programmed 40-bit numerator
    if(dpll_mode)
    {
        uint64_t num = (fvco - n * (uint64_t)fref) * ((1ull << 40) - 1) / fref;

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
        double frac = (double)fvco / fref - n;
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

enum {
    freq_too_low = -1,
    freq_too_high = 1,
    freq_ok = 0,
    freq_invalid = 42,
};

VWLT_ATTRIBUTE(optimize("-Ofast"))
static inline int lmk05318_get_output_divider(const lmk05318_out_config_t* cfg, uint64_t ifreq, uint64_t* div)
{
    *div = ifreq / cfg->wanted.freq;

    if(*div > cfg->max_odiv)
        return freq_too_high;

    if(*div < 1)
        return freq_too_low;

    double f = (double)ifreq / *div;

    if(f <= cfg->freq_max && f >= cfg->freq_min)
        return freq_ok;

    if(*div <= cfg->max_odiv - 1)
    {
        f = (double)ifreq / ++(*div);
        if(f <= cfg->freq_max && f >= cfg->freq_min)
            return freq_ok;
    }

    return freq_invalid;
}

//#define LMK05318_SOLVER_DEBUG

VWLT_ATTRIBUTE(optimize("-Ofast"))
static inline int lmk05318_solver_helper(lmk05318_out_config_t* outs, unsigned cnt_to_solve, uint64_t f_in,
                                         uint64_t* res_fvco2, int* res_pd1, int* res_pd2)
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
            uint64_t f_pd = f_in / pd;
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
            if(outs[rr_sec->port_idx].port == outs[rr_prim->port_idx].port)
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

        // use the first valid solution and return

        USDR_LOG("5318", USDR_LOG_DEBUG, "SOLUTION#%d valid:%d FVCO2[%" PRIu64 "; %" PRIu64 "]->", i, sol->is_valid, sol->fvco2.min, sol->fvco2.max);

        *res_fvco2 = (sol->fvco2.min + sol->fvco2.max) >> 1;
        *res_pd1 = pd1;
        *res_pd2 = pd2;

        for(int ii = 0; ii < pd_binds_count; ++ii)
        {
            const pd_bind_t* b = &pd_binds[ii];
            char tmp[1024];
            int tmp_len = sprintf(tmp, "\tPD%d=%d ports[", (ii+1), b->pd);

            for(int j = 0; j < b->ports_count; ++j)
            {
                tmp_len += sprintf(tmp + tmp_len, "%d(OD:%" PRIu64 ")),", outs[b->ports[j]].port, b->odivs[j]);
            }

            tmp_len += sprintf(tmp + tmp_len, "]");
            USDR_LOG("5318", USDR_LOG_DEBUG, "%s", tmp);

            //set results
            for(int j = 0; j < b->ports_count; ++j)
            {
                lmk05318_out_config_t* out = &outs[b->ports[j]];

                out->result.out_div = b->odivs[j];
                out->result.freq = *res_fvco2 / b->pd / b->odivs[j];
                out->result.mux = (b->pd == pd1) ? OUT_PLL_SEL_APLL2_P1 : OUT_PLL_SEL_APLL2_P2;
                out->solved = true;
            }
        }

        return 0;
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
    int pd1 = 0, pd2 = 0;
    uint64_t fvco2 = 0;

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
    const uint64_t half_band = (VCO_APLL2_MAX - VCO_APLL2_MIN) / 2;

    //first try the center
    int res = lmk05318_solver_helper(outs, cnt_to_solve, f_mid, &fvco2, &pd1, &pd2);

    //if not - do circular search
    if(res)
    {
        uint64_t step = half_band;

        //max search granularity hardcoded here
        while(step > 10000)
        {
            uint64_t n = half_band / step;

            for(uint64_t i = 1; i <= n; ++i)
            {
                res = lmk05318_solver_helper(outs, cnt_to_solve, f_mid + i * step, &fvco2, &pd1, &pd2);
                if(!res)
                {
                    step = 0;
                    break;
                }

                res = lmk05318_solver_helper(outs, cnt_to_solve, f_mid - i * step, &fvco2, &pd1, &pd2);
                if(!res)
                {
                    step = 0;
                    break;
                }
            }

            step /= 2;
        }
    }

    if(res)
        return res;


have_complete_solution:

    //if ok - update the results

    qsort(_outs, n_outs, sizeof(lmk05318_out_config_t), lmk05318_comp_port);
    bool complete_solution_check = true;

    USDR_LOG("5318", USDR_LOG_DEBUG, "=== COMPLETE SOLUTION @ VCO1:%" PRIu64 " VCO2:%" PRIu64 " PD1:%d PD2:%d ===", VCO_APLL1, fvco2, pd1, pd2);
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

        USDR_LOG("5318", is_freq_ok ? USDR_LOG_DEBUG : USDR_LOG_ERROR, "port:%d solved [OD:%" PRIu64 " freq:%.2f mux:%d(%s)] %s",
                 out_dst->port, out_dst->result.out_div, out_dst->result.freq, out_dst->result.mux,
                 lmk05318_decode_mux(out_dst->result.mux),
                 is_freq_ok ? "**OK**" : "**BAD**");
    }

    if(complete_solution_check == false)
        return -EINVAL;

    if(d)
    {
        //update params in context
        d->vco2_freq = fvco2;
        d->pd1 = pd1;
        d->pd2 = pd2;

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
        int res = lmk05318_tune_apll2_ex(d, fvco2, pd1, pd2);
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

int lmk05318_wait_apll1_lock(lmk05318_state_t* d, bool dpll_mode, unsigned timeout)
{
    int res = 0;
    unsigned elapsed = 0;
    bool locked = false;
    uint8_t reg;
    unsigned los_msk;

    while(timeout == 0 || elapsed < timeout)
    {
        res = lmk05318_reg_rd(d, PLL1_CALSTAT1, &reg);
        if(res)
        {
            USDR_LOG("SYNC", USDR_LOG_ERROR, "LMK05318 read(PLL1_CALSTAT1) error:%d", res);
            return res;
        }
        const bool pll1_vm_inside = reg & PLL1_VM_INSIDE_MSK;

        res = lmk05318_check_lock(d, &los_msk, true/*silent*/);
        if(res)
        {
            USDR_LOG("SYNC", USDR_LOG_ERROR, "LMK05318 lmk05318_check_lock() error:%d", res);
            return res;
        }

        if(dpll_mode)
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
        USDR_LOG("5318", USDR_LOG_ERROR, "APLL1 is not locked!");
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

    while(timeout == 0 || elapsed < timeout)
    {
        res = lmk05318_reg_rd(d, PLL2_CALSTAT1, &reg);
        if(res)
        {
            USDR_LOG("SYNC", USDR_LOG_ERROR, "LMK05318 read(PLL2_CALSTAT1) error:%d", res);
            return res;
        }
        const bool pll2_vm_inside = reg & PLL2_VM_INSIDE_MSK;

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
        USDR_LOG("5318", USDR_LOG_ERROR, "APLL2 is not locked!");
        return -ETIMEDOUT;
    }

    USDR_LOG("5318", USDR_LOG_INFO, "APLL2 locked OK");
    return 0;
}
