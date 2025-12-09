// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "lms8001.h"
#include "def_lms8001.h"

#include <usdr_logging.h>
#include <string.h>
#include <math.h>

enum lms8_vco_params {
    LMS8_VCO1_MIN_MPW2015 = 4150000000ULL,
    LMS8_VCO1_MIN_MPW2024 = 4550000000ULL,

    LMS8_VCO3_MAX_MPW2015 = 9150000000ULL,
    LMS8_VCO3_MAX_MPW2024 = 9650000000ULL,
};

enum {
    LMS_LDO_1P25 = 101,

    LMS_LDO_VDD_PLL_CP = 159,
    LMS_LDO_VDD_PLL_DIV = 179,
    LMS_LDO_VDD_PLL_CLKBUF = 215,

    LMS_LDO_VCO_REG = 207,
};


#define TEMPSENS_T0  -105.45
#define TEMPSENS_T1  1.2646
#define TEMPSENS_T2  -0.000548



#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) < (b)) ? (a) : (b))

static int lms8001_spi_post(lms8001_state_t* obj, uint32_t* regs, unsigned count)
{
    int res;
    for (unsigned i = 0; i < count; i++) {
        res = lowlevel_spi_tr32(obj->dev, obj->subdev, obj->lsaddr, regs[i], NULL);
        if (res)
            return res;

        USDR_LOG("8001", USDR_LOG_NOTE, "[%d/%d] reg wr %08x\n", i, count, regs[i]);
    }

    return 0;
}

static int lms8001_spi_get(lms8001_state_t* obj, uint16_t addr, uint16_t* out)
{
    uint32_t v;
    int res = lowlevel_spi_tr32(obj->dev, obj->subdev, obj->lsaddr, addr << 16, &v);
    if (res)
        return res;

    USDR_LOG("8001", USDR_LOG_NOTE, " reg rd %04x => %08x\n", addr, v);
    *out = v;
    return 0;
}



static int _lms8001_check_lo_range(lms8001_state_t* obj, uint64_t flo, bool geniq)
{
    uint64_t LMS8_VCO1_MIN = (obj->stepping == LMS8_MPW2024) ? LMS8_VCO1_MIN_MPW2024 : LMS8_VCO1_MIN_MPW2015;
    uint64_t LMS8_VCO3_MAX = (obj->stepping == LMS8_MPW2024) ? LMS8_VCO3_MAX_MPW2024 : LMS8_VCO3_MAX_MPW2015;
    uint64_t LMS8_MIN_NIQ = LMS8_VCO1_MIN / 8;
    uint64_t LMS8_MIN_IQ = LMS8_MIN_NIQ / 2;
    uint64_t LMS8_MAX_IQ = LMS8_VCO3_MAX / 2;

    if (geniq && (!((flo >= LMS8_MIN_IQ) && (flo <= LMS8_MAX_IQ)))) {
        USDR_LOG("8001", USDR_LOG_ERROR, "LO frequency should be between %d MHz and %d MHz when GenIQ is selected, requested %d Mhz\n",
                 (unsigned)(LMS8_MIN_IQ / 1000000), (unsigned)(LMS8_MAX_IQ / 1000000), (unsigned)(flo / 1000000));
        return -EINVAL;
    }

    if (!geniq && (!((flo >= LMS8_MIN_IQ) && (flo <= LMS8_VCO3_MAX)))) {
        USDR_LOG("8001", USDR_LOG_ERROR, "LO frequency should be between %d MHz and %d MHz, requested %d Mhz\n",
                 (unsigned)(LMS8_MIN_IQ / 1000000), (unsigned)(LMS8_VCO3_MAX / 1000000), (unsigned)(flo / 1000000));
        return -EINVAL;
    }

    if (!geniq && (((flo >= LMS8_MIN_IQ) && (flo <= LMS8_MIN_NIQ)))) {
        USDR_LOG("8001", USDR_LOG_ERROR, "LO frequency values between %d MHz and %d MHz can only be generated when IQ=True, requested %d Mhz\n",
                 (unsigned)(LMS8_MIN_IQ / 1000000), (unsigned)(LMS8_MIN_NIQ / 1000000), (unsigned)(flo / 1000000));
        return -EINVAL;
    }

    return 0;
}

struct lms8001_vco_settings {
    uint64_t fvco;
    unsigned divi;
};
typedef struct lms8001_vco_settings lms8001_vco_settings_t;

static int _lms8001_calc_vco(lms8001_state_t* obj, uint64_t outfreq, lms8001_vco_settings_t* ro)
{
    uint64_t LMS8_VCO1_MIN = (obj->stepping == LMS8_MPW2024) ? LMS8_VCO1_MIN_MPW2024 : LMS8_VCO1_MIN_MPW2015;
    uint64_t LMS8_VCO3_MAX = (obj->stepping == LMS8_MPW2024) ? LMS8_VCO3_MAX_MPW2024 : LMS8_VCO3_MAX_MPW2015;
    lms8001_vco_settings_t r;
    r.divi = 0;

    if (outfreq > LMS8_VCO3_MAX) {
        return -EINVAL;
    }

    for (r.divi = 0; r.divi < 4; r.divi++) {
        r.fvco = outfreq << r.divi;
        if (r.fvco > LMS8_VCO1_MIN)
            break;
    }
    if (r.divi == 4) {
        USDR_LOG("8001", USDR_LOG_WARNING, "Can't deliver LO %.3f Mhz!\n", r.fvco / 1.0e6);
        return -EINVAL;
    }

    *ro = r;
    return 0;
}

struct lms8001_pll_settings {
    unsigned nint;
    unsigned nfrac;
    unsigned nfix; // /2 prescaler activated
};
typedef struct lms8001_pll_settings lms8001_pll_settings_t;

static lms8001_pll_settings_t _lms8001_calc_pll(uint64_t fvco, unsigned fref, uint32_t tuneflags)
{
    lms8001_pll_settings_t o;
    bool int_mode = ((tuneflags & LMS8001_INT_MODE) == LMS8001_INT_MODE);
    bool pdiv2 = ((tuneflags & LMS8001_PDIV2) == LMS8001_PDIV2);

    if (pdiv2) fvco >>= 1;

    o.nfix = (pdiv2) ? 2 : 1;
    if (int_mode) {
        o.nint = (fvco + fref / 2) / fref;
        o.nfrac = 0;
    } else {
        o.nint = fvco / fref;
        o.nfrac = (fvco - (uint64_t)o.nint * fref) * ((uint64_t)1 << 20) / fref;
    }
    return o;
}

// TODO: Add profile
int lms8001_tune(lms8001_state_t* state, unsigned fref, uint64_t out)
{
    lms8001_vco_settings_t st;
    uint16_t rb;
    int res = _lms8001_calc_vco(state, out, &st);
    if (res)
        return res;

    lms8001_pll_settings_t pll = _lms8001_calc_pll(st.fvco, fref, 0);

    if (pll.nint > 1023) {
        return -EINVAL;
    }

    USDR_LOG("8001", USDR_LOG_ERROR, "OUT=%.3f VCO=%.3f PLL NINT=%d FRAC=%d DIV=%d\n",
             out / 1.0e6, st.fvco / 1.0e6, pll.nint, pll.nfrac, (1 << st.divi));

    // TODO: Prescaler DIV
    uint32_t pll_regs[] = {
        MAKE_LMS8001_PLL_PROFILE_0_PLL_LPF_CFG1_n(1, 1, 8, 8),
        MAKE_LMS8001_PLL_PROFILE_0_PLL_LPF_CFG2_n(1, 0, 8),

        //MAKE_LMS8001_PLL_PROFILE_0_PLL_CP_CFG0_n(0, 0, 4, 0),
        //MAKE_LMS8001_PLL_PROFILE_0_PLL_CP_CFG1_n(2, 16),

        MAKE_LMS8001_PLL_PROFILE_0_PLL_CP_CFG0_n(0, 0, 2, 1),
        MAKE_LMS8001_PLL_PROFILE_0_PLL_CP_CFG1_n(2, 5),

        MAKE_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_n(0, 1, 2, 3, 1),
        MAKE_LMS8001_PLL_PROFILE_0_PLL_FF_CFG_n(st.divi == 0 ? 0 : 1, st.divi, st.divi),

        MAKE_LMS8001_PLL_PROFILE_0_PLL_SDM_CFG_n(pll.nfrac == 0 ? 1 : 0, 1, 0, 0, pll.nint),

        MAKE_LMS8001_PLL_PROFILE_0_PLL_FRACMODL_n(pll.nfrac),
        MAKE_LMS8001_PLL_PROFILE_0_PLL_FRACMODH_n(pll.nfrac >> 16),

        // Auto calibration
        MAKE_LMS8001_PLL_CONFIGURATION_PLL_CFG(0, 3, 0, 1, 1, 0, 0), // Reset PLL
        MAKE_LMS8001_PLL_CONFIGURATION_PLL_CFG(1, 3, 0, 1, 1, 0, 0), // Calibration ON
        MAKE_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO1(0, 2, 7, 0),
        MAKE_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO2(4, 128),
        MAKE_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO3(250, 5),

        // Enable (divider disabled)
        MAKE_LMS8001_PLL_PROFILE_0_PLL_ENABLE_n(1, 0, 1, 1, 1, 1, 1, 1, st.divi == 0 ? 0 : 1, 0, 1, 1, 1),

        // Start auto calibration
        MAKE_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO0(1, 0, 0, 0, 0),

        // LO dist settings
        MAKE_LMS8001_PLL_PROFILE_0_PLL_LODIST_CFG_n(state->chan_mask, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0),
    };

    res = res ? res : lms8001_spi_post(state, pll_regs, SIZEOF_ARRAY(pll_regs));
    res = res ? res : usleep(5000);
    res = res ? res : lms8001_spi_get(state, PLL_CONFIGURATION_PLL_CAL_AUTO0, &rb);
    if (res)
        return res;

    int fcst = GET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO0_FCAL_START(rb);
    int vco_sel_v =  GET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO0_VCO_SEL_FINAL_VAL(rb);
    int freq_sel_v = GET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO0_FREQ_FINAL_VAL(rb);

    int cal_vco = GET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO0_VCO_SEL_FINAL(rb);
    int cal_freq = GET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO0_FREQ_FINAL(rb);

    if (!(fcst == 0 && vco_sel_v == 1 && freq_sel_v == 1)) {
        USDR_LOG("8001", USDR_LOG_ERROR, "Can't perform VCO autocalibration! VCO = %.3f Mhz REF = %.3f Mhz\n", st.fvco / 1.0e6, fref / 1.0e6);
        return -ERANGE;
    }

    uint32_t pll_regs2[] = {
        MAKE_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_n(0, 1, 2, cal_vco, 1),
        MAKE_LMS8001_PLL_PROFILE_0_PLL_VCO_FREQ_n(cal_freq),
        MAKE_LMS8001_PLL_CONFIGURATION_PLL_CFG(1, 3, 0, 0, 1, 0, 0),  // Calibration OFF
    };
    res = res ? res : lms8001_spi_post(state, pll_regs2, SIZEOF_ARRAY(pll_regs2));
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


int lms8001a_ch_enable(lms8001_state_t* state, unsigned mask, unsigned lna_loss[4], unsigned pa_loss[4])
{
    int res;
    unsigned e[] = { (mask >> 0) & 1, (mask >> 1) & 1, (mask >> 2) & 1, (mask >> 3) & 1 };
    unsigned lna_bp[4];
    unsigned pa_bp[4];

    for (unsigned i = 0; i < 4; i++) {
        lna_bp[i] = (lna_loss[i] == ~0u) ? 1 : 0;
        pa_bp[i] = (pa_loss[i] == ~0u) ? 1 : 0;
    }

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

        MAKE_LMS8001_CHANNEL_A_CHx_PD0(0, pa_bp[0], pa_bp[0] || !e[0], !lna_bp[0] || !e[0], 1, lna_bp[0] || !e[0], 1, lna_bp[0] || !e[0]),
        MAKE_LMS8001_CHANNEL_B_CHx_PD0(0, pa_bp[1], pa_bp[1] || !e[1], !lna_bp[1] || !e[1], 1, lna_bp[1] || !e[1], 1, lna_bp[1] || !e[1]),
        MAKE_LMS8001_CHANNEL_C_CHx_PD0(0, pa_bp[2], pa_bp[2] || !e[2], !lna_bp[2] || !e[2], 1, lna_bp[2] || !e[2], 1, lna_bp[2] || !e[2]),
        MAKE_LMS8001_CHANNEL_D_CHx_PD0(0, pa_bp[3], pa_bp[3] || !e[3], !lna_bp[3] || !e[3], 1, lna_bp[3] || !e[3], 1, lna_bp[3] || !e[3]),

        MAKE_LMS8001_CHANNEL_A_CHx_LNA_CTRL0(16, 16, 10, lna_loss[0]),
        MAKE_LMS8001_CHANNEL_B_CHx_LNA_CTRL0(16, 16, 10, lna_loss[1]),
        MAKE_LMS8001_CHANNEL_C_CHx_LNA_CTRL0(16, 16, 10, lna_loss[2]),
        MAKE_LMS8001_CHANNEL_D_CHx_LNA_CTRL0(16, 16, 10, lna_loss[3]),

        MAKE_LMS8001_CHANNEL_A_CHx_PA_CTRL0(pa_loss[0], pa_loss[0]),
        MAKE_LMS8001_CHANNEL_B_CHx_PA_CTRL0(pa_loss[1], pa_loss[1]),
        MAKE_LMS8001_CHANNEL_C_CHx_PA_CTRL0(pa_loss[2], pa_loss[2]),
        MAKE_LMS8001_CHANNEL_D_CHx_PA_CTRL0(pa_loss[3], pa_loss[3]),
    };

    res = lms8001_spi_post(state, en_regs, SIZEOF_ARRAY(en_regs));
    return res;
}

int lms8001a_ch_lna_pa_set(lms8001_state_t* state, unsigned chan, unsigned lna_loss, unsigned pa_loss)
{
    if (chan >= 4)
        return -EINVAL;

    unsigned choff = ((chan == 0) ? CHANNEL_A_CHx_PD0 : (chan == 1) ? CHANNEL_B_CHx_PD0 : (chan == 2) ? CHANNEL_C_CHx_PD0 : CHANNEL_D_CHx_PD0) - CHANNEL_A_CHx_PD0;
    unsigned lna_bp = (lna_loss == ~0u) ? 1 : 0;
    unsigned pa_bp = (pa_loss == ~0u) ? 1 : 0;
    uint32_t en_regs[] = {
        MAKE_LMS8001_CHANNEL_A_CHx_PD0(0, pa_bp, pa_bp, !lna_bp, 1, lna_bp, 1, lna_bp) + (choff << 16),
        MAKE_LMS8001_CHANNEL_A_CHx_LNA_CTRL0(16, 16, 10, lna_loss) + (choff << 16),
        MAKE_LMS8001_CHANNEL_A_CHx_PA_CTRL0(pa_loss, pa_loss) + (choff << 16),
    };

    return lms8001_spi_post(state, en_regs, SIZEOF_ARRAY(en_regs));
}

int lms8001b_hlmix_loss_set(lms8001_state_t* state, unsigned chan, unsigned loss)
{
    if (chan >= 4)
        return -EINVAL;

    unsigned choff = ((chan == 0) ? HLMIXA_HLMIXx_LOSS0 : (chan == 1) ? HLMIXB_HLMIXx_LOSS0 : (chan == 2) ? HLMIXC_HLMIXx_LOSS0 : HLMIXD_HLMIXx_LOSS0) - HLMIXA_HLMIXx_LOSS0;
    uint32_t en_regs[] = {
        MAKE_LMS8001_HLMIXA_HLMIXx_LOSS0(loss, 0) + (choff << 16),
    };

    return lms8001_spi_post(state, en_regs, SIZEOF_ARRAY(en_regs));
}

int lms8001_core_enable(lms8001_state_t* out, bool en)
{
    uint32_t lms_init[] = {
        MAKE_LMS8001_BIASLDOCONFIG_CLK_BUF_LDO_Config(0, 0, en ? 1 : 0, out->stepping == LMS8_MPW2024 ? LMS_LDO_VDD_PLL_CLKBUF : LMS_LDO_1P25),
        MAKE_LMS8001_BIASLDOCONFIG_PLL_DIV_LDO_Config(0, 0, en ? 1 : 0, out->stepping == LMS8_MPW2024 ? LMS_LDO_VDD_PLL_DIV : LMS_LDO_1P25),
        MAKE_LMS8001_BIASLDOCONFIG_PLL_CP_LDO_Config(0, 0, en ? 1 : 0, out->stepping == LMS8_MPW2024 ? LMS_LDO_VDD_PLL_CP : LMS_LDO_1P25),
    };
    return lms8001_spi_post(out, lms_init, SIZEOF_ARRAY(lms_init));
}

int lms8001_create(lldev_t dev, unsigned subdev, unsigned lsaddr, unsigned int stepping, lms8001_state_t *out)
{
    int res;
    memset(&out->pll, 0, sizeof(out->pll));
    memset(out->pll_profiles, 0, sizeof(out->pll_profiles));

    uint32_t lms_init[] = {
        MAKE_LMS8001_CHIPCONFIG_SPIConfig(1, 1, 1, 1, 1, 1, 1),

        MAKE_LMS8001_BIASLDOCONFIG_BiasConfig(1, 0, 9, 0, 0, 0, 0, 0),
        MAKE_LMS8001_BIASLDOCONFIG_CLK_BUF_LDO_Config(0, 0, 1, out->stepping == LMS8_MPW2024 ? LMS_LDO_VDD_PLL_CLKBUF : LMS_LDO_1P25),
        MAKE_LMS8001_BIASLDOCONFIG_PLL_DIV_LDO_Config(0, 0, 1, out->stepping == LMS8_MPW2024 ? LMS_LDO_VDD_PLL_DIV : LMS_LDO_1P25),
        MAKE_LMS8001_BIASLDOCONFIG_PLL_CP_LDO_Config(0, 0, 1, out->stepping == LMS8_MPW2024 ? LMS_LDO_VDD_PLL_CP : LMS_LDO_1P25),

        MAKE_LMS8001_PLL_CONFIGURATION_PLL_VREG(1, 0, 1, 1, 205),
        MAKE_LMS8001_PLL_CONFIGURATION_PLL_CFG_XBUF(1, 0, 1),
        //MAKE_LMS8001_PLL_CONFIGURATION_PLL_CFG_XBUF(0, 0, 1),

        ////////////////////////////////////////////////////////////////////////////////////
        MAKE_LMS8001_PLL_PROFILE_0_PLL_ENABLE_n(1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1),
    };

    if (stepping > LMS8_MPW2024)
        return -EINVAL;

    out->stepping = stepping;

    out->pll.PLL_VREG = lms_init[4];
    out->pll.PLL_CFG_XBUF = lms_init[5];

    out->dev = dev;
    out->subdev = subdev;
    out->lsaddr = lsaddr;

    out->chan_mask = 0;
    out->act_profile = 0;


    res = lms8001_spi_post(out, lms_init, SIZEOF_ARRAY(lms_init));

    // Move away!
    res = res ? res : lms8001_ch_enable(out, 0xff);

    res = res ? res : lms8001_spi_get(out, PLL_CONFIGURATION_PLL_CAL_AUTO1, &out->pll.PLL_CAL_AUTO1);
    res = res ? res : lms8001_spi_get(out, PLL_CONFIGURATION_PLL_CAL_AUTO2, &out->pll.PLL_CAL_AUTO2);
    res = res ? res : lms8001_spi_get(out, PLL_CONFIGURATION_PLL_CAL_AUTO3, &out->pll.PLL_CAL_AUTO3);

    res = res ? res : lms8001_spi_get(out, PLL_CONFIGURATION_PLL_CFG, &out->pll.PLL_CFG);
   // res = res ? res : lms8001_spi_get(out, PLL_CONFIGURATION_PLL_LODIST_CFG1, &out->pll.PLL_LODIST_CFG1);
   // res = res ? res : lms8001_spi_get(out, PLL_CONFIGURATION_PLL_LODIST_CFG2, &out->pll.PLL_LODIST_CFG2);

    res = res ? res : lms8001_spi_get(out, PLL_PROFILE_0_PLL_ENABLE_n, &out->pll_profiles[0].ENABLE);
    res = res ? res : lms8001_spi_get(out, PLL_PROFILE_0_PLL_LPF_CFG1_n, &out->pll_profiles[0].LPF_CFG1);
    res = res ? res : lms8001_spi_get(out, PLL_PROFILE_0_PLL_LPF_CFG2_n, &out->pll_profiles[0].LPF_CFG2);
    res = res ? res : lms8001_spi_get(out, PLL_PROFILE_0_PLL_CP_CFG0_n, &out->pll_profiles[0].CP_CFG0);
    res = res ? res : lms8001_spi_get(out, PLL_PROFILE_0_PLL_CP_CFG1_n, &out->pll_profiles[0].CP_CFG1);
    res = res ? res : lms8001_spi_get(out, PLL_PROFILE_0_PLL_VCO_FREQ_n, &out->pll_profiles[0].VCO_FREQ);
    res = res ? res : lms8001_spi_get(out, PLL_PROFILE_0_PLL_VCO_CFG_n, &out->pll_profiles[0].VCO_CFG);
    res = res ? res : lms8001_spi_get(out, PLL_PROFILE_0_PLL_FF_CFG_n, &out->pll_profiles[0].FF_CFG);
    res = res ? res : lms8001_spi_get(out, PLL_PROFILE_0_PLL_SDM_CFG_n, &out->pll_profiles[0].SDM_CFG);
    res = res ? res : lms8001_spi_get(out, PLL_PROFILE_0_PLL_FRACMODL_n, &out->pll_profiles[0].FRACMODL);
    res = res ? res : lms8001_spi_get(out, PLL_PROFILE_0_PLL_FRACMODH_n, &out->pll_profiles[0].FRACMODH);
    res = res ? res : lms8001_spi_get(out, PLL_PROFILE_0_PLL_LODIST_CFG_n, &out->pll_profiles[0].LODIST_CFG);
    res = res ? res : lms8001_spi_get(out, PLL_PROFILE_0_PLL_FLOCK_CFG1_n, &out->pll_profiles[0].FLOCK_CFG1);
    res = res ? res : lms8001_spi_get(out, PLL_PROFILE_0_PLL_FLOCK_CFG2_n, &out->pll_profiles[0].FLOCK_CFG2);
    res = res ? res : lms8001_spi_get(out, PLL_PROFILE_0_PLL_FLOCK_CFG3_n, &out->pll_profiles[0].FLOCK_CFG3);
    return res;
}

int lms8001_destroy(lms8001_state_t* m)
{
    return 0;
}


struct lms80001_tune_settings {
    int vtune_vct;
    int vco_sel_force;
    int vco_sel_init;
    int freq_init_pos;
    int freq_init;
    int freq_settling_N;
    int vtune_wait_N;
    int vco_sel_freq_max;
    int vco_sel_freq_min;
};
typedef struct lms80001_tune_settings lms80001_tune_settings_t;

static void _lms80001_tune_settings_def(lms8001_state_t* m, lms80001_tune_settings_t* s) {
    s->vtune_vct = m->stepping == LMS8_MPW2024 ? 2 : 1;
    s->vco_sel_force = 0;
    s->vco_sel_init = 2;
    s->freq_init_pos = 7;
    s->freq_init  = 0;
    s->freq_settling_N = 4;
    s->vtune_wait_N = 128;
    s->vco_sel_freq_max = 255;
    s->vco_sel_freq_min = 0;

}

static uint32_t _mk_pav(lms8001_state_t* m, unsigned addr, uint16_t val)
{
    return MAKE_LMS8001_REG_WR((PLL_PROFILE_1_PLL_ENABLE_n - PLL_PROFILE_0_PLL_ENABLE_n) * m->act_profile + addr, val);
}

static int _lms8001_vco_tune(lms8001_state_t* m, uint64_t fvco, int fref, uint32_t flags, const lms80001_tune_settings_t* s, double *actual)
{
    lms8001_pll_settings_t pll = _lms8001_calc_pll(fvco, fref, flags);
    bool xbuf_slfben = (flags & LMS8001_SELF_BIAS_XBUF) == LMS8001_SELF_BIAS_XBUF;
    bool pdiv2 = (flags & LMS8001_PDIV2) == LMS8001_PDIV2;
    bool int_mode = (flags & LMS8001_INT_MODE) == LMS8001_INT_MODE;
    int res = 0;
    uint16_t rb;
    double actual_freq = (double)fref * pll.nfix * (pll.nint + pll.nfrac / (double)(1 << 20));

    lms8001_pll_state_t* curr = &m->pll_profiles[m->act_profile];

    // ======================= enablePLL part =======================
    // Enable VCO Biasing Block
    SET_LMS8001_PLL_CONFIGURATION_PLL_VREG_EN_VCOBIAS(m->pll.PLL_VREG, 1);

    // Enable XBUF
    // Sets SLFBEN, when TCXO is AC - coupled to LMS8001 IC REFIN
    SET_LMS8001_PLL_CONFIGURATION_PLL_CFG_XBUF_PLL_XBUF_EN(m->pll.PLL_CFG_XBUF, 1);
    SET_LMS8001_PLL_CONFIGURATION_PLL_CFG_XBUF_PLL_XBUF_SLFBEN(m->pll.PLL_CFG_XBUF, xbuf_slfben ? 1 : 0);

    SET_LMS8001_PLL_PROFILE_0_PLL_ENABLE_N_PLL_EN_VTUNE_COMP_n(curr->ENABLE, 1);
    SET_LMS8001_PLL_PROFILE_0_PLL_ENABLE_N_PLL_EN_LD_n(curr->ENABLE, 1);
    SET_LMS8001_PLL_PROFILE_0_PLL_ENABLE_N_PLL_EN_PFD_n(curr->ENABLE, 1);
    SET_LMS8001_PLL_PROFILE_0_PLL_ENABLE_N_PLL_EN_CP_n(curr->ENABLE, 1);
    SET_LMS8001_PLL_PROFILE_0_PLL_ENABLE_N_PLL_EN_VCO_n(curr->ENABLE, 1);
    SET_LMS8001_PLL_PROFILE_0_PLL_ENABLE_N_PLL_EN_FFDIV_n(curr->ENABLE, 1);
    SET_LMS8001_PLL_PROFILE_0_PLL_ENABLE_N_PLL_EN_FBDIV_n(curr->ENABLE, 1);

    SET_LMS8001_PLL_PROFILE_0_PLL_ENABLE_N_PLL_EN_FB_PDIV2_n(curr->ENABLE, pdiv2 ? 1 : 0);
    SET_LMS8001_PLL_PROFILE_0_PLL_ENABLE_N_PLL_SDM_CLK_EN_n(curr->ENABLE, int_mode ? 0 : 1);

    // ======================= back to vco_auto_ctune =======================
    // Set the VCO tuning voltage value during coarse - tuning
    SET_LMS8001_PLL_PROFILE_0_PLL_LPF_CFG2_N_VTUNE_VCT_n(curr->LPF_CFG2, s->vtune_vct);

    SET_LMS8001_PLL_PROFILE_0_PLL_SDM_CFG_N_INTMOD_EN_n(curr->SDM_CFG, int_mode ? 1 : 0);
    SET_LMS8001_PLL_PROFILE_0_PLL_SDM_CFG_N_INTMOD_n(curr->SDM_CFG, pll.nint);

    SET_LMS8001_PLL_PROFILE_0_PLL_FRACMODL_N_FRACMODL_n(curr->FRACMODL, pll.nfrac >> 0);
    SET_LMS8001_PLL_PROFILE_0_PLL_FRACMODH_N_FRACMODH_n(curr->FRACMODH, pll.nfrac >> 16);

    // Reset PLL, Enable Calibration Mode
    // Reset PLL: PLL_RSTN 0 -> 1 (later in transaction stage)

    SET_LMS8001_PLL_CONFIGURATION_PLL_CFG_PLL_RSTN(m->pll.PLL_CFG, 1);
    SET_LMS8001_PLL_CONFIGURATION_PLL_CFG_CTUNE_RES(m->pll.PLL_CFG, 3);

    m->pll.PLL_CAL_AUTO1 = MAKE_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO1(s->vco_sel_force, s->vco_sel_init, s->freq_init_pos, s->freq_init);
    m->pll.PLL_CAL_AUTO2 = MAKE_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO2(s->freq_settling_N, s->vtune_wait_N);
    m->pll.PLL_CAL_AUTO3 = MAKE_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO3(s->vco_sel_freq_max, s->vco_sel_freq_min);

    uint32_t lms_init[] = {
        MAKE_LMS8001_REG_WR(PLL_CONFIGURATION_PLL_VREG, m->pll.PLL_VREG),
        MAKE_LMS8001_REG_WR(PLL_CONFIGURATION_PLL_CFG_XBUF, m->pll.PLL_CFG_XBUF),

        _mk_pav(m, PLL_PROFILE_0_PLL_ENABLE_n, curr->ENABLE),
        _mk_pav(m, PLL_PROFILE_0_PLL_LPF_CFG2_n, curr->LPF_CFG2),
        _mk_pav(m, PLL_PROFILE_0_PLL_SDM_CFG_n, curr->SDM_CFG),
        _mk_pav(m, PLL_PROFILE_0_PLL_FRACMODL_n, curr->FRACMODL),
        _mk_pav(m, PLL_PROFILE_0_PLL_FRACMODH_n, curr->FRACMODH),

        MAKE_LMS8001_REG_WR(PLL_CONFIGURATION_PLL_CFG, m->pll.PLL_CFG & ~((1 << PLL_CONFIGURATION_PLL_CFG_PLL_RSTN_OFF))),
        MAKE_LMS8001_REG_WR(PLL_CONFIGURATION_PLL_CFG, m->pll.PLL_CFG),
        MAKE_LMS8001_REG_WR(PLL_CONFIGURATION_PLL_CFG, m->pll.PLL_CFG | (1 << PLL_CONFIGURATION_PLL_CFG_PLL_CALIBRATION_EN_OFF)), // Set CAL mode

        // Write VCO AUTO - CAL Registers
        MAKE_LMS8001_REG_WR(PLL_CONFIGURATION_PLL_CAL_AUTO1, m->pll.PLL_CAL_AUTO1),
        MAKE_LMS8001_REG_WR(PLL_CONFIGURATION_PLL_CAL_AUTO2, m->pll.PLL_CAL_AUTO2),
        MAKE_LMS8001_REG_WR(PLL_CONFIGURATION_PLL_CAL_AUTO3, m->pll.PLL_CAL_AUTO3),

        // Start VCO Auto - Tuning Process
        MAKE_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO0(1, 0, 0, 0, 0),
    };

    res = lms8001_spi_post(m, lms_init, SIZEOF_ARRAY(lms_init));
    if (res)
        return res;

    usleep(5000);

    //  If it is not, check again?
    res = lms8001_spi_get(m, PLL_CONFIGURATION_PLL_CAL_AUTO0, &rb);
    if (res)
        return res;

    int FCAL_start = GET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO0_FCAL_START(rb);
    int VCO_sel_final_val =  GET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO0_VCO_SEL_FINAL_VAL(rb);
    int freq_sel_final_val = GET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO0_FREQ_FINAL_VAL(rb);

    int VCO_final = GET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO0_VCO_SEL_FINAL(rb);
    int freq_final = GET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO0_FREQ_FINAL(rb);

    if ((FCAL_start == 0) && (VCO_sel_final_val == 1) && (freq_sel_final_val == 1)) {
        SET_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_N_VCO_SEL_n(curr->VCO_CFG, VCO_final);
        SET_LMS8001_PLL_PROFILE_0_PLL_VCO_FREQ_N_VCO_FREQ_n(curr->VCO_FREQ, freq_final);

        uint32_t lms_init[] = {
            _mk_pav(m, PLL_PROFILE_0_PLL_VCO_CFG_n, curr->VCO_CFG),
            _mk_pav(m, PLL_PROFILE_0_PLL_VCO_FREQ_n, curr->VCO_FREQ),

            MAKE_LMS8001_REG_WR(PLL_CONFIGURATION_PLL_CFG, m->pll.PLL_CFG), // Clear CAL mode
        };
        res = res ? res : lms8001_spi_post(m, lms_init, SIZEOF_ARRAY(lms_init));

    } else {
        USDR_LOG("8001", USDR_LOG_WARNING, "WARNING: Requested frequency fvco = %.3f Mhz could not be tuned FCAL/VCO/FREQ: %d/%d/%d.\n",
                 fvco / 1.0e6, FCAL_start, VCO_sel_final_val, freq_sel_final_val);
        res = -ERANGE;
    }

    USDR_LOG("8001", USDR_LOG_NOTE, "VCO Calibration finished, VCO:%d CAP:%d\n", VCO_final, freq_final);

    if (actual) *actual = actual_freq;
    return res;
}

static int _lms8001_lock_status(lms8001_state_t* m, int* vtune_high, int* vtune_low, int* pll_lock)
{
    uint16_t rb;
    int res = lms8001_spi_get(m, PLL_CONFIGURATION_PLL_CFG_STATUS, &rb);
    if (res)
        return res;

    // Get VTUNE_HIGH, VTUNE_LOW, PLL_LOCK bit values
    *vtune_high = GET_LMS8001_PLL_CONFIGURATION_PLL_CFG_STATUS_VTUNE_HIGH(rb);
    *vtune_low = GET_LMS8001_PLL_CONFIGURATION_PLL_CFG_STATUS_VTUNE_LOW(rb);
    *pll_lock = GET_LMS8001_PLL_CONFIGURATION_PLL_CFG_STATUS_PLL_LOCK(rb);
    return 0;
}

static int _lms8001_change_pll_vco_cfg(lms8001_state_t* m, uint64_t fvco, int fref, uint32_t flags, lms80001_tune_settings_t* vco_settings)
{
    int res = 0, vtune_high, vtune_low, pll_lock;
    lms8001_pll_state_t* curr = &m->pll_profiles[m->act_profile];
    uint32_t lms_init[] = {
        _mk_pav(m, PLL_PROFILE_0_PLL_VCO_CFG_n, curr->VCO_CFG),
    };
    res = res ? res : lms8001_spi_post(m, lms_init, SIZEOF_ARRAY(lms_init));
    res = res ? res : _lms8001_vco_tune(m, fvco, fref, flags, vco_settings, NULL);
    res = res ? res : usleep(30000);
    res = res ? res :  _lms8001_lock_status(m, &vtune_high, &vtune_low, &pll_lock);
    if (res)
        return res;

    if ((vtune_high == 0) && (vtune_low == 0)) {
        return 1;
    }
    return 0;
}

static int _lms8001_center_vtune(lms8001_state_t* m, uint64_t fvco, int fref, uint32_t flags)
{
    int res;
    lms8001_pll_state_t* curr = &m->pll_profiles[m->act_profile];
    lms80001_tune_settings_t vco_settings;
    _lms80001_tune_settings_def(m, &vco_settings);
    vco_settings.vtune_vct = 1;
    vco_settings.vco_sel_force = 1;
    vco_settings.freq_init_pos = 4;
    vco_settings.vco_sel_init = GET_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_N_VCO_SEL_n(curr->VCO_CFG);

    // Reset PLL
    uint32_t lms_init[] = {
        MAKE_LMS8001_REG_WR(PLL_CONFIGURATION_PLL_CFG, m->pll.PLL_CFG & ~((1 << PLL_CONFIGURATION_PLL_CFG_PLL_RSTN_OFF))),
        MAKE_LMS8001_REG_WR(PLL_CONFIGURATION_PLL_CFG, m->pll.PLL_CFG | (1 << PLL_CONFIGURATION_PLL_CFG_PLL_RSTN_OFF)),
    };
    res = lms8001_spi_post(m, lms_init, SIZEOF_ARRAY(lms_init));
    if (res)
        return res;

    // Get Initial value for VCO_FREQ<1:0> word
    int freq_init = GET_LMS8001_PLL_PROFILE_0_PLL_VCO_FREQ_N_VCO_FREQ_n(curr->VCO_FREQ);
    vco_settings.freq_init = freq_init;

    // Get Initial value for VDIV_SWVDD<1:0> word
    int vdiv_swvdd_init = GET_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_N_VDIV_SWVDD_n(curr->VCO_CFG);

    // Get Initial Value for VCO_AMP<7:0> and VCO_AAC_EN
    int amp_init = GET_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_N_VCO_AMP_n(curr->VCO_CFG);
    int aac_en_init = GET_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_N_VCO_AAC_EN_n(curr->VCO_CFG);

    // Get Initial Values stored in PLL_CAL_AUTO1 register
    int vco_sel_force_init = GET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO1_VCO_SEL_FORCE(m->pll.PLL_CAL_AUTO1);
    int vco_sel_init = GET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO1_VCO_SEL_INIT(m->pll.PLL_CAL_AUTO1);
    int vco_freq_init_pos = GET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO1_FREQ_INIT_POS(m->pll.PLL_CAL_AUTO1);
    int vco_freq_init = GET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO1_FREQ_INIT(m->pll.PLL_CAL_AUTO1);

    int vtune_high, vtune_low, pll_lock;

    usleep(20000);

    res = _lms8001_lock_status(m, &vtune_high, &vtune_low, &pll_lock);
    if (res)
        return res;

    if ((vtune_high == 0) && (vtune_low == 0)) {
        USDR_LOG("8001", USDR_LOG_INFO, "Centering of VTUNE not needed\n");
        return 0;
    }

    int swvdd_list[4] = { 3, 2, 1, 0 };
    int amp_list[4] = { 3, 2, 1, 0 };

    for (int i = 0; i < 4; i++) {
        int vdiv_swvdd = swvdd_list[i];
        if (vdiv_swvdd_init != vdiv_swvdd) {
            SET_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_N_VDIV_SWVDD_n(curr->VCO_CFG, vdiv_swvdd);
            res = _lms8001_change_pll_vco_cfg(m, fvco, fref, flags, &vco_settings);
            if (res == 1) {
                USDR_LOG("8001", USDR_LOG_INFO, "VTUNE voltage centered successfuly by changing VDIV_SWVDD value = %d\n", vdiv_swvdd);
                goto restore_autotune;
            } else if (res) {
                return res;
            }
        }
    }

    // Set back VDIV_SWVDD<1:0> and FREQ<7:0> to inital values
    SET_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_N_VDIV_SWVDD_n(curr->VCO_CFG, vdiv_swvdd_init);
    SET_LMS8001_PLL_PROFILE_0_PLL_VCO_FREQ_N_VCO_FREQ_n(curr->VCO_FREQ, freq_init);
    uint32_t lms_init2[] = {
        _mk_pav(m, PLL_PROFILE_0_PLL_VCO_CFG_n, curr->VCO_CFG),
        _mk_pav(m, PLL_PROFILE_0_PLL_VCO_FREQ_n, curr->VCO_FREQ),
    };
    res = lms8001_spi_post(m, lms_init2, SIZEOF_ARRAY(lms_init2));
    if (res) {
        return res;
    }

    for (int i = 0; i < 4; i++) {
        int amp = amp_list[i];
        if (amp_init != amp) {
            SET_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_N_VCO_AMP_n(curr->VCO_CFG, amp);
            SET_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_N_VCO_AAC_EN_n(curr->VCO_CFG, 1);
            res = _lms8001_change_pll_vco_cfg(m, fvco, fref, flags, &vco_settings);
            if (res == 1) {
                USDR_LOG("8001", USDR_LOG_INFO, "VTUNE voltage centered successfuly by changing VCO_AMP value = %d\n", amp);
                goto restore_autotune;
            } else if (res) {
                return res;
            }
        }
    }

    // Set back VCO_AMP, VCO_AAC_EN, and FREQ<7:0> to inital values
    SET_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_N_VCO_AMP_n(curr->VCO_CFG, amp_init);
    SET_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_N_VCO_AAC_EN_n(curr->VCO_CFG, aac_en_init);
    SET_LMS8001_PLL_PROFILE_0_PLL_VCO_FREQ_N_VCO_FREQ_n(curr->VCO_FREQ, freq_init);


    uint32_t lms_init3[] = {
        _mk_pav(m, PLL_PROFILE_0_PLL_VCO_CFG_n, curr->VCO_CFG),
        _mk_pav(m, PLL_PROFILE_0_PLL_VCO_FREQ_n, curr->VCO_FREQ),
    };
    res = lms8001_spi_post(m, lms_init3, SIZEOF_ARRAY(lms_init3));
    if (res) {
        return res;
    }

    // FIXME: somehow it helps!
    // Last resort check
    usleep(5000);
    res = _lms8001_lock_status(m, &vtune_high, &vtune_low, &pll_lock);
    if (res)
        return res;

    if ((vtune_high == 0) && (vtune_low == 0)) {
        USDR_LOG("8001", USDR_LOG_INFO, "Centering of VTUNE back to origianl\n");
        return 0;
    }

    USDR_LOG("8001", USDR_LOG_ERROR, "Centering VTUNE using VDIV_SWVDD and VCO_AMP failed!\n");
    return -ERANGE;

restore_autotune:
    // Set back PLL_CAL_AUTO1 to starting values
    SET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO1_VCO_SEL_FORCE(m->pll.PLL_CAL_AUTO1, vco_sel_force_init);
    SET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO1_VCO_SEL_INIT(m->pll.PLL_CAL_AUTO1, vco_sel_init);
    SET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO1_FREQ_INIT_POS(m->pll.PLL_CAL_AUTO1, vco_freq_init_pos);
    SET_LMS8001_PLL_CONFIGURATION_PLL_CAL_AUTO1_FREQ_INIT(m->pll.PLL_CAL_AUTO1, vco_freq_init);

    uint32_t lms_init4[] = {
        MAKE_LMS8001_REG_WR(PLL_CONFIGURATION_PLL_CAL_AUTO1, m->pll.PLL_CAL_AUTO1),
    };
    return lms8001_spi_post(m, lms_init4, SIZEOF_ARRAY(lms_init4));
}


static int _lms8001_pll_set_lpf(lms8001_state_t* m, int C1, int C2, int R2, int C3, int R3)
{
    lms8001_pll_state_t* curr = &m->pll_profiles[m->act_profile];

    SET_LMS8001_PLL_PROFILE_0_PLL_LPF_CFG1_N_R3_n(curr->LPF_CFG1, R3);
    SET_LMS8001_PLL_PROFILE_0_PLL_LPF_CFG1_N_R2_n(curr->LPF_CFG1, R2);
    SET_LMS8001_PLL_PROFILE_0_PLL_LPF_CFG1_N_C2_n(curr->LPF_CFG1, C2);
    SET_LMS8001_PLL_PROFILE_0_PLL_LPF_CFG1_N_C1_n(curr->LPF_CFG1, C1);

    SET_LMS8001_PLL_PROFILE_0_PLL_LPF_CFG2_N_C3_n(curr->LPF_CFG2, C3);

    uint32_t lms_init[] = {
        _mk_pav(m, PLL_PROFILE_0_PLL_LPF_CFG1_n, curr->LPF_CFG1),
        _mk_pav(m, PLL_PROFILE_0_PLL_LPF_CFG2_n, curr->LPF_CFG2),
    };
    return lms8001_spi_post(m, lms_init, SIZEOF_ARRAY(lms_init));
}

static int _lms8001_pll_set_cp(lms8001_state_t* m, int PULSE, int OFS, int ICT_CP)
{
    lms8001_pll_state_t* curr = &m->pll_profiles[m->act_profile];

    SET_LMS8001_PLL_PROFILE_0_PLL_CP_CFG0_N_PULSE_n(curr->CP_CFG0, PULSE);
    SET_LMS8001_PLL_PROFILE_0_PLL_CP_CFG0_N_OFS_n(curr->CP_CFG0, OFS);

    SET_LMS8001_PLL_PROFILE_0_PLL_CP_CFG1_N_ICT_CP_n(curr->CP_CFG1, ICT_CP);

    SET_LMS8001_PLL_PROFILE_0_PLL_ENABLE_N_PLL_EN_CPOFS_n(curr->ENABLE, (OFS >= 1) ? 1 : 0);

    uint32_t lms_init[] = {
        _mk_pav(m, PLL_PROFILE_0_PLL_CP_CFG0_n, curr->CP_CFG0),
        _mk_pav(m, PLL_PROFILE_0_PLL_CP_CFG1_n, curr->CP_CFG1),
        _mk_pav(m, PLL_PROFILE_0_PLL_ENABLE_n, curr->ENABLE),
    };
    return lms8001_spi_post(m, lms_init, SIZEOF_ARRAY(lms_init));
}

static int _lms8001_optim_pll_loopbw(lms8001_state_t* m, double PM_deg, double fc, bool fitKVCO, double N)
{
    lms8001_pll_state_t* curr = &m->pll_profiles[m->act_profile];
    int res = 0;

    // Calculate Phase Margin in radians
    // Calculate angle frequency for fc
    double PM_rad = PM_deg * M_PI / 180.0;
    double wc = 2.0 * M_PI * fc;

    // Get initial CP current settings
    int PULSE_INIT = GET_LMS8001_PLL_PROFILE_0_PLL_CP_CFG0_N_PULSE_n(curr->CP_CFG0);
    int OFS_INIT = GET_LMS8001_PLL_PROFILE_0_PLL_CP_CFG0_N_OFS_n(curr->CP_CFG0);
    int ICT_CP_INIT = GET_LMS8001_PLL_PROFILE_0_PLL_CP_CFG1_N_ICT_CP_n(curr->CP_CFG1);

    // Pulse control word of CP inside LMS8001 will be swept from 63 to 4.
    // First value that gives implementable PLL configuration will be used.
    int cp_pulse_vals[60];
    int cp_pulse_vals_No;
    int currindex = 0;
    for (int i = 63; i >= 4; i-- ) {
        cp_pulse_vals[currindex] = i;
        currindex++;
    }
    cp_pulse_vals_No = currindex;

    // Check VCO_SEL and VCO_FREQ
    int vco_sel = GET_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_N_VCO_SEL_n(curr->VCO_CFG);
    int vco_freq = GET_LMS8001_PLL_PROFILE_0_PLL_VCO_FREQ_N_VCO_FREQ_n(curr->VCO_FREQ);
    if (vco_sel == 0) {
        USDR_LOG("8001", USDR_LOG_ERROR, "External LO selected in PLL_PROFILE\n");
        return -EINVAL;
    }

    double KVCO_avg = 0;
    const static double s_kvco_s_mpw2015[4] = {0, 44.404e6, 33.924e6, 41.455e6};
    const static double s_kvco_s_mpw2024[4] = {0, 6.2842e7, 6.4853e7, 9.1550e7};

    if (!fitKVCO) {
        KVCO_avg = (m->stepping == LMS8_MPW2015) ? s_kvco_s_mpw2015[vco_sel] : s_kvco_s_mpw2024[vco_sel];
    } else if (m->stepping == LMS8_MPW2015) {
        // Use Fitted Values for KVCO in Calculations for mpw 2015
        int CBANK = vco_freq;
        if (vco_sel == 1)
            KVCO_avg = 24.71e6 * (2.09e-10 * pow(CBANK, 4) + 2.77e-09 * pow(CBANK, 3) + 1.13e-05 * pow(CBANK, 2) + 3.73e-03 * CBANK + 1.01e+00);
        else if (vco_sel == 2)
            KVCO_avg = 21.05e6 * (-9.88e-11 * pow(CBANK, 4) + 1.46e-07 * pow(CBANK, 3) + (-2.14e-05) * pow(CBANK, 2) + 5.08e-03 * CBANK + 9.99e-01);
        else if (vco_sel == 3)
            KVCO_avg = 32.00e6 * (-1.04e-10 * pow(CBANK, 4) + 8.72e-08 * pow(CBANK, 3) + (-4.68e-06) * pow(CBANK, 2) + 3.68e-03 * CBANK + 1.00e+00);
    } else {
        // Use Fitted Values for KVCO in Calculations for mpw 2024
        int CBANK = vco_freq;
        if (vco_sel == 1)
            KVCO_avg = 3.8492e7 * (2.1005e-10 * pow(CBANK, 4) - 3.6267e-08 * pow(CBANK, 3) + 1.3634e-05 *pow(CBANK, 2) + 3.0541e-03 * CBANK + 1.0011e+00);
        else if (vco_sel == 2)
            KVCO_avg = 4.4058e7 * (6.4250e-11 * pow(CBANK, 4) + 2.1831e-10 * pow(CBANK, 3) + 5.7102e-06 *pow(CBANK, 2) + 2.6286e-03 * CBANK + 1.0011e+00);
        else if (vco_sel == 3)
            KVCO_avg = 6.2508e7 * (3.7053e-11 * pow(CBANK, 4) + 6.3153e-09 * pow(CBANK, 3) + 5.6397e-06 *pow(CBANK, 2) + 2.5792e-03 * CBANK + 9.9837e-01);
    }

    double Kvco = 2 * M_PI * KVCO_avg;

    double LMS8001_C1_STEP = 1.2e-12;
    double LMS8001_C2_STEP = 10.0e-12;
    double LMS8001_C3_STEP = 1.2e-12;
    double LMS8001_C2_FIX = 150.0e-12;
    double LMS8001_C3_FIX = 5.0e-12;
    double LMS8001_R2_0 = 24.6e3;
    double LMS8001_R3_0 = 14.9e3;

    for (int i = 0; i < cp_pulse_vals_No; i++) {
        int cp_pulse = cp_pulse_vals[i];

        // Read CP Current Value
        double Icp = ICT_CP_INIT * 25.0e-6 / 16.0 * cp_pulse;
        double Kphase = Icp / (2 * M_PI);

        double gamma = 1.045;
        double T31 = 0.1;

        // Approx. formula, Dean Banerjee
        double T1 = (1.0 / cos(PM_rad) - tan(PM_rad)) / (wc*(1 + T31));

        double T3 = T1*T31;
        double T2 = gamma/((pow(wc, 2))*(T1+T3));

        double A0 = (Kphase*Kvco)/((pow(wc, 2))*N)*sqrt((1+(pow(wc, 2))*(pow(T2, 2)))/((1+(pow(wc, 2))*(pow(T1, 2)))*(1+(pow(wc, 2))*(pow(T3, 2)))));
        double A2 = A0*T1*T3;
        double A1 = A0*(T1+T3);

        double C1 = A2/(pow(T2, 2))*(1+sqrt(1+T2/A2*(T2*A0-A1)));
        double C3 = (-(pow(T2, 2))*(pow(C1, 2))+T2*A1*C1-A2*A0)/((pow(T2, 2))*C1-A2);
        double C2 = A0-C1-C3;
        double R2 = T2/C2;
        double R3 = A2/(C1*C3*T2);

        bool C1_cond = (LMS8001_C1_STEP <= C1) && (C1 <= 15.0 * LMS8001_C1_STEP);
        bool C2_cond = (LMS8001_C2_FIX <= C2) && (C2 <= LMS8001_C2_FIX + 15.0 * LMS8001_C2_STEP);
        bool C3_cond = (LMS8001_C3_FIX + LMS8001_C3_STEP <= C3) && (C3 <= LMS8001_C3_FIX + 15.0 * LMS8001_C3_STEP);
        bool R2_cond = (LMS8001_R2_0 / 15.0 <= R2) && (R2 <= LMS8001_R2_0);
        bool R3_cond = (LMS8001_R3_0 / 15.0 <= R3) && (R3 <= LMS8001_R3_0);

        if (C1_cond && C2_cond && C3_cond && R2_cond && R3_cond) {

            int C1_CODE = (int)(round(C1 / LMS8001_C1_STEP));
            int C2_CODE = (int)(round((C2 - LMS8001_C2_FIX) / LMS8001_C2_STEP));
            int C3_CODE = (int)(round((C3 - LMS8001_C3_FIX) / LMS8001_C3_STEP));
            C1_CODE = (int)(min(max(C1_CODE, 0), 15));
            C2_CODE = (int)(min(max(C2_CODE, 0), 15));
            C3_CODE = (int)(min(max(C3_CODE, 0), 15));

            int R2_CODE = (int)(round(LMS8001_R2_0 / R2));
            int R3_CODE = (int)(round(LMS8001_R3_0 / R3));
            R2_CODE = min(max(R2_CODE, 1), 15);
            R3_CODE = min(max(R3_CODE, 1), 15);

            // Set CP Pulse Current to the optimized value
            res = (res) ? res : _lms8001_pll_set_cp(m, cp_pulse, OFS_INIT, ICT_CP_INIT);

            // Set LPF Components to the optimized values
            res = (res) ? res : _lms8001_pll_set_lpf(m, C1_CODE, C2_CODE, R2_CODE, C3_CODE, R3_CODE);

            return res;
        }
    }

    res = (res) ? res : _lms8001_pll_set_cp(m, PULSE_INIT, OFS_INIT, ICT_CP_INIT);
    if (res)
        return res;

    USDR_LOG("8001", USDR_LOG_ERROR, "PLL LoopBW Optimization failed: some of the LPF components out of implementable range\n");
    return -ERANGE;
}


static int _lms8001_pll_set_ld(lms8001_state_t* m, int LD_VCT)
{
    lms8001_pll_state_t* curr = &m->pll_profiles[m->act_profile];
    SET_LMS8001_PLL_PROFILE_0_PLL_ENABLE_N_PLL_EN_LD_n(curr->ENABLE, 1);
    SET_LMS8001_PLL_PROFILE_0_PLL_CP_CFG1_N_LD_VCT_n(curr->CP_CFG1, LD_VCT);

    uint32_t lms_init[] = {
        _mk_pav(m, PLL_PROFILE_0_PLL_ENABLE_n, curr->ENABLE),
        _mk_pav(m, PLL_PROFILE_0_PLL_CP_CFG1_n, curr->CP_CFG1),

    };
    return lms8001_spi_post(m, lms_init, SIZEOF_ARRAY(lms_init));
}


static int _lms8001_optim_cp_ld(lms8001_state_t* m)
{
    lms8001_pll_state_t* curr = &m->pll_profiles[m->act_profile];
    int res = 0;

    // Check operating mode of LMS8001 PLL
    int INTMOD_EN = GET_LMS8001_PLL_PROFILE_0_PLL_SDM_CFG_N_INTMOD_EN_n(curr->SDM_CFG);
    // Read CP current configuration
    int PULSE = GET_LMS8001_PLL_PROFILE_0_PLL_CP_CFG0_N_PULSE_n(curr->CP_CFG0);
    int ICT_CP =GET_LMS8001_PLL_PROFILE_0_PLL_CP_CFG1_N_ICT_CP_n(curr->CP_CFG1);
    int OFS, LD_VCT;

    // Calculate OFS and LD_VCT optimal values
    if (INTMOD_EN) {
        // Set Offset Current and Lock Detector Threashold for IntN - Operating Mode
        LD_VCT = 2;
        OFS = 0;
    } else {
        // Set Offset Current and Lock Detector Threashold for IntN - Operating Mode
        LD_VCT = 0;
        double Icp = (25.0 * ICT_CP / 16.0) * PULSE;
        // Calculate Target Value for Offset Current, as 3 % of Pulse current value
        double Icp_OFS = 0.03 * Icp;
        double Icp_OFS_step = (25.0 * ICT_CP / 16.0) * 0.25;
        OFS = max(1, (int)(Icp_OFS / Icp_OFS_step));
    }

    res = (res) ? res : _lms8001_pll_set_cp(m, PULSE, OFS, ICT_CP);
    res = (res) ? res : _lms8001_pll_set_ld(m, LD_VCT);
    if (res)
        return res;

    USDR_LOG("8001", USDR_LOG_INFO, "Optimization of CP-OFS and LD-VCT Settings: OFS=%d, LD_VCT=%d\n", OFS, LD_VCT);
    return 0;
}

static int _lms8001_set_flock(lms8001_state_t* m, double bwef, int flock_N)
{
    lms8001_pll_state_t* curr = &m->pll_profiles[m->act_profile];

    int R3 = GET_LMS8001_PLL_PROFILE_0_PLL_LPF_CFG1_N_R3_n(curr->LPF_CFG1);
    int R2 = GET_LMS8001_PLL_PROFILE_0_PLL_LPF_CFG1_N_R2_n(curr->LPF_CFG1);
    int C1 = GET_LMS8001_PLL_PROFILE_0_PLL_LPF_CFG1_N_C1_n(curr->LPF_CFG1);
    int C2 = GET_LMS8001_PLL_PROFILE_0_PLL_LPF_CFG1_N_C2_n(curr->LPF_CFG1);
    int C3 = GET_LMS8001_PLL_PROFILE_0_PLL_LPF_CFG2_N_C3_n(curr->LPF_CFG2);

    int PULSE = GET_LMS8001_PLL_PROFILE_0_PLL_CP_CFG0_N_PULSE_n(curr->CP_CFG0);
    int OFS = GET_LMS8001_PLL_PROFILE_0_PLL_CP_CFG0_N_OFS_n(curr->CP_CFG0);

    int R3_FLOCK = min(R3 * bwef, 15);
    int R2_FLOCK = min(R2 * bwef, 15);
    int PULSE_FLOCK = min(PULSE * pow(bwef, 2), 63);

    SET_LMS8001_PLL_PROFILE_0_PLL_FLOCK_CFG1_N_FLOCK_R3_n(curr->FLOCK_CFG1, R3_FLOCK);
    SET_LMS8001_PLL_PROFILE_0_PLL_FLOCK_CFG1_N_FLOCK_R2_n(curr->FLOCK_CFG1, R2_FLOCK);
    SET_LMS8001_PLL_PROFILE_0_PLL_FLOCK_CFG1_N_FLOCK_C2_n(curr->FLOCK_CFG1, C2);
    SET_LMS8001_PLL_PROFILE_0_PLL_FLOCK_CFG1_N_FLOCK_C1_n(curr->FLOCK_CFG1, C1);

    SET_LMS8001_PLL_PROFILE_0_PLL_FLOCK_CFG2_N_FLOCK_C3_n(curr->FLOCK_CFG2, C3);
    SET_LMS8001_PLL_PROFILE_0_PLL_FLOCK_CFG2_N_FLOCK_PULSE_n(curr->FLOCK_CFG2, PULSE_FLOCK);
    SET_LMS8001_PLL_PROFILE_0_PLL_FLOCK_CFG2_N_FLOCK_OFS_n(curr->FLOCK_CFG2, OFS);

    SET_LMS8001_PLL_PROFILE_0_PLL_FLOCK_CFG3_N_FLOCK_VCO_SPDUP_n(curr->FLOCK_CFG3, 0);
    SET_LMS8001_PLL_PROFILE_0_PLL_FLOCK_CFG3_N_FLOCK_N_n(curr->FLOCK_CFG3, min(flock_N, 1023));

    uint32_t lms_init[] = {
        _mk_pav(m, PLL_PROFILE_0_PLL_FLOCK_CFG1_n, curr->FLOCK_CFG1),
        _mk_pav(m, PLL_PROFILE_0_PLL_FLOCK_CFG2_n, curr->FLOCK_CFG2),
        _mk_pav(m, PLL_PROFILE_0_PLL_FLOCK_CFG3_n, curr->FLOCK_CFG3),
    };
    return lms8001_spi_post(m, lms_init, SIZEOF_ARRAY(lms_init));
}

int lms8001_config_pll(lms8001_state_t* m, uint64_t flo, int fref,
                       unsigned tune_flags,
                       int loopBW, double pm, bool fitKVCO, double bwef, int flock_N)
{
    int res = 0;
    bool iq_gen = (tune_flags & LMS8001_IQ_GEN) == LMS8001_IQ_GEN;
    lms8001_pll_state_t* curr = &m->pll_profiles[m->act_profile];
    lms8001_vco_settings_t pll_s;
    double actual_vco;

    // Calculate Loop - Crossover frequency
    double fc = loopBW / 1.65;

    // Check LO range (that was inside setLOFREQ)
    res = (res) ? res : _lms8001_check_lo_range(m, flo, iq_gen);
    res = (res) ? res : _lms8001_calc_vco(m, iq_gen ? (flo << 1) : flo, &pll_s);
    if (res) {
        return res;
    }

    // Set optimal VCO settings 
    SET_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_N_VDIV_SWVDD_n(curr->VCO_CFG, PLL_PROFILE_0_PLL_VCO_CFG_N_VDIV_SWVDD_n_1000_MV_);
    SET_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_N_VCO_AMP_n(curr->VCO_CFG, 3);
    SET_LMS8001_PLL_PROFILE_0_PLL_VCO_CFG_N_VCO_AAC_EN_n(curr->VCO_CFG, 1);

    // Sets FF-DIV Modulus (former setFFDIV)
    SET_LMS8001_PLL_PROFILE_0_PLL_FF_CFG_N_FF_MOD_n(curr->FF_CFG, pll_s.divi);
    SET_LMS8001_PLL_PROFILE_0_PLL_FF_CFG_N_FFCORE_MOD_n(curr->FF_CFG, pll_s.divi);
    SET_LMS8001_PLL_PROFILE_0_PLL_FF_CFG_N_FFDIV_SEL_n(curr->FF_CFG, pll_s.divi > 0 ? 1 : 0);

    SET_LMS8001_PLL_PROFILE_0_PLL_ENABLE_N_PLL_EN_FFCORE_n(curr->ENABLE,  pll_s.divi > 0 ? 1 : 0);

    uint32_t lms_init[] = {
        _mk_pav(m, PLL_PROFILE_0_PLL_VCO_CFG_n, curr->VCO_CFG),
        _mk_pav(m, PLL_PROFILE_0_PLL_FF_CFG_n, curr->FF_CFG),

        // Enable register is going to be update late anyway, so skip this
        // _mk_pav(m, PLL_PROFILE_0_PLL_ENABLE_n, curr->ENABLE),
    };
    res = lms8001_spi_post(m, lms_init, SIZEOF_ARRAY(lms_init));
    if (res)
        return res;

    // Calculate actual VCO frequency
    uint64_t fvco = (iq_gen) ? (flo << (pll_s.divi + 1)) : (flo << pll_s.divi);
    lms80001_tune_settings_t vco_settings;
    _lms80001_tune_settings_def(m, &vco_settings);

    USDR_LOG("8001", USDR_LOG_NOTE, "PLL Tuning to F_VCO=%.3f GHz DIV=%d\n", fvco / 1.0e9, pll_s.divi);

    // Step 1 - Tune PLL to generate F_LO frequency at LODIST outputs that should be manualy enabled
    // outside this method
    res = _lms8001_vco_tune(m, fvco, fref, tune_flags, &vco_settings, &actual_vco);
    if (res) {
        USDR_LOG("8001", USDR_LOG_WARNING, "PLL Tuning to F_LO=%.3f GHz failed!\n", flo / 1.0e9);
        return res;
    }

    // Step 2 - Center VCO Tuning Voltage if needed only for MPW2015
    res = (m->stepping == LMS8_MPW2024) ? res : _lms8001_center_vtune(m, fvco, fref, tune_flags);
    if (res) {
        return res;
    }

    // Step 3 - Optimize PLL settings for targeted LoopBW
    res = _lms8001_optim_pll_loopbw(m, pm, fc, fitKVCO, actual_vco / fref);
    if (res) {
        return res;
    }

    // Step 4 - Optimize CP offset current Lock Detector Threashold depending on operating mode chosen(IntN or FracN)
    res = _lms8001_optim_cp_ld(m);
    if (res) {
        return res;
    }

    // Step 5 - Configure Fast - Lock Mode Registers
    res = _lms8001_set_flock(m, bwef, flock_N);
    if (res) {
        return res;
    }

    return 0;
}


// Smart tune function
// loopbw - Loop BW value, in hz
// phasemargin - Phase Margin value
// bwef - Bandwidth Extension Factor
// flock_N - Number of cycles for fast lock
//
int lms8001_smart_tune(lms8001_state_t* m, unsigned tune_flags, uint64_t flo, int fref, int loopbw, float phasemargin, float bwef, int flock_N)
{
    int res;
    uint64_t LMS8_MIN_NIQ = ((m->stepping == LMS8_MPW2024) ? LMS8_VCO1_MIN_MPW2024 : LMS8_VCO1_MIN_MPW2015) / 8;

    // Set IQ gen for freq < 520 Mhz
    if (flo < LMS8_MIN_NIQ) {
        tune_flags |= LMS8001_IQ_GEN;
    }

    bool fitKVCO = true;
    bool iq_gen = (tune_flags & LMS8001_IQ_GEN) == LMS8001_IQ_GEN;

    lms8001_pll_state_t* curr = &m->pll_profiles[m->act_profile];

    res = lms8001_config_pll(m, flo, fref, tune_flags,
                             loopbw, phasemargin, fitKVCO, bwef, flock_N);
    if (res)
        return res;

    // Setup (enable or disable) quadrature generation
    SET_LMS8001_PLL_PROFILE_0_PLL_LODIST_CFG_N_PLL_LODIST_EN_OUT_n(curr->LODIST_CFG, m->chan_mask);

    SET_LMS8001_PLL_PROFILE_0_PLL_LODIST_CFG_N_PLL_LODIST_FSP_OUT0_FQD_n(curr->LODIST_CFG, iq_gen ? 0 : 1);
    SET_LMS8001_PLL_PROFILE_0_PLL_LODIST_CFG_N_PLL_LODIST_FSP_OUT1_FQD_n(curr->LODIST_CFG, iq_gen ? 0 : 1);
    SET_LMS8001_PLL_PROFILE_0_PLL_LODIST_CFG_N_PLL_LODIST_FSP_OUT2_FQD_n(curr->LODIST_CFG, iq_gen ? 0 : 1);
    SET_LMS8001_PLL_PROFILE_0_PLL_LODIST_CFG_N_PLL_LODIST_FSP_OUT3_FQD_n(curr->LODIST_CFG, iq_gen ? 0 : 1);

    SET_LMS8001_PLL_PROFILE_0_PLL_ENABLE_N_PLL_LODIST_EN_DIV2IQ_n(curr->ENABLE, iq_gen ? 1 : 0);

    // Enable LO distribution
    SET_LMS8001_PLL_PROFILE_0_PLL_ENABLE_N_PLL_LODIST_EN_BIAS_n(curr->ENABLE, 1);

    uint32_t lms_init[] = {
        _mk_pav(m, PLL_PROFILE_0_PLL_LODIST_CFG_n, curr->LODIST_CFG),
        _mk_pav(m, PLL_PROFILE_0_PLL_ENABLE_n, curr->ENABLE),
    };
    res = lms8001_spi_post(m, lms_init, SIZEOF_ARRAY(lms_init));
    if (res)
        return res;

    return 0;
}


int lms8001_reg_set(lms8001_state_t* m, uint16_t addr, uint16_t val)
{
    // Update internal state to syncronze cahce values

    if (addr >= PLL_CONFIGURATION_PLL_VREG) {
        switch (addr) {
        case PLL_CONFIGURATION_PLL_VREG: m->pll.PLL_VREG = val; break;
        case PLL_CONFIGURATION_PLL_CFG_XBUF: m->pll.PLL_CFG_XBUF = val; break;
        case PLL_CONFIGURATION_PLL_CAL_AUTO0: break;
        case PLL_CONFIGURATION_PLL_CAL_AUTO1: m->pll.PLL_CAL_AUTO1 = val; break;
        case PLL_CONFIGURATION_PLL_CAL_AUTO2: m->pll.PLL_CAL_AUTO2 = val; break;
        case PLL_CONFIGURATION_PLL_CAL_AUTO3: m->pll.PLL_CAL_AUTO3 = val; break;
        case PLL_CONFIGURATION_PLL_CAL_MAN: break;
        case PLL_CONFIGURATION_PLL_CFG_SEL0: break;
        case PLL_CONFIGURATION_PLL_CFG_SEL1: break;
        case PLL_CONFIGURATION_PLL_CFG_SEL2: break;
        case PLL_CONFIGURATION_PLL_CFG: m->pll.PLL_CFG = val; break;
        case PLL_CONFIGURATION_PLL_CFG_STATUS: break;
        case PLL_CONFIGURATION_PLL_LODIST_CFG1: /* m->pll.PLL_LODIST_CFG1 = val; */ break;
        case PLL_CONFIGURATION_PLL_LODIST_CFG2: /* m->pll.PLL_LODIST_CFG2 = val; */ break;
        case PLL_CONFIGURATION_PLL_SDM_BIST1: break;
        }

    } else if (addr >= PLL_PROFILE_0_PLL_ENABLE_n) {
        unsigned profile = (addr - PLL_PROFILE_0_PLL_ENABLE_n) / (PLL_PROFILE_1_PLL_ENABLE_n - PLL_PROFILE_0_PLL_ENABLE_n);
        unsigned addr_norm = addr - profile * (PLL_PROFILE_1_PLL_ENABLE_n - PLL_PROFILE_0_PLL_ENABLE_n);
        lms8001_pll_state_t* curr = &m->pll_profiles[profile];

        if (profile < 8) {
            switch (addr_norm) {
            case PLL_PROFILE_0_PLL_ENABLE_n: curr->ENABLE = val; break;
            case PLL_PROFILE_0_PLL_LPF_CFG1_n: curr->LPF_CFG1 = val; break;
            case PLL_PROFILE_0_PLL_LPF_CFG2_n: curr->LPF_CFG2 = val; break;
            case PLL_PROFILE_0_PLL_CP_CFG0_n: curr->CP_CFG0 = val; break;
            case PLL_PROFILE_0_PLL_CP_CFG1_n: curr->CP_CFG1 = val; break;
            case PLL_PROFILE_0_PLL_VCO_FREQ_n: curr->VCO_FREQ = val; break;
            case PLL_PROFILE_0_PLL_VCO_CFG_n: curr->VCO_CFG = val; break;
            case PLL_PROFILE_0_PLL_FF_CFG_n: curr->FF_CFG = val; break;
            case PLL_PROFILE_0_PLL_SDM_CFG_n: curr->SDM_CFG = val; break;
            case PLL_PROFILE_0_PLL_FRACMODL_n: curr->FRACMODL = val; break;
            case PLL_PROFILE_0_PLL_FRACMODH_n: curr->FRACMODH = val; break;
            case PLL_PROFILE_0_PLL_LODIST_CFG_n: curr->LODIST_CFG = val; break;
            case PLL_PROFILE_0_PLL_FLOCK_CFG1_n: curr->FLOCK_CFG1 = val; break;
            case PLL_PROFILE_0_PLL_FLOCK_CFG2_n: curr->FLOCK_CFG2 = val; break;
            case PLL_PROFILE_0_PLL_FLOCK_CFG3_n: curr->FLOCK_CFG3 = val; break;
            }
        }
    }

    uint32_t reg = MAKE_LMS8001_REG_WR(addr, val);
    return lms8001_spi_post(m, &reg, 1);
}

int lms8001_reg_get(lms8001_state_t* m, uint16_t addr, uint16_t* oval)
{
    return lms8001_spi_get(m, addr, oval);
}


int lms8001_temp_start(lms8001_state_t* m)
{
    uint32_t lms_init[] = {
        MAKE_LMS8001_CHIPCONFIG_TEMP_SENS(1, 1, 1, 0),
    };
    return lms8001_spi_post(m, lms_init, SIZEOF_ARRAY(lms_init));
}

int lms8001_temp_get(lms8001_state_t* m, int* temp256)
{
    uint16_t rb;
    int res = lms8001_spi_get(m, CHIPCONFIG_TEMP_SENS, &rb);
    if (res)
        return res;

    float v = GET_LMS8001_CHIPCONFIG_TEMP_SENS_TEMP_READ(rb);
    *temp256 = 256.0 * (TEMPSENS_T0 + TEMPSENS_T1 * v + TEMPSENS_T2 * v * v);
    return 0;
}



















