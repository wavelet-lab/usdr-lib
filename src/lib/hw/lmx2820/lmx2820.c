// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <inttypes.h>
#include <assert.h>
#include <math.h>

#include "lmx2820.h"
#include "def_lmx2820.h"
#include <usdr_logging.h>

enum {

    OSC_IN_MIN = 5000000ull,
    OSC_IN_MAX = 1400000000ull,
    OSC_IN_MAX_DBLR = 250000000ull,

    OUT_FREQ_MIN =    45000000ull,
    OUT_FREQ_MAX = 22600000000ull,

    VCO_MIN =  5650000000ull,
    VCO_MAX = 11300000000ull,

    PLL_R_PRE_DIV_MIN = 1,
    PLL_R_PRE_DIV_MAX = 4095,

    MULT_IN_FREQ_MIN = 30000000ull,
    MULT_IN_FREQ_MAX = 70000000ull,
    MULT_OUT_FREQ_MIN = 180000000ull,
    MULT_OUT_FREQ_MAX = 250000000ull,

    MULT_MIN = MULT_X3,
    MULT_MAX = MULT_X7,

    FPD_MIN = 5000000ull,

    PLL_R_DIV_MIN = 1,
    PLL_R_DIV_MAX = 255,
    PLL_R_DIV_2_IN_FREQ_MAX = 500000000ull,
    PLL_R_DIV_GT2_IN_FREQ_MAX = 250000000ull,

    OUT_DIV_LOG2_MIN = 1,
    OUT_DIV_LOG2_MAX = 7,
};

#define VCO_ACCURACY 0.1f
#define RF_ACCURACY 1.0f
#define OUT_DIV_DIAP_MAX (OUT_DIV_LOG2_MAX - OUT_DIV_LOG2_MIN + 1 + 1)

//Pin3 bias capacitor, uF
#define C_BIAS 4.7f

enum {
    PLL_N_MIN = 12,
    PLL_N_MAX = 32767,
};

struct range {
    uint64_t min, max;
};
typedef struct range range_t;

struct vco_core {
    range_t freq;
    uint16_t ndiv_min[MASH_ORDER_THIRD_ORDER + 1];
};
typedef struct vco_core vco_core_t;

static vco_core_t VCO_CORES[VCO_SEL_VCO7] =
{
    {{VCO_MIN,       6350000000}, {12,18,19,24}},
    {{6350000000,    7300000000}, {14,21,22,26}},
    {{7300000000,    8100000000}, {16,23,24,26}},
    {{8100000000,    9000000000}, {16,26,27,29}},
    {{9000000000,    9800000000}, {18,28,29,31}},
    {{9800000000,   10600000000}, {18,30,31,33}},
    {{10600000000,  VCO_MAX + 1}, {20,33,34,36}}
};

static uint64_t FPD_MAX[MASH_ORDER_THIRD_ORDER + 1] =
{
    400000000, 300000000, 300000000, 250000000
};

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define INSTCAL_R0_MASK (((uint16_t)1 << FCAL_EN_OFF) | ((uint16_t)1 << DBLR_CAL_EN_OFF) | ((uint16_t)1 << INSTCAL_SKIP_ACAL_OFF))

static int lmx2820_spi_post(lmx2820_state_t* obj, uint32_t* regs, unsigned count)
{
    int res;
    for (unsigned i = 0; i < count; i++) {
        res = lowlevel_spi_tr32(obj->dev, obj->subdev, obj->lsaddr, regs[i], NULL);
        if (res)
            return res;

        USDR_LOG("2820", USDR_LOG_NOTE, "[%d/%d] reg wr %08x\n", i, count, regs[i]);
    }

    return 0;
}

static int lmx2820_spi_get(lmx2820_state_t* obj, uint16_t addr, uint16_t* out)
{
    uint32_t v;
    int res = lowlevel_spi_tr32(obj->dev, obj->subdev, obj->lsaddr, addr << 16, &v);
    if (res)
        return res;

    USDR_LOG("2820", USDR_LOG_NOTE, " reg rd %04x => %08x\n", addr, v);
    *out = v;
    return 0;
}


static int lmx2820_get_worst_vco_core(uint64_t vco_freq, unsigned mash_order, unsigned* vco_core, uint16_t* min_pll_n)
{
    if( vco_freq < VCO_MIN || vco_freq > VCO_MAX ||
        mash_order > MASH_ORDER_THIRD_ORDER)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "VCO core detection failed [VCO:%" PRIu64 " MASH_ORDER:%d]", vco_freq, mash_order);
        return -EINVAL;
    }

    for(unsigned i = 0; i < VCO_SEL_VCO7; ++i)
    {
        const vco_core_t r = VCO_CORES[i];
        if(vco_freq >= r.freq.min && vco_freq < r.freq.max)
        {
            if(vco_core)
                *vco_core = i + 1;
            if(min_pll_n)
                *min_pll_n = r.ndiv_min[mash_order];
            USDR_LOG("2820", USDR_LOG_DEBUG, "VCO:%" PRIu64 " -> VCO_CORE%d PLL_N_MIN:%d", vco_freq, (i + 1), r.ndiv_min[mash_order]);
            return 0;
        }
    }

    return -EINVAL;
}


static int lmx2820_reset(lmx2820_state_t* st)
{
    memset(st, 0, sizeof(*st));

    uint16_t r0;

    int res = lmx2820_spi_get(st, R0, &r0);
    if(res)
        return res;

    r0 |= (uint16_t)1 << RESET_OFF;

    uint32_t reg = MAKE_LMX2820_REG_WR(R0, r0);

    res = lmx2820_spi_post(st, &reg, 1);
    if(res)
        return res;

    usleep(5); //reset takes <1us
    return 0;
}

static int lmx2820_calibrate(lmx2820_state_t* st, bool set_flag)
{
    uint16_t r0;

    int res = lmx2820_spi_get(st, R0, &r0);
    if(res)
        return res;

    if(set_flag)
        r0 |= (uint16_t)1 << FCAL_EN_OFF;
    else
        r0 &= ~((uint16_t)1 << FCAL_EN_OFF);

    uint32_t reg = MAKE_LMX2820_REG_WR(R0, r0);

    return lmx2820_spi_post(st, &reg, 1);
}

static int lmx2820_wait_pll_lock(lmx2820_state_t* st, unsigned timeout)
{
    int res = 0;
    unsigned elapsed = 0;

    uint16_t r74;
    while(timeout == 0 || elapsed < timeout)
    {
        res = lmx2820_spi_get(st, R74, &r74);
        if(res)
            return res;

        const uint16_t lock_detect_status = (r74 & RB_LD_MSK) >> RB_LD_OFF;
        switch(lock_detect_status)
        {
        case RB_LD_INVALID: return -EINVAL;
        case RB_LD_LOCKED: return 0;
        default:
            usleep(100);
            elapsed += 100;
        }
    }

    return -ETIMEDOUT;
}

int lmx2820_get_temperature(lmx2820_state_t* st, float* value)
{
    if(!value)
        return -EINVAL;

    uint16_t r76;

    int res = lmx2820_spi_get(st, R76, &r76);
    if(res)
        return res;

    int16_t code = (r76 & RB_TEMP_SENS_MSK) >> RB_TEMP_SENS_OFF;
    *value = 0.85f * code - 415.0f;

    return 0;
}

static inline const char* lmx2820_decode_lock_status(uint8_t ld)
{
    switch(ld)
    {
    case RB_LD_UNLOCKED0:
    case RB_LD_UNLOCKED1: return "UNLOCKED";
    case RB_LD_LOCKED: return "LOCKED";
    case RB_LD_INVALID: return "INVALID";
    }
    return "UNKNOWN";
}

int lmx2820_read_status(lmx2820_state_t* st, lmx2820_stats_t* status)
{
    if(!status)
        return -EINVAL;

    uint16_t r74, r75;

    int res = lmx2820_get_temperature(st, &status->temperature);
    res = res ? res : lmx2820_spi_get(st, R74, &r74);
    res = res ? res : lmx2820_spi_get(st, R75, &r75);
    if(res)
        return res;

    status->lock_detect_status = (r74 & RB_LD_MSK) >> RB_LD_OFF;
    status->vco_capctrl = (r74 & RB_VCO_CAPCTRL_MSK) >> RB_VCO_CAPCTRL_OFF;
    status->vco_sel = (r74 & RB_VCO_SEL_MSK) >> RB_VCO_SEL_OFF;
    status->vco_daciset = (r75 & RB_VCO_DACISET_MSK) >> RB_VCO_DACISET_OFF;

    USDR_LOG("2820", USDR_LOG_DEBUG, "STATUS> Temp:%.2fC LOCK:%d(%s) VCO_CAPCTRL:%d VCO_SEL:%d VCO_DACISET:%d",
                    status->temperature, status->lock_detect_status,
                    lmx2820_decode_lock_status(status->lock_detect_status),
                    status->vco_capctrl, status->vco_sel, status->vco_daciset
             );

    return 0;
}

int lmx2820_create(lldev_t dev, unsigned subdev, unsigned lsaddr, lmx2820_state_t* st)
{
    st->dev = dev;
    st->subdev = subdev;
    st->lsaddr = lsaddr;

    int res = lmx2820_reset(st);
    if(res)
        return res;

    //this list is incompleted
    uint32_t regs[] =
    {
        MAKE_LMX2820_R19(0x109, TEMPSENSE_EN_ENABLED, 0x0), //enable temperature sensor
    };

    res = lmx2820_spi_post(st, regs, sizeof(regs));
    if(res)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "Registers set lmx2820_spi_post() failed, err:%d", res);
        return res;
    }

    usleep(10);

    lmx2820_stats_t status;
    res = lmx2820_read_status(st, &status);
    if(res)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "Status check failed, err:%d", res);
        return res;
    }

    USDR_LOG("2820", USDR_LOG_DEBUG, "Create OK");
    return 0;
}

int lmx2820_destroy(lmx2820_state_t* st)
{
    USDR_LOG("2820", USDR_LOG_DEBUG, "Destroy OK");
    return 0;
}


static int lmx2820_tune_vco(lmx2820_state_t* st, uint64_t vco)
{
    int res = 0;
    lmx2820_input_chain_t * settings = &st->lmx2820_input_chain;

    uint16_t min_n_total;
    uint16_t max_n_total = PLL_N_MAX + 1;

    unsigned vco_core;
    res = lmx2820_get_worst_vco_core(vco, settings->mash_order, &vco_core, &min_n_total);
    if(res)
        return res;

    double n_total = (double)vco / settings->fpd;
    USDR_LOG("2820", USDR_LOG_DEBUG, "N_total:%.6f", n_total);

    if(n_total < min_n_total || n_total > max_n_total)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "Ntotal:%.6f out of range [%u;%u)", n_total, min_n_total, max_n_total);
        return -EINVAL;
    }

    uint16_t pll_n = (uint16_t)n_total;
    double pll_frac = n_total - pll_n;
    USDR_LOG("2820", USDR_LOG_DEBUG, "PLL_N:%u PLL_FRAC:%.8f", pll_n, pll_frac);

    uint32_t pll_den = UINT32_MAX;
    uint32_t pll_num = pll_frac * pll_den;
    double vco_fact = (double)settings->fpd * (pll_n + (double)pll_num / pll_den);

    double delta = fabs(vco_fact - vco);
    USDR_LOG("2820", USDR_LOG_DEBUG, "PLL_N:%u PLL_NUM:%u PLL_DEN:%u VCO:%.2f Deviation:%.8fHz", pll_n, pll_num, pll_den, vco_fact, delta);

    if(delta > VCO_ACCURACY)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "VCO tuning too rough");
        return -EINVAL;
    }

    settings->vco_core = vco_core;
    settings->pll_n = pll_n;
    settings->pll_num = pll_num;
    settings->pll_den = pll_den;
    settings->fvco = vco_fact;

    return 0;
}

static int lmx2820_calculate_input_chain(lmx2820_state_t* st, uint64_t fosc_in, uint64_t vco, unsigned mash_order, unsigned force_mult)
{
    int res = 0;
    unsigned mult, pll_r_pre, pll_r;

    uint16_t min_n_total;
    uint16_t max_n_total = PLL_N_MAX + 1;

    res = lmx2820_get_worst_vco_core(vco, mash_order, NULL, &min_n_total);
    if(res)
        return res;

    uint64_t fpd_max = FPD_MAX[mash_order];
    uint64_t fpd_min = FPD_MIN;

    fpd_max = MIN(fpd_max, vco / min_n_total);
    fpd_min = MAX(fpd_min, vco / max_n_total);
    USDR_LOG("2820", USDR_LOG_DEBUG, "VCO:%" PRIu64 "    NMIN:%u NMAX:%u    FPD_MIN:%" PRIu64 " FPD_MAX:%" PRIu64,
                                     vco, min_n_total, max_n_total, fpd_min, fpd_max);

    bool need_mult = (fosc_in < fpd_min) || force_mult;
    const bool osc_2x = (fosc_in <= OSC_IN_MAX_DBLR && !need_mult);
    uint64_t osc_in = fosc_in * (osc_2x ? 2 : 1);
    USDR_LOG("2820", USDR_LOG_DEBUG, "OSC_2X:%d -> effective OSC_IN:%" PRIu64, osc_2x, osc_in);

    if((osc_in < fpd_min) || force_mult)
    {
        if(force_mult)
            USDR_LOG("2820", USDR_LOG_DEBUG, "Mult:%d forced by user", force_mult);
        else
            USDR_LOG("2820", USDR_LOG_DEBUG, "Need mult");

        //need mult
        mult = MAX(force_mult ? force_mult : (unsigned)ceil((double)fpd_min / osc_in), MULT_MIN);
        if(mult > MULT_MAX)
        {
            USDR_LOG("2820", USDR_LOG_ERROR, "Mult:%d out of range", mult);
            return -EINVAL;
        }

        pll_r_pre = 1;
        pll_r = 1;

        if(osc_in < MULT_IN_FREQ_MIN)
        {
            USDR_LOG("2820", USDR_LOG_ERROR, "OSC_IN:%" PRIu64" too low for mult, set %" PRIu64 " at least", fosc_in, (uint64_t)MULT_IN_FREQ_MIN/2);
            return -EINVAL;
        }

        if(osc_in > MULT_IN_FREQ_MAX)
        {
            pll_r_pre = (unsigned)ceil((double)osc_in / MULT_IN_FREQ_MAX);
        }

        uint64_t freq_pre = osc_in / pll_r_pre;
        uint64_t freq_mult = freq_pre * mult;

        while(freq_mult < MULT_OUT_FREQ_MIN)
        {
            if(mult == MULT_MAX)
                return -EINVAL;
            freq_mult = freq_pre * (++mult);
            if(freq_mult > MULT_OUT_FREQ_MAX)
                return -EINVAL;
        }

        while(freq_mult > MULT_OUT_FREQ_MAX)
        {
            if(mult == MULT_MIN)
                return -EINVAL;
            freq_mult = freq_pre * (--mult);
            if(freq_mult < MULT_OUT_FREQ_MIN)
                return -EINVAL;
        }

        if(freq_mult > fpd_max)
        {
            pll_r = (unsigned)ceil((double)freq_mult / fpd_max);
        }
    }
    else if(osc_in > fpd_max)
    {
        USDR_LOG("2820", USDR_LOG_DEBUG, "Need divs");

        //no need for mult, but need for divs
        mult = 1;
        unsigned div = (unsigned)ceil((double)osc_in / fpd_max);
        if(div > PLL_R_PRE_DIV_MAX * PLL_R_DIV_MAX)
            return -EINVAL;


        if(div <= PLL_R_PRE_DIV_MAX)
        {
            pll_r_pre = div;
            pll_r = 1;
        }
        else
        {
            pll_r_pre = PLL_R_PRE_DIV_MAX;
            pll_r = (unsigned)ceil((double)div / PLL_R_PRE_DIV_MAX);
        }

        USDR_LOG("2820", USDR_LOG_DEBUG, "TOTAL_DIV:%u PLL_R_PRE:%u PLL_R:%u", div, pll_r_pre, pll_r);
    }
    else
    {
        //no need neither for mult or for divs
        mult = 1;
        pll_r_pre = 1;
        pll_r = 1;
    }

    if(pll_r > PLL_R_DIV_MAX)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "PLL_R:%d out of range", pll_r);
        return -EINVAL;
    }

    uint64_t f_in_pll_r = osc_in * mult / pll_r_pre;
    uint64_t max_f_in_pll_r = (pll_r <= 2) ? PLL_R_DIV_2_IN_FREQ_MAX : PLL_R_DIV_GT2_IN_FREQ_MAX;
    if(f_in_pll_r > max_f_in_pll_r)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "Input freq for PLL_R:%d is out of range, %" PRIu64 " > %" PRIu64, pll_r, f_in_pll_r, max_f_in_pll_r);
        return -EINVAL;
    }

    uint64_t fpd = (uint64_t)((double)osc_in * mult / (pll_r_pre * pll_r) + 0.5);
    USDR_LOG("2820", USDR_LOG_DEBUG, "For VCO:%" PRIu64 " -> FPD:%" PRIu64, vco, fpd);

    if(fpd < fpd_min || fpd > fpd_max)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "FPD:%" PRIu64 " out of range: should never be happen!", fpd);
        return -EINVAL;
    }

    lmx2820_input_chain_t * settings = &st->lmx2820_input_chain;

    settings->fosc_in = fosc_in;
    settings->osc_2x = osc_2x;
    settings->mash_order = mash_order;
    settings->pll_r_pre = pll_r_pre;
    settings->mult = mult;
    settings->pll_r = pll_r;
    settings->fpd = fpd;

    res = lmx2820_tune_vco(st, vco);
    if(res)
        return res;

    USDR_LOG("2820", USDR_LOG_WARNING, "Input circuit res: OSC_IN:%" PRIu64 " OSC_2X:%d PLL_R_PRE:%d MULT:%d PLL_R:%d FPD:%.0f PLL_N:%u PLL_NUM:%u PLL_DEN:%u VCO:%.2f",
             settings->fosc_in,
             settings->osc_2x,
             settings->pll_r_pre,
             settings->mult,
             settings->pll_r,
             settings->fpd,
             settings->pll_n,
             settings->pll_num,
             settings->pll_den,
             settings->fvco);

    return 0;
}


static int lmx2820_solver_prepare(uint64_t rfouta, uint64_t rfoutb, uint64_t* vco, uint8_t* diva, uint8_t* divb, uint8_t* muxa, uint8_t* muxb)
{
    uint64_t *rf_max, *rf_min;
    unsigned mux_max, mux_min;

    if(rfouta > rfoutb)
    {
        rf_max = &rfouta;
        rf_min = &rfoutb;
    }
    else
    {
        rf_max = &rfoutb;
        rf_min = &rfouta;
    }

    double rf_ratio = log2((double)(*rf_max)/(*rf_min));
    unsigned rf_ratio_n = (unsigned)rf_ratio;

    if(fabs(rf_ratio - rf_ratio_n) > 1E-8)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "RFOUT A/B ratio must be == 2^N");
        return -EINVAL;
    }

    if(rf_ratio_n > OUT_DIV_DIAP_MAX)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "RFOUT ratio:%d > %d, out of range", rf_ratio_n, (int)OUT_DIV_DIAP_MAX);
        return -EINVAL;
    }

    UNUSED uint64_t rf_max_fact, rf_min_fact;
    uint8_t div_max = 1, div_min = 1;

    if(*rf_max > VCO_MAX)
    {
        //rf_max is doubled VCO
        //rf_min may be VCO or VCO/2..7

        mux_max = OUTA_MUX_VCO_DOUBLER;
        *vco = (uint64_t)((double)(*rf_max) / 2 + 0.5);
        rf_max_fact = *vco << 1;

        switch(rf_ratio_n)
        {
        case 0: rf_min_fact = rf_max_fact; mux_min = OUTA_MUX_VCO_DOUBLER; break;
        case 1: rf_min_fact = *vco; mux_min = OUTA_MUX_VCO; break;
        default:
            div_min = rf_ratio_n - 1;
            rf_min_fact = *vco >> div_min;
            mux_min = OUTA_MUX_CHANNEL_DIVIDER;
            if(div_min == OUT_DIV_LOG2_MAX)
            {
                div_max = div_min;
            }
        }
    }
    else if(*rf_max < VCO_MIN)
    {
        //both rf_max & rf_min are VCO/2..7
        //rf_ratio_n must be <=6

        if(rf_ratio_n > OUT_DIV_DIAP_MAX - 2)
        {
            USDR_LOG("2820", USDR_LOG_ERROR, "RFOUT ratio:%d > %d, out of range", rf_ratio_n, (int)OUT_DIV_DIAP_MAX - 2);
            return -EINVAL;
        }

        *vco = MAX((*rf_max) << OUT_DIV_LOG2_MIN, VCO_MIN);
        div_max = (uint8_t)ceil(log2((double)(*vco) / (*rf_max)));
        div_max = MAX(div_max, OUT_DIV_LOG2_MIN);
        div_min = div_max + rf_ratio_n;

        if(div_max < OUT_DIV_LOG2_MIN || div_max > OUT_DIV_LOG2_MAX || div_min < OUT_DIV_LOG2_MIN || div_min > OUT_DIV_LOG2_MAX)
        {
            USDR_LOG("2820", USDR_LOG_ERROR, "Cannot calculate dividers for RFs specified (DIV_MIN:%d DIV_MAX:%d)", div_min, div_max);
            return -EINVAL;
        }

        if((div_min == OUT_DIV_LOG2_MAX || div_max == OUT_DIV_LOG2_MAX) && div_min != div_max)
        {
            USDR_LOG("2820", USDR_LOG_ERROR, "Invalid RF dividers configuration (DIV_MIN:%d DIV_MAX:%d)", div_min, div_max);
            return -EINVAL;
        }

        *vco = (*rf_max) << div_max;
        rf_max_fact = *vco >> div_max;
        rf_min_fact = *vco >> div_min;
        mux_min = mux_max = OUTA_MUX_CHANNEL_DIVIDER;
    }
    else
    {
        //rf_max == VCO
        //rf_min - VCO if rfa==rfb, or VCO/2..7
        //rf_ratio_n must be <=7

        if(rf_ratio_n > OUT_DIV_DIAP_MAX - 1)
        {
            USDR_LOG("2820", USDR_LOG_ERROR, "RFOUT ratio:%d > %d, out of range", rf_ratio_n, (int)OUT_DIV_DIAP_MAX - 1);
            return -EINVAL;
        }

        *vco = *rf_max;
        rf_max_fact = *vco;
        mux_max = OUTA_MUX_VCO;

        switch(rf_ratio_n)
        {
        case 0: rf_min_fact = rf_max_fact; mux_min = OUTA_MUX_VCO; break;
        default:
            div_min = rf_ratio_n;
            rf_min_fact = *vco >> div_min;
            mux_min = OUTA_MUX_CHANNEL_DIVIDER;
            if(div_min == OUT_DIV_LOG2_MAX)
            {
                div_max = div_min;
            }
        }
    }

    *diva   = (&rfouta == rf_min) ? div_min : div_max;
    *divb   = (&rfoutb == rf_min) ? div_min : div_max;
    *muxa = (&rfouta == rf_min) ? mux_min : mux_max;
    *muxb = (&rfoutb == rf_min) ? mux_min : mux_max;

    if(*diva < OUT_DIV_LOG2_MIN || *diva > OUT_DIV_LOG2_MAX || *divb < OUT_DIV_LOG2_MIN || *divb > OUT_DIV_LOG2_MAX)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "RFOUT dividers out of range (DIV_A:%d DIV_B:%d) [should never happen]", *diva, *divb);
        return -EINVAL;
    }

    return 0;
}

static int lmx2820_solver_validate_and_save(lmx2820_state_t* st, uint64_t rfouta, uint64_t rfoutb, uint8_t diva, uint8_t divb, uint8_t muxa, uint8_t muxb)
{
    double fvco = st->lmx2820_input_chain.fvco;
    double rfa, rfb;

    switch(muxa)
    {
    case OUTA_MUX_VCO_DOUBLER:     rfa = fvco * 2.0; break;
    case OUTA_MUX_VCO:             rfa = fvco; break;
    case OUTA_MUX_CHANNEL_DIVIDER: rfa = fvco / ((unsigned)1 << diva); break;
    default:
        return -EINVAL;
    }
    switch(muxb)
    {
    case OUTA_MUX_VCO_DOUBLER:     rfb = fvco * 2.0; break;
    case OUTA_MUX_VCO:             rfb = fvco; break;
    case OUTA_MUX_CHANNEL_DIVIDER: rfb = fvco / ((unsigned)1 << divb); break;
    default:
        return -EINVAL;
    }

    double rfa_delta = fabs(rfouta - rfa);
    double rfb_delta = fabs(rfoutb - rfb);

    USDR_LOG("2820", USDR_LOG_DEBUG, "RF_A:%" PRIu64 "->%.6f Deviation:%.8fHz", rfouta, rfa, rfa_delta);
    USDR_LOG("2820", USDR_LOG_DEBUG, "RF_B:%" PRIu64 "->%.6f Deviation:%.8fHz", rfoutb, rfb, rfb_delta);

    if(rfa_delta > RF_ACCURACY || rfb_delta > RF_ACCURACY)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "RF tuning too rough");
        return -EINVAL;
    }

    lmx2820_output_chain_t * outs = &st->lmx2820_output_chain;
    outs->chdiva   = diva;
    outs->chdivb   = divb;
    outs->rfouta   = rfa;
    outs->rfoutb   = rfb;
    outs->outa_mux = muxa;
    outs->outb_mux = muxb;

    return 0;
}

int lmx2820_solver(lmx2820_state_t* st, uint64_t osc_in, unsigned mash_order, unsigned force_mult, uint64_t rfouta, uint64_t rfoutb)
{
    int res = 0;

    if(osc_in < OSC_IN_MIN || osc_in > OSC_IN_MAX)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "OSC_IN %" PRIu64 " out of range [%" PRIu64 ";%" PRIu64 "]", osc_in, (uint64_t)OSC_IN_MIN, (uint64_t)OSC_IN_MAX);
        return -EINVAL;
    }

    switch(mash_order)
    {
    case MASH_ORDER_INTEGER_MODE:
    case MASH_ORDER_FIRST_ORDER:
    case MASH_ORDER_SECOND_ORDER:
    case MASH_ORDER_THIRD_ORDER: break;
    default: {
        USDR_LOG("2820", USDR_LOG_ERROR, "MASH_ORDER %u out of range [%u;%u]", mash_order, (unsigned)MASH_ORDER_INTEGER_MODE, (unsigned)MASH_ORDER_THIRD_ORDER);
        return -EINVAL;
    }
    }

    if(rfouta < OUT_FREQ_MIN || rfouta > OUT_FREQ_MAX)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "RF_OUTA %" PRIu64 " out of range [%" PRIu64 ";%" PRIu64 "]", rfouta, (uint64_t)OUT_FREQ_MIN, (uint64_t)OUT_FREQ_MAX);
        return -EINVAL;
    }
    if(rfoutb < OUT_FREQ_MIN || rfoutb > OUT_FREQ_MAX)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "RF_OUTB %" PRIu64 " out of range [%" PRIu64 ";%" PRIu64 "]", rfoutb, (uint64_t)OUT_FREQ_MIN, (uint64_t)OUT_FREQ_MAX);
        return -EINVAL;
    }

    uint64_t vco;
    uint8_t diva, divb, muxa, muxb;

    res = lmx2820_solver_prepare(rfouta, rfoutb, &vco, &diva, &divb, &muxa, &muxb);
    if(res)
        return res;

    res = lmx2820_calculate_input_chain(st, osc_in, vco, mash_order, force_mult);
    if(res)
        return res;

    res = lmx2820_solver_validate_and_save(st, rfouta, rfoutb, diva, divb, muxa, muxb);
    if(res)
        return res;

    lmx2820_output_chain_t * outs = &st->lmx2820_output_chain;

    USDR_LOG("2820", USDR_LOG_WARNING, "***** SOLUTION *****");
    USDR_LOG("2820", USDR_LOG_WARNING, "VCO:%.2f OSC_IN:%" PRIu64 " MASH_ORDER:%u VCO_CORE%u", st->lmx2820_input_chain.fvco, osc_in, mash_order, st->lmx2820_input_chain.vco_core);
    USDR_LOG("2820", USDR_LOG_WARNING, "CH_A - RF:%.2f DIV:%u(%u) MUX:%s", outs->rfouta, outs->chdiva - 1, (1 << outs->chdiva),
             outs->outa_mux == OUTA_MUX_VCO_DOUBLER ? "OUTA_MUX_VCO_DOUBLER" : (outs->outa_mux == OUTA_MUX_VCO ? "OUTA_MUX_VCO": "OUTA_MUX_CHANNEL_DIVIDER"));
    USDR_LOG("2820", USDR_LOG_WARNING, "CH_B - RF:%.2f DIV:%u(%u) MUX:%s", outs->rfoutb, outs->chdivb - 1, (1 << outs->chdivb),
             outs->outb_mux == OUTB_MUX_VCO_DOUBLER ? "OUTB_MUX_VCO_DOUBLER" : (outs->outb_mux == OUTB_MUX_VCO ? "OUTB_MUX_VCO": "OUTB_MUX_CHANNEL_DIVIDER"));

    return 0;
}

int lmx2820_solver_instcal(lmx2820_state_t* st, uint64_t rfouta, uint64_t rfoutb)
{
    int res = 0;

    unsigned mash_order = st->lmx2820_input_chain.mash_order;
    if(mash_order > MASH_ORDER_THIRD_ORDER)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "MASH_ORDER:%u out of range, maybe uninitialized", mash_order);
        return -EINVAL;
    }

    double fpd = st->lmx2820_input_chain.fpd;
    if(fpd < FPD_MIN || fpd > FPD_MAX[mash_order])
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "FPD:%.2f out of range, maybe uninitialized", fpd);
        return -EINVAL;
    }

    const double rfmax = MIN(fpd * (PLL_N_MAX + 1) * 2.0, OUT_FREQ_MAX);
    const double rfmin = MAX(fpd * PLL_N_MIN / (1 << OUT_DIV_LOG2_MAX), OUT_FREQ_MIN);

    if(rfouta < rfmin || rfouta > rfmax)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "RF_A:%" PRIu64 " out of range [%.2f;%.2f] for FPD:%.2f", rfouta, rfmin, rfmax, fpd);
        return -EINVAL;
    }
    if(rfoutb < rfmin || rfoutb > rfmax)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "RF_B:%" PRIu64 " out of range [%.2f;%.2f] for FPD:%.2f", rfoutb, rfmin, rfmax, fpd);
        return -EINVAL;
    }

    uint64_t vco;
    uint8_t diva, divb, muxa, muxb;

    res = lmx2820_solver_prepare(rfouta, rfoutb, &vco, &diva, &divb, &muxa, &muxb);
    if(res)
        return res;

    res = lmx2820_tune_vco(st, vco);
    if(res)
        return res;

    res = lmx2820_solver_validate_and_save(st, rfouta, rfoutb, diva, divb, muxa, muxb);
    if(res)
        return res;

    lmx2820_output_chain_t * outs = &st->lmx2820_output_chain;

    USDR_LOG("2820", USDR_LOG_WARNING, "***** INSTCAL SOLUTION *****");
    USDR_LOG("2820", USDR_LOG_WARNING, "VCO:%.2f", st->lmx2820_input_chain.fvco);
    USDR_LOG("2820", USDR_LOG_WARNING, "CH_A - RF:%.2f DIV:%u(%u) MUX:%s", outs->rfouta, outs->chdiva - 1, (1 << outs->chdiva),
             outs->outa_mux == OUTA_MUX_VCO_DOUBLER ? "OUTA_MUX_VCO_DOUBLER" : (outs->outa_mux == OUTA_MUX_VCO ? "OUTA_MUX_VCO": "OUTA_MUX_CHANNEL_DIVIDER"));
    USDR_LOG("2820", USDR_LOG_WARNING, "CH_B - RF:%.2f DIV:%u(%u) MUX:%s", outs->rfoutb, outs->chdivb - 1, (1 << outs->chdivb),
             outs->outb_mux == OUTB_MUX_VCO_DOUBLER ? "OUTB_MUX_VCO_DOUBLER" : (outs->outb_mux == OUTB_MUX_VCO ? "OUTB_MUX_VCO": "OUTB_MUX_CHANNEL_DIVIDER"));

    return 0;
}

static int lmx2820_tune_internal(lmx2820_state_t* st, uint64_t osc_in, unsigned mash_order, unsigned force_mult, uint64_t rfouta, uint64_t rfoutb, bool use_instcal)
{
    int res = lmx2820_solver(st, osc_in, mash_order, force_mult, rfouta, rfoutb);
    if(res)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "lmx2820_solver() failed, err:%d", res);
        return res;
    }

    uint8_t HP_fd_adj, LP_fd_adj;
    double fpd = st->lmx2820_input_chain.fpd;

    if(fpd < 2500000)
        LP_fd_adj = FCAL_LPFD_ADJ_FPD_LT_2_5_MHZ;
    else if(fpd < 5000000)
        LP_fd_adj = FCAL_LPFD_ADJ_5_MHZ_GT_FPD_GE_2_5_MHZ;
    else if(fpd < 10000000)
        LP_fd_adj = FCAL_LPFD_ADJ_10_MHZ_GT_FPD_GE_5_MHZ;
    else
        LP_fd_adj = FCAL_LPFD_ADJ_FPD_GE_10_MHZ;

    if(fpd <= 100000000)
        HP_fd_adj = FCAL_HPFD_ADJ_FPD_LE_100_MHZ;
    else if(fpd <= 150000000)
        HP_fd_adj = FCAL_HPFD_ADJ_100_MHZ_LT_FPD_LE_150_MHZ;
    else if(fpd <= 200000000)
        HP_fd_adj = FCAL_HPFD_ADJ_150_MHZ_LT_FPD_LE_200_MHZ;
    else
        HP_fd_adj = FCAL_HPFD_ADJ_FPD_GT_200_MHZ;

    uint8_t cal_clk_div;
    if(osc_in <= 200000000)
        cal_clk_div = CAL_CLK_DIV_FOSCIN_LE_200_MHZ;
    else if(osc_in <= 400000000)
        cal_clk_div = CAL_CLK_DIV_FOSCIN_LE_400_MHZ;
    else if(osc_in <= 800000000)
        cal_clk_div = CAL_CLK_DIV_FOSCIN_LE_800_MHZ;
    else
        cal_clk_div = CAL_CLK_DIV_ALL_OTHER_FOSCIN_VALUES;

    uint16_t instcal_dly = 0x1f4;
    uint32_t instcal_pll_num = 0;
    uint8_t  instcal_dblr_en = INSTCAL_DBLR_EN_NORMAL_OPERATION;

    if(use_instcal)
    {
        instcal_dly = (uint16_t)((2.5f * C_BIAS / 0.47) * ((double)osc_in / 1E6) / (1 << cal_clk_div) + 0.5);
        instcal_pll_num = (double)((uint64_t)1 << 32) * ((double)st->lmx2820_input_chain.pll_num / st->lmx2820_input_chain.pll_den);
        instcal_dblr_en = INSTCAL_DBLR_EN_VCO_DOUBLER_IS_ENGAGED;
    }

    st->instcal_dly = instcal_dly; //store it for instcal triggering

    USDR_LOG("2820", USDR_LOG_DEBUG, "REGS> LP_FD_ADJ:%d HP_FD_ADJ:%d CAL_CLK_DIV:%d INSTCAL_DLY:%d INSTCAL_PLL_NUM:%u INSTCAL_DBLR_EN:%d",
             LP_fd_adj, HP_fd_adj, cal_clk_div, instcal_dly, instcal_pll_num, instcal_dblr_en);

    uint32_t regs[] =
    {
        MAKE_LMX2820_R79(0, OUTB_PD_NORMAL_OPERATION, 0, st->lmx2820_output_chain.outb_mux, 0x7, 0),
        MAKE_LMX2820_R78(0, OUTA_PD_NORMAL_OPERATION, 0, st->lmx2820_output_chain.outa_mux),
        MAKE_LMX2820_R45((uint16_t)instcal_pll_num),
        MAKE_LMX2820_R44((uint16_t)(instcal_pll_num >> 16)),
        MAKE_LMX2820_R43((uint16_t)st->lmx2820_input_chain.pll_num),
        MAKE_LMX2820_R42((uint16_t)(st->lmx2820_input_chain.pll_num >> 16)),
        MAKE_LMX2820_R39((uint16_t)st->lmx2820_input_chain.pll_den),
        MAKE_LMX2820_R38((uint16_t)(st->lmx2820_input_chain.pll_den >> 16)),
        MAKE_LMX2820_R36(0, st->lmx2820_input_chain.pll_n),
        MAKE_LMX2820_R35(1, MASH_RESET_N_NORMAL_OPERATION, 0, st->lmx2820_input_chain.mash_order, MASHSEED_EN_DISABLED, 0),
        MAKE_LMX2820_R32(1, st->lmx2820_output_chain.chdivb - 1, st->lmx2820_output_chain.chdiva - 1, 1),
        MAKE_LMX2820_R22(st->lmx2820_input_chain.vco_core, 0x2, 0xBF),
        MAKE_LMX2820_R14(0x3, st->lmx2820_input_chain.pll_r_pre),
        MAKE_LMX2820_R13(0, st->lmx2820_input_chain.pll_r, 0x18),
        MAKE_LMX2820_R12(0, st->lmx2820_input_chain.mult, 0x8),
        MAKE_LMX2820_R11(0x30, st->lmx2820_input_chain.osc_2x ? 1 : 0, 0x3),
        MAKE_LMX2820_R2 (1, cal_clk_div, instcal_dly, QUICK_RECAL_EN_DISABLED),
        MAKE_LMX2820_R1 (PHASE_SYNC_EN_NORMAL_OPERATION, 0x15E, LD_VTUNE_EN_VCOCAL_AND_VTUNE_LOCK_DETECT, 0,
                        instcal_dblr_en,
                        use_instcal ? INSTCAL_EN_ENABLED : INSTCAL_EN_DISABLED),
        MAKE_LMX2820_R0 (1, 1, 0, HP_fd_adj, LP_fd_adj, DBLR_CAL_EN_ENABLED, 1, FCAL_EN_DISABLED, 0, RESET_NORMAL_OPERATION, POWERDOWN_NORMAL_OPERATION)
    };

    res = lmx2820_spi_post(st, regs, sizeof(regs));
    if(res)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "Registers set lmx2820_spi_post() failed, err:%d", res);
        return res;
    }

    //Wait 10 ms to allow the internal LDOs to power up.
    usleep(10000);

    res = lmx2820_calibrate(st, true);
    if(res)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "lmx2820_calibrate(1) failed, err:%d", res);
        return res;
    }

    if(use_instcal)
    {
        res = lmx2820_calibrate(st, false);
        if(res)
        {
            USDR_LOG("2820", USDR_LOG_ERROR, "lmx2820_calibrate(0) failed, err:%d", res);
            return res;
        }
    }

    res = lmx2820_wait_pll_lock(st, 10000);
    if(res)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "lmx2820_wait_pll_lock() failed, err:%d", res);
        return res;
    }

    return 0;
}

/*
 * Full tuning, including input circuit calculation OSC_IN->FPD->VCO->RFOUT
 */
int lmx2820_tune(lmx2820_state_t* st, uint64_t osc_in, unsigned mash_order, unsigned force_mult, uint64_t rfouta, uint64_t rfoutb)
{
    return lmx2820_tune_internal(st, osc_in, mash_order, force_mult, rfouta, rfoutb, false);
}


/*
 * Initialize LMX for instant calibration
 */
int lmx2820_instant_calibration_init(lmx2820_state_t* st, uint64_t osc_in, unsigned mash_order, unsigned force_mult)
{
    int res = lmx2820_tune_internal(st, osc_in, mash_order, force_mult, VCO_MIN, VCO_MIN, true);
    if(res)
        return res;

    USDR_LOG("2820", USDR_LOG_DEBUG, "Device is initialized for the particular phase detector frequency = %.2f", st->lmx2820_input_chain.fpd);

    return 0;
}

/*
 * Instant tuning - phase detector freq == invar.
 * Func lmx2820_instant_calibration_init MUST be called formerly.
 */
int lmx2820_tune_instcal(lmx2820_state_t* st, uint64_t rfouta, uint64_t rfoutb)
{
    int res = lmx2820_solver_instcal(st, rfouta, rfoutb);
    if(res)
        return res;

    const uint32_t instcal_pll_num = (double)((uint64_t)1 << 32) * ((double)st->lmx2820_input_chain.pll_num / st->lmx2820_input_chain.pll_den);

    uint16_t r0;
    res = lmx2820_spi_get(st, R0, &r0);
    if(res)
        return res;

    r0 &= ~INSTCAL_R0_MASK;

    uint32_t regs[] =
        {
            MAKE_LMX2820_R79(0, OUTB_PD_NORMAL_OPERATION, 0, st->lmx2820_output_chain.outb_mux, 0x7, 0),
            MAKE_LMX2820_R78(0, OUTA_PD_NORMAL_OPERATION, 0, st->lmx2820_output_chain.outa_mux),
            MAKE_LMX2820_R45((uint16_t)instcal_pll_num),
            MAKE_LMX2820_R44((uint16_t)(instcal_pll_num >> 16)),
            MAKE_LMX2820_R43((uint16_t)st->lmx2820_input_chain.pll_num),
            MAKE_LMX2820_R42((uint16_t)(st->lmx2820_input_chain.pll_num >> 16)),
            MAKE_LMX2820_R39((uint16_t)st->lmx2820_input_chain.pll_den),
            MAKE_LMX2820_R38((uint16_t)(st->lmx2820_input_chain.pll_den >> 16)),
            MAKE_LMX2820_R36(0, st->lmx2820_input_chain.pll_n),
            MAKE_LMX2820_R32(1, st->lmx2820_output_chain.chdivb - 1, st->lmx2820_output_chain.chdiva - 1, 1),
            MAKE_LMX2820_REG_WR(R0, r0)
        };

    res = lmx2820_spi_post(st, regs, sizeof(regs));
    if(res)
    {
        USDR_LOG("2820", USDR_LOG_ERROR, "Registers set lmx2820_spi_post() failed, err:%d", res);
        return res;
    }

    //no need to wait PLL lock, but should wait instcal_dly us
    usleep(st->instcal_dly);

    return 0;
}
