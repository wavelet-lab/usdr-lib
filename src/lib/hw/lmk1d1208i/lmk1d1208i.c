// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>

#include "def_lmk1d1208i.h"
#include "lmk1d1208i.h"
#include "usdr_logging.h"


static int lmk1d1208i_reg_wr(lmk1d1208i_state_t* d, uint8_t reg, uint8_t out)
{
    uint8_t data[2] = { reg, out };
    return lowlevel_ls_op(d->dev, d->subdev,
                          USDR_LSOP_I2C_DEV, d->lsaddr,
                          0, NULL, 2, data);
}

static int lmk1d1208i_reg_rd(lmk1d1208i_state_t* d, uint8_t reg, uint8_t* val)
{
    uint8_t addr[1] = { reg };
    return lowlevel_ls_op(d->dev, d->subdev,
                          USDR_LSOP_I2C_DEV, d->lsaddr,
                          1, val, 1, addr);
}

static int lmk1d1208i_reg_print(const uint16_t* regs, unsigned count)
{
    for (unsigned j = 0; j < count; j++)
    {
        uint8_t addr = regs[j] >> 8;
        uint8_t data = regs[j];
        USDR_LOG("1208", USDR_LOG_DEBUG, "WRITING REG R%02u -> 0x%02x [0x%04x]", addr, data, regs[j]);
    }
    return 0;
}

static int lmk1d1208i_reg_wr_n(lmk1d1208i_state_t* d, const uint16_t* regs, unsigned count)
{
    int res;
    //
    lmk1d1208i_reg_print(regs, count);
    //
    for (unsigned j = 0; j < count; j++)
    {
        uint8_t addr = regs[j] >> 8;
        uint8_t data = regs[j];

        res = lmk1d1208i_reg_wr(d, addr, data);
        if (res)
            return res;
    }

    return 0;
}

int lmk1d1208i_create(lldev_t dev, unsigned subdev, unsigned lsaddr, const lmk1d1208i_config_t* cfg,
                      lmk1d1208i_state_t* st)
{
    int res;
    memset(st, 0, sizeof(lmk1d1208i_state_t));

    st->dev = dev;
    st->subdev = subdev;
    st->lsaddr = lsaddr;

    uint8_t r5;
    res = lmk1d1208i_reg_rd(st, R5, &r5);
    if(res)
    {
        USDR_LOG("1208", USDR_LOG_ERROR, "lmk1d1208i_reg_rd() error:%d", res);
        return res;
    }

    const uint8_t rev_id = (r5 & REV_ID_MSK) >> REV_ID_OFF;
    const uint8_t dev_id = (r5 & DEV_ID_MSK) >> DEV_ID_OFF;
    USDR_LOG("1208", USDR_LOG_DEBUG, "REV_ID:0x%01x DEV_ID:0x%01x", rev_id, dev_id);

    if(rev_id != 0x2 && dev_id != 0x0)
    {
        USDR_LOG("1208", USDR_LOG_ERROR, "1D1208I chip/bus not found");
        return -EINVAL;
    }

    uint16_t regs[] =
    {
        MAKE_LMK1D1208I_R0(cfg->out[7].enabled ? OUT7_EN_OUTPUT_ENABLED : OUT7_EN_OUTPUT_DISABLED_HI_Z,
                           cfg->out[6].enabled ? OUT6_EN_OUTPUT_ENABLED : OUT6_EN_OUTPUT_DISABLED_HI_Z,
                           cfg->out[5].enabled ? OUT5_EN_OUTPUT_ENABLED : OUT5_EN_OUTPUT_DISABLED_HI_Z,
                           cfg->out[4].enabled ? OUT4_EN_OUTPUT_ENABLED : OUT4_EN_OUTPUT_DISABLED_HI_Z,
                           cfg->out[3].enabled ? OUT3_EN_OUTPUT_ENABLED : OUT3_EN_OUTPUT_DISABLED_HI_Z,
                           cfg->out[2].enabled ? OUT2_EN_OUTPUT_ENABLED : OUT2_EN_OUTPUT_DISABLED_HI_Z,
                           cfg->out[1].enabled ? OUT1_EN_OUTPUT_ENABLED : OUT1_EN_OUTPUT_DISABLED_HI_Z,
                           cfg->out[0].enabled ? OUT0_EN_OUTPUT_ENABLED : OUT0_EN_OUTPUT_DISABLED_HI_Z
                           ),
        MAKE_LMK1D1208I_R1(cfg->out[7].amp == LMK1D1208I_BOOSTED_LVDS ? OUT7_AMP_SEL_BOOSTED_LVDS_SWING_500_MV : OUT7_AMP_SEL_STANDARD_LVDS_SWING_350_MV,
                           cfg->out[6].amp == LMK1D1208I_BOOSTED_LVDS ? OUT6_AMP_SEL_BOOSTED_LVDS_SWING_500_MV : OUT6_AMP_SEL_STANDARD_LVDS_SWING_350_MV,
                           cfg->out[5].amp == LMK1D1208I_BOOSTED_LVDS ? OUT5_AMP_SEL_BOOSTED_LVDS_SWING_500_MV : OUT5_AMP_SEL_STANDARD_LVDS_SWING_350_MV,
                           cfg->out[4].amp == LMK1D1208I_BOOSTED_LVDS ? OUT4_AMP_SEL_BOOSTED_LVDS_SWING_500_MV : OUT4_AMP_SEL_STANDARD_LVDS_SWING_350_MV,
                           cfg->out[3].amp == LMK1D1208I_BOOSTED_LVDS ? OUT3_AMP_SEL_BOOSTED_LVDS_SWING_500_MV : OUT3_AMP_SEL_STANDARD_LVDS_SWING_350_MV,
                           cfg->out[2].amp == LMK1D1208I_BOOSTED_LVDS ? OUT2_AMP_SEL_BOOSTED_LVDS_SWING_500_MV : OUT2_AMP_SEL_STANDARD_LVDS_SWING_350_MV,
                           cfg->out[1].amp == LMK1D1208I_BOOSTED_LVDS ? OUT1_AMP_SEL_BOOSTED_LVDS_SWING_500_MV : OUT1_AMP_SEL_STANDARD_LVDS_SWING_350_MV,
                           cfg->out[0].amp == LMK1D1208I_BOOSTED_LVDS ? OUT0_AMP_SEL_BOOSTED_LVDS_SWING_500_MV : OUT0_AMP_SEL_STANDARD_LVDS_SWING_350_MV
                           ),
        MAKE_LMK1D1208I_R2(1, 1, /*reserved*/
                           cfg->bank[1].sel == LMK1D1208I_IN0 ? BANK1_IN_SEL_IN0_PDIVIN0_N : BANK1_IN_SEL_IN1_PDIVIN1_N,
                           cfg->bank[0].sel == LMK1D1208I_IN0 ? BANK0_IN_SEL_IN0_PDIVIN0_N : BANK0_IN_SEL_IN1_PDIVIN1_N,
                           cfg->bank[1].mute ? BANK1_MUTE_LOGIC_LOW : BANK1_MUTE_INX_PDIVINX_N,
                           cfg->bank[0].mute ? BANK0_MUTE_LOGIC_LOW : BANK0_MUTE_INX_PDIVINX_N,
                           cfg->in[1].enabled ? IN1_EN_INPUT_ENABLED : IN1_EN_INPUT_DISABLED_REDUCES_POWER_CONSUMPTION,
                           cfg->in[0].enabled ? IN0_EN_INPUT_ENABLED : IN0_EN_INPUT_DISABLED_REDUCES_POWER_CONSUMPTION
                           ),
    };

    res = lmk1d1208i_reg_wr_n(st, regs, SIZEOF_ARRAY(regs));
    if(res)
    {
        USDR_LOG("1208", USDR_LOG_ERROR, "lmk1d1208i_reg_wr_n() error:%d", res);
        return res;
    }

    USDR_LOG("1208", USDR_LOG_DEBUG, "Create OK");
    return 0;
}

int lmk1d1208i_destroy(lmk1d1208i_state_t* st)
{
    USDR_LOG("1208", USDR_LOG_DEBUG, "Destroy OK");
    return 0;
}


