// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "lms8001.h"
#include "def_lms8001.h"

#include <usdr_logging.h>
#include <string.h>

enum lms8_vco_params {
    LMS8_VCO1_MIN = 4400000000ULL,
    LMS8_VCO1_MAX = 6600000000ULL,

    LMS8_VCO2_MIN = 6200000000ULL,
    LMS8_VCO2_MAX = 8300000000ULL,

    LMS8_VCO3_MIN = 7700000000ULL,
    LMS8_VCO3_MAX = 10400000000ULL,
};

enum {
    LMS_LDO_1P25 = 101,
};

static int lms8001_spi_post(lms8001_state_t* obj, uint32_t* regs, unsigned count)
{
    int res;
    for (unsigned i = 0; i < count; i++) {
        res = lowlevel_spi_tr32(obj->dev, obj->subdev, obj->lsaddr, regs[i], NULL);
        if (res)
            return res;

        USDR_LOG("8001", USDR_LOG_INFO, "[%d/%d] reg wr %08x\n", i, count, regs[i]);
    }

    return 0;
}

// TODO: Add profile
int lms8001_tune(lms8001_state_t* state, unsigned fref, uint64_t out)
{
    if (out > LMS8_VCO3_MAX) {
        return -EINVAL;
    }

    int res;
    uint64_t vco;
    unsigned divi = 0;

    for (divi = 0; divi < 4; divi++) {
        vco = out << divi;
        if (vco > LMS8_VCO1_MIN)
            break;
    }
    if (divi == 4) {
        USDR_LOG("8001", USDR_LOG_WARNING, "Can't deliver LO %.3f Mhz!\n", out / 1.0e6);
        return -EINVAL;
    }

    uint64_t nint = vco / fref;
    uint64_t frac = (vco - nint * fref) * ((uint64_t)1 << 20) / fref;

    if (nint > 1023) {
        return -EINVAL;
    }

    USDR_LOG("8001", USDR_LOG_ERROR, "OUT=%.3f VCO=%.3f PLL NINT=%d FRAC=%d DIV=%d\n",
             out  / 1.0e6, vco / 1.0e6, (unsigned)nint, (unsigned)frac, (1 << divi));

    // TODO: Prescaler DIV
    uint32_t pll_regs[] = {
        MAKE_LMS8001_PLL_PROFILE_0_PLL_LPF_CFG1_n(1, 1, 8, 8),
        MAKE_LMS8001_PLL_PROFILE_0_PLL_LPF_CFG2_n(1, 0, 8),

        //MAKE_LMS8001_PLL_PROFILE_0_PLL_CP_CFG0_n(0, 0, 4, 0),
        //MAKE_LMS8001_PLL_PROFILE_0_PLL_CP_CFG1_n(2, 16),

        MAKE_LMS8001_PLL_PROFILE_0_PLL_CP_CFG0_n(0, 0, 2, 1),
        MAKE_LMS8001_PLL_PROFILE_0_PLL_CP_CFG1_n(2, 5),

        MAKE_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_n(0, 1, 2, 3, 1),
        MAKE_LMS8001_PLL_PROFILE_0_PLL_FF_CFG_n(divi == 0 ? 0 : 1, divi, divi),

        MAKE_LMS8001_PLL_PROFILE_0_PLL_SDM_CFG_n(frac == 0 ? 1 : 0, 1, 0, 0, nint),

        MAKE_LMS8001_PLL_PROFILE_0_PLL_FRACMODL_n(frac),
        MAKE_LMS8001_PLL_PROFILE_0_PLL_FRACMODH_n(frac >> 16),

        // Auto calibration
        MAKE_LMS8001_PLL_CONFIGURATION_PLL_CFG(1, 1, 0, 1, 1, 0, 0),
        MAKE_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO1(0, 1, 7, 0),

        // Enable (divider disabled)
        MAKE_LMS8001_PLL_PROFILE_0_PLL_ENABLE_n(1, 0, 1, 1, 1, 1, 1, 1, divi == 0 ? 0 : 1, 0, 1, 1, 1),

        // Start auto calibration
        MAKE_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO0(1, 0, 0, 0, 0),

        // LO dist settings
        MAKE_LMS8001_PLL_PROFILE_0_PLL_LODIST_CFG_n(state->chan_mask, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0),
    };

    res = lms8001_spi_post(state, pll_regs, SIZEOF_ARRAY(pll_regs));
    return res;
}

int lms8001_ch_enable(lms8001_state_t* state, unsigned mask)
{
    int res;
    unsigned e[] = { (mask >> 0) & 1, (mask >> 1) & 1, (mask >> 2) & 1, (mask >> 3) & 1 };

    state->chan_mask = mask;

    uint32_t en_regs[] = {
        MAKE_LMS8001_BIASLDOCONFIG_LOBUFA_LDO_Config(0, 0, e[0], LMS_LDO_1P25),
        MAKE_LMS8001_BIASLDOCONFIG_LOBUFB_LDO_Config(0, 0, e[1], LMS_LDO_1P25),
        MAKE_LMS8001_BIASLDOCONFIG_LOBUFC_LDO_Config(0, 0, e[2], LMS_LDO_1P25),
        MAKE_LMS8001_BIASLDOCONFIG_LOBUFD_LDO_Config(0, 0, e[3], LMS_LDO_1P25),

        MAKE_LMS8001_BIASLDOCONFIG_HFLNAA_LDO_Config(0, 0, e[0], LMS_LDO_1P25),
        MAKE_LMS8001_BIASLDOCONFIG_HFLNAB_LDO_Config(0, 0, e[1], LMS_LDO_1P25),
        MAKE_LMS8001_BIASLDOCONFIG_HFLNAC_LDO_Config(0, 0, e[2], LMS_LDO_1P25),
        MAKE_LMS8001_BIASLDOCONFIG_HFLNAD_LDO_Config(0, 0, e[3], LMS_LDO_1P25),

        MAKE_LMS8001_HLMIXA_HLMIXx_CONFIG0(64, 16, !e[0], !e[0]),
        MAKE_LMS8001_HLMIXB_HLMIXx_CONFIG0(64, 16, !e[1], !e[1]),
        MAKE_LMS8001_HLMIXC_HLMIXx_CONFIG0(64, 16, !e[2], !e[2]),
        MAKE_LMS8001_HLMIXD_HLMIXx_CONFIG0(64, 16, !e[3], !e[3]),
    };

    res = lms8001_spi_post(state, en_regs, SIZEOF_ARRAY(en_regs));
    return res;
}

int lms8001_create(lldev_t dev, unsigned subdev, unsigned lsaddr, lms8001_state_t *out)
{
    int res;

    uint32_t lms_init[] = {
        MAKE_LMS8001_CHIPCONFIG_SPIConfig(1, 1, 1, 1, 1, 1, 1),

        MAKE_LMS8001_BIASLDOCONFIG_CLK_BUF_LDO_Config(0, 0, 1, LMS_LDO_1P25),
        MAKE_LMS8001_BIASLDOCONFIG_PLL_DIV_LDO_Config(0, 0, 1, LMS_LDO_1P25),
        MAKE_LMS8001_BIASLDOCONFIG_PLL_CP_LDO_Config(0, 0, 1, LMS_LDO_1P25),

        MAKE_LMS8001_PLL_CONFIGURATION_PLL_VREG(1, 0, 1, 1, 32),
        MAKE_LMS8001_PLL_CONFIGURATION_PLL_CFG_XBUF(1, 0, 1),
    };

    out->dev = dev;
    out->subdev = subdev;
    out->lsaddr = lsaddr;

    res = lms8001_spi_post(out, lms_init, SIZEOF_ARRAY(lms_init));

    // Move away!
    res = res ? res : lms8001_ch_enable(out, 0xff);
    return res;
}

int lms8001_destroy(lms8001_state_t* m)
{
    return 0;
}
