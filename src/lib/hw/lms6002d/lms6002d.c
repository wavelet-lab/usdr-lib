// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "lms6002d.h"
#include <usdr_port.h>
#include <usdr_logging.h>
#include <usdr_lowlevel.h>
#include <string.h>

#include <def_lms6002d.h>


enum lms6002d_vcos {
    VCO1_MIN = 6250,
    VCO1_MAX = 8100,

    VCO2_MIN = 5250,
    VCO2_MAX = 6900,

    VCO3_MIN = 4450,
    VCO3_MAX = 5650,

    VCO4_MIN = 3650,
    VCO4_MAX = 4800,
};

enum lms6002d_pll {
    PLL_MIN = 23,
    PLL_MAX = 41,
};

// Helper macros
static
int lms6002d_spi_post(lms6002d_state_t* obj, uint16_t* regs, unsigned count)
{
    int res;
    for (unsigned i = 0; i < count; i++) {
        res = lowlevel_spi_tr32(obj->dev, obj->subdev, obj->lsaddr, regs[i], NULL);
        if (res)
            return res;

        USDR_LOG("6002", USDR_LOG_NOTE, "[%d/%d] reg wr %04x\n", i, count, regs[i]);
    }

    return 0;
}

static
int lms6002d_spi_rd(lms6002d_state_t* obj, uint8_t addr, uint8_t* data)
{
    uint32_t rd;
    int res = lowlevel_spi_tr32(obj->dev, obj->subdev, obj->lsaddr, ((unsigned)addr << 8), &rd);
    if (res)
        return res;

    USDR_LOG("6002", USDR_LOG_NOTE, "reg rd %02x => %02x\n", addr, rd & 0xff);
    *data = (uint8_t)rd;
    return 0;
}


int lms6002d_create(lldev_t dev, unsigned subdev, unsigned lsaddr, struct lms6002d_state* out)
{
    int res;
    uint8_t rev;

    out->dev = dev;
    out->subdev = subdev;
    out->lsaddr = lsaddr;
    out->fref = 0;

    // TODO replace through options
    out->top_encfg = (uint8_t)MAKE_LMS6002D_TOP_ENCFG(0, 1, 1, 0, 0, 1);
    out->top_enreg = (uint8_t)MAKE_LMS6002D_TOP_ENREG(0, 0, 0, 0, 0, 0, 0, 0);
    out->rxpll_vco_div_bufsel = (uint8_t)MAKE_LMS6002D_RXPLL_VCO_DIV_BUFSEL(0, 0, 1);
    out->rfe_gain_lna_sel = 0xc0;

    memset(out->rclpfcal, 3, sizeof(out->rclpfcal));

    res = lms6002d_spi_rd(out, TOP_CHIPID, &rev);
    if (res)
        return res;

    uint16_t cfg_regs[] = {
        MAKE_LMS6002D_TOP_ENCFG(0, 0, 0, 0, 0, 1),

        MAKE_LMS6002D_REG_WR(TOP_ENCFG, out->top_encfg),
        MAKE_LMS6002D_REG_WR(TOP_ENREG, out->top_enreg),
        MAKE_LMS6002D_TOP_POWER(0, 1, 0, 1, 0), // XCO control, with bias

        MAKE_LMS6002D_REG_WR(TXPLL_VCO_REG_PFD_U, 0xE0),
        MAKE_LMS6002D_REG_WR(RXPLL_VCO_REG_PFD_U, 0xE3),
        MAKE_LMS6002D_REG_WR(RFE_CTRL, 0x01),

        // FAQ v1.0r12, 5.27:
        MAKE_LMS6002D_REG_WR(TRF_CTRL4, 0x40),
        MAKE_LMS6002D_REG_WR(AFE_RX_CTRL2, 0x29),
        MAKE_LMS6002D_REG_WR(RXVGA2_CTRL, 0x36),
        MAKE_LMS6002D_REG_WR(RFE_RDLINT_LNA, 0x37),

        // IQ, neg polarity
        MAKE_LMS6002D_REG_WR(AFE_MISC_CTRL, 0xb0),
    };

    res = lms6002d_spi_post(out, cfg_regs, SIZEOF_ARRAY(cfg_regs));
    if (res)
        return res;

    USDR_LOG("6002", USDR_LOG_NOTE, "lms6002d rev=%02x\n", rev);
    return (rev != 0xff) ? 0 : -ENODEV;
}

struct nint_nfrac {
    unsigned nint;
    unsigned frac;
};

static
struct nint_nfrac lms6002d_pll_calc(unsigned fref, uint64_t vco)
{
    struct nint_nfrac res;
    res.nint = vco / fref;
    res.frac = ((vco - (uint64_t)res.nint * fref) * ((uint64_t)1ul << 23)) / fref;
    return res;
}


enum vcos {
    VCO_OFF = 0,
    VCO4 = 4, //100
    VCO3 = 5, //101
    VCO2 = 6, //110
    VCO1 = 7, //111
};

struct vco_sel_lut {
    unsigned freqmax;
    uint16_t vconum;
    uint16_t vcodiv;
} static const s_vco_ranges[] = {
 {  285625000, VCO4, 3 },
 {  336875000, VCO3, 3 },
 {  405000000, VCO2, 3 },
 {  465000000, VCO1, 3 },
 {  571250000, VCO4, 2 },
 {  673750000, VCO3, 2 },
 {  810000000, VCO2, 2 },
 {  930000000, VCO1, 2 },
 { 1142500000, VCO4, 1 },
 { 1347500000, VCO3, 1 },
 { 1620000000, VCO2, 1 },
 { 1860000000, VCO1, 1 },
 { 2285000000, VCO4, 0 },
 { 2695000000, VCO3, 0 },
 { 3240000000, VCO2, 0 },
 { 3720000000, VCO1, 0 },
};

enum vtune_cmp {
    VTUNE_CMP_OK = 0,
    VTUNE_CMP_LOW = 2,
    VTUNE_CMP_HIGH = 1,
    VTUNE_CMP_FAIL = 3,
};


static
int lms6002d_get_comp(lms6002d_state_t* obj, bool tx)
{
    uint8_t val;
    int res = lms6002d_spi_rd(obj, tx ? TXPLL_VTUNE : RXPLL_VTUNE, &val);
    if (res)
        return res;

    return (GET_LMS6002D_TXPLL_VTUNE_VTUNE_H(val) << 1) |
            (GET_LMS6002D_TXPLL_VTUNE_VTUNE_L(val) << 0);
}

static
int lms6002d_find_vcocap(lms6002d_state_t* obj, bool tx, uint8_t* phi, uint8_t* plo)
{
    int res;
    int j = 4, i = 32, lo = 0, hi = -1;
    bool lsearch = true;

    // TODO last step error!!!
    for(;;) {
        if (lsearch && j < 0) {
            lsearch = false;
            lo = i;
            USDR_LOG("6002", USDR_LOG_INFO, "pll %s binary result: %d",
                     tx ? "tx" : "rx", i);
        }
        if (!lsearch && !(i < 64)) {
            if (hi == -1)
                hi = 0;
            break;
        }

        uint16_t regs[] = { tx ? MAKE_LMS6002D_TXPLL_PLL_CFG2(0, i) : MAKE_LMS6002D_RXPLL_PLL_CFG2(0, i) };
        res = lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
        if (res)
            return res;

        usleep(150);

        switch ((res = lms6002d_get_comp(obj, tx))) {
        case VTUNE_CMP_OK:
            if (!lsearch) {
                hi = i;
                break;
            }
        case VTUNE_CMP_HIGH:
            if (lsearch) {
                i -= (1 << j);
            } else /*if (hi == -1)*/ {
                hi = (i == 0) ? 0 : i - 1;
                goto found;
            }
            break;
        case VTUNE_CMP_LOW:
            if (lsearch) {
                i += (1 << j);
            } else {
                lo = i + 1;
            }
            break;
        case VTUNE_CMP_FAIL:
            return -EIO;
        default:
            return res;
        }

        if (lsearch)
            j--;
        else
            i++;
    }

found:
    *phi = hi;
    *plo = lo;
    return 0;
}


int lms6002d_tune_pll(lms6002d_state_t* obj, bool tx, unsigned freq)
{
    int res;
    unsigned k;
    if (freq < 200000000)
        return -ERANGE;

    for (k = 0; k < SIZEOF_ARRAY(s_vco_ranges) - 1; k++) {
        if (freq <= s_vco_ranges[k].freqmax)
            break;
    }

    uint64_t vcofreq = (uint64_t)freq << (s_vco_ranges[k].vcodiv + 1);
    struct nint_nfrac nn = lms6002d_pll_calc(obj->fref, vcofreq);
    unsigned vcon = s_vco_ranges[k].vconum;
    //for (vcon = 4; vcon < 8; vcon++)
    //{
    USDR_LOG("6002", USDR_LOG_INFO, "pll %s: OUT=%u VCO_FREQ=%llu VCO_NUM=%d NINT=%u NFRAC=%u FREF=%u\n",
             tx ? "tx" : "rx", freq, (unsigned long long)vcofreq, 8 - s_vco_ranges[k].vconum, nn.nint, nn.frac, obj->fref);

    if (tx) {
        SET_LMS6002D_TOP_ENREG_CLK_TX_DSM_SPI(obj->top_enreg, 1);
    } else {
        SET_LMS6002D_TOP_ENREG_CLK_RX_DSM_SPI(obj->top_enreg, 1);
    }

    uint32_t nint_nfrac = MAKE_LMS6002D_TXPLL_NINT_NFRAC_LONG(nn.nint, nn.frac);

    uint16_t regs[] = {
        MAKE_LMS6002D_REG_WR(TOP_ENREG, obj->top_enreg),
        tx ? MAKE_LMS6002D_TXPLL_NINT_NFRAC_BY0(nint_nfrac) :
             MAKE_LMS6002D_RXPLL_NINT_NFRAC_BY0(nint_nfrac),
        tx ? MAKE_LMS6002D_TXPLL_NINT_NFRAC_BY1(nint_nfrac) :
             MAKE_LMS6002D_RXPLL_NINT_NFRAC_BY1(nint_nfrac),
        tx ? MAKE_LMS6002D_TXPLL_NINT_NFRAC_BY2(nint_nfrac) :
             MAKE_LMS6002D_RXPLL_NINT_NFRAC_BY2(nint_nfrac),
        tx ? MAKE_LMS6002D_TXPLL_NINT_NFRAC_BY3(nint_nfrac) :
             MAKE_LMS6002D_RXPLL_NINT_NFRAC_BY3(nint_nfrac),
        tx ? MAKE_LMS6002D_TXPLL_PLL_CFG(1, 1, 1, 1, 0) :
             MAKE_LMS6002D_RXPLL_PLL_CFG(1, 1, 1, 1, 0),
        tx ? MAKE_LMS6002D_TXPLL_VCO_DIV(vcon, 0, 0) :
             MAKE_LMS6002D_RXPLL_VCO_DIV_BUFSEL(vcon, 0, 0),
        tx ? MAKE_LMS6002D_TXPLL_VCO_DIV(vcon, s_vco_ranges[k].vcodiv | 4, 0) :
             MAKE_LMS6002D_RXPLL_VCO_DIV_BUFSEL(vcon, s_vco_ranges[k].vcodiv | 4,
                                                GET_LMS6002D_RXPLL_VCO_DIV_BUFSEL_SELOUT(obj->rxpll_vco_div_bufsel)),
        tx ? MAKE_LMS6002D_TXPLL_PFD_UP(1, 0, 0, 6) :   //2
             MAKE_LMS6002D_RXPLL_PFD_UP(1, 0, 0, 6),    //2
        tx ? MAKE_LMS6002D_TXPLL_VCO_REG_PFD_U(1, 1, 0, 0) :
             MAKE_LMS6002D_RXPLL_VCO_REG_PFD_U(1, 1, 0, 0),
        tx ? MAKE_LMS6002D_TXPLL_VCO_REG_PFD_D(0, 2) :
             MAKE_LMS6002D_RXPLL_VCO_REG_PFD_D(0, 2),
        tx ? MAKE_LMS6002D_TXPLL_PLL_CFG2(0, 0x20) :
             MAKE_LMS6002D_RXPLL_PLL_CFG2(0, 0x20),
        //tx ? MAKE_LMS6002D_TXPLL_VCO_REG_PFD_D(2, 0) :
        //     MAKE_LMS6002D_RXPLL_VCO_REG_PFD_D(2, 0),
        tx ? 0x9b76 : 0xab76,
    };
    res = lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
    if (res)
        return res;

    // Toggle Changes for shadow register (2 cycles+ on SEN high)
    //   lowlevel_reg_wr32(obj->dev, obj->subdev, 0, (1<<29) | 1);
    //   lowlevel_spi_tr32(obj->dev, obj->subdev, obj->lsaddr, 0x0000, NULL);

    usleep(100);

    uint8_t wrreg[4] = {0};
    lms6002d_spi_rd(obj, tx ? TXPLL_NINT_NFRAC_BY0 : RXPLL_NINT_NFRAC_BY0, &wrreg[0]);
    lms6002d_spi_rd(obj, tx ? TXPLL_NINT_NFRAC_BY1 : RXPLL_NINT_NFRAC_BY1, &wrreg[1]);
    lms6002d_spi_rd(obj, tx ? TXPLL_NINT_NFRAC_BY2 : RXPLL_NINT_NFRAC_BY2, &wrreg[2]);
    lms6002d_spi_rd(obj, tx ? TXPLL_NINT_NFRAC_BY3 : RXPLL_NINT_NFRAC_BY3, &wrreg[3]);

    if (((regs[1] & 0xff) != wrreg[0]) ||
        ((regs[2] & 0xff) != wrreg[1]) ||
        ((regs[3] & 0xff) != wrreg[2]) ||
        ((regs[4] & 0xff) != wrreg[3])) {

        USDR_LOG("6002", USDR_LOG_ERROR, "Sanity check error! device malfunction!\n");
        return -EIO;
    }

    uint8_t lo, hi;
    res = lms6002d_find_vcocap(obj, tx, &hi, &lo);
    if (res)
        return res;

    // TODO add thermal info
    uint8_t vcocap = (lo + hi) / 2;
    uint16_t vregs[] = {
        tx ? MAKE_LMS6002D_TXPLL_PLL_CFG2(0, vcocap) :
             MAKE_LMS6002D_RXPLL_PLL_CFG2(0, vcocap),
        tx ? MAKE_LMS6002D_TXPLL_VCO_DIV(vcon, s_vco_ranges[k].vcodiv | 4, 0) :
             MAKE_LMS6002D_RXPLL_VCO_DIV_BUFSEL(vcon, s_vco_ranges[k].vcodiv | 4,
                                                GET_LMS6002D_RXPLL_VCO_DIV_BUFSEL_SELOUT(obj->rxpll_vco_div_bufsel)),
        tx ? 0x9b7e : 0xab7e, //PD comparator
 //       tx ? MAKE_LMS6002D_LMS6002D_TXPLL_0x17(1, 1, 0, 2) :
 //            MAKE_LMS6002D_LMS6002D_RXPLL_0x27(1, 1, 0, 2),
    };
    res = lms6002d_spi_post(obj, vregs, SIZEOF_ARRAY(vregs));
    if (res)
        return res;

    USDR_LOG("6002", USDR_LOG_INFO, "pll %s: vco_cap[%d;%d] => %d /%d / %02x -> %02x\n",
             tx ? "tx" : "rx", lo, hi, vcocap, vcon, obj->rxpll_vco_div_bufsel, vregs[1] );

    if (!tx)
        obj->rxpll_vco_div_bufsel = vregs[1];

    // TODO add more options for failed locks
    if (lo > hi && hi == 0) {
        return -ENOLCK;
    }
    return 0;
}

// TODO proper calibration
static struct lpf_band_lut {
    uint16_t freq_khz;
    uint8_t band;
    uint8_t rc;
} s_lpf_lut[] = {
    { 1030, 15, 7 },
    { 1190, 15, 6 },
    { 1333, 15, 5 },
    { 1500, 15, 4 },
    { 1650, 15, 3 },
    { 1700, 14, 4 },
    { 1870, 14, 3 },
    { 2250, 13, 5 }, //2210
    { 2750, 13, 4 }, //2500
    { 3000, 12, 4 }, //2750
    { 3250, 11, 4 }, //3000
    { 3650, 11, 3 }, //3300
    { 4170, 10, 4 }, //3840
    { 4650, 10, 3 }, //4220
    { 5200, 10, 2 }, //4740
    { 5500,  9, 4 }, //5000
    { 5750,  7, 5 }, //5310
    { 6000,  8, 4 }, //5500
    { 6500,  7, 4 }, //6000
    { 7250,  7, 3 }, //6590
    { 7550,  6, 4 }, //7000
    { 8400,  6, 3 }, //7690
    { 9400,  5, 4 }, //8750
    { 10400, 5, 3 }, //9620
    { 11650, 4, 4 }, //10000
    { 12400, 4, 3 }, //11000
    { 14400, 3, 4 }, //12000
    { 15200, 3, 3 }, //13200
    { 16650, 2, 4 }, //14000
    { 18900, 2, 3 }, //15400
    { 21150, 1, 5 }, //17700
    { 24000, 1, 4 }, //20000
    { 25650, 1, 3 }, //22000
    { 28650, 0, 5 }, //24800
    { 31900, 0, 4 }, //28000
    { 35400, 0, 3 }, //30800
    { 39150, 0, 2 }, //34600
    { 44650, 0, 1 }, //41200
    { 47000, 0, 0 },
};

int lms6002d_set_bandwidth(lms6002d_state_t* obj, bool tx, unsigned freq)
{
    int res;
    unsigned band = 0;
    unsigned b = 1;
    unsigned lpfcal = 0;

    for (unsigned i = 0; i < SIZEOF_ARRAY(s_lpf_lut); i++) {
        if ((freq / 1000) <= s_lpf_lut[i].freq_khz) {
            band = s_lpf_lut[i].band;
            lpfcal = s_lpf_lut[i].rc;
            b = 0;
            break;
        }
    }

    // 0x30 -- TX LPF
    // 0x50 -- RX LPF
    uint16_t regs[] = {
        (tx ? 0x3400 : 0x5400) | 0x8000 | (band << 2) | (b ? 0x000 : 0x0002),
        (tx ? 0x3500 : 0x5500) | 0x8000 | (b << 6) | 0x000C,
        (tx ? 0x3600 : 0x5600) | 0x8000 | (lpfcal << 4),
    };

    USDR_LOG("6002", USDR_LOG_INFO, "LPF %d => BAND=%d RC=%d BYPASS=%d CAL=%d\n",
             freq / 1000, band, lpfcal, b, obj->rclpfcal[band]);

    res = lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
    if (res)
        return res;
    return res;

}

int lms6002d_set_rxlna_gain(lms6002d_state_t* obj, unsigned lnag)
{
    SET_LMS6002D_RFE_GAIN_LNA_SEL_G_LNA(obj->rfe_gain_lna_sel, lnag);

    int res;
    uint16_t regs[] = {
        MAKE_LMS6002D_REG_WR(RFE_GAIN_LNA_SEL, obj->rfe_gain_lna_sel),
    };

    res = lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
    if (res)
        return res;
    return res;
}

int lms6002d_set_rxvga1_gain(lms6002d_state_t* obj, unsigned lnag)
{
    int res;
    uint16_t regs[] = {
        MAKE_LMS6002D_RFE_RFB_TIA(lnag),
    };

    // TODO RCAL adaptation
    res = lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
    if (res)
        return res;
    return res;
}

int lms6002d_set_txvga1_gain(lms6002d_state_t* obj, unsigned vga)
{
    int res;
    uint16_t regs[] = {
        MAKE_LMS6002D_TRF_VGA1GAIN(vga),
    };

    res = lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
    if (res)
        return res;
    return res;
}

int lms6002d_set_txvga2_gain(lms6002d_state_t* obj, unsigned vga)
{
    int res;
    uint16_t regs[] = {
        // FIXME: ENVD[2:0]: Controls envelop/peak detector analogue MUX
        MAKE_LMS6002D_TRF_CTRL2(vga, 0, 0),
    };

    res = lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
    if (res)
        return res;
    return res;
}


int lms6002d_set_rxvga2_gain(lms6002d_state_t* obj, unsigned vga)
{
    if (vga > 20)
        vga = 20;

    uint16_t regs[] = {
        MAKE_LMS6002D_RXVGA2_GAIN_COMB(vga),
    };

    return lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
}

int lms6002d_set_rxvga2ab_gain(lms6002d_state_t* obj, unsigned vga2a, unsigned vga2b)
{
    if (vga2a > 10)
        vga2a = 10;

    if (vga2b > 10)
        vga2b = 10;

    uint16_t regs[] = {
        MAKE_LMS6002D_RXVGA2_GAIN(vga2b, vga2a),
    };

    return lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
}

int lms6002d_set_rx_extterm(lms6002d_state_t* obj, bool extterm)
{
    uint16_t regs[] = {
        0x8000 | 0x7100 | (obj->rfe_in1sel_dci & 0x7f) | (extterm ? 0 : 0x80),
        0x8000 | 0x7c00 | (extterm ? 4 : 0), // RINEN_MIX_RXFE
    };

    //obj->rxfe_0x71 = regs[0];

    return lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
}

int lms6002d_set_rx_path(lms6002d_state_t* obj, unsigned path)
{
    SET_LMS6002D_RXPLL_VCO_DIV_BUFSEL_SELOUT(obj->rxpll_vco_div_bufsel, path);
    SET_LMS6002D_RFE_GAIN_LNA_SEL_LNASEL(obj->rfe_gain_lna_sel, path);

    uint16_t regs[] = {
        MAKE_LMS6002D_REG_WR(RXPLL_VCO_DIV_BUFSEL, obj->rxpll_vco_div_bufsel),
        MAKE_LMS6002D_REG_WR(RFE_GAIN_LNA_SEL, obj->rfe_gain_lna_sel),
    };

    USDR_LOG("6002", USDR_LOG_INFO, "Set PATH %d: %04x %04x\n", path,
             regs[0], regs[1]);

    return lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
}

int lms6002d_set_tx_path(lms6002d_state_t* obj, unsigned path)
{
    uint16_t regs[] = {
        MAKE_LMS6002D_TRF_PA_CTRL(path, path == 3 ? 1 : 0),
    };
    return lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
}

static
int lms6002d_calibration_loop(lms6002d_state_t* obj, uint16_t reg, uint8_t addr, uint8_t *pureg)
{
    uint8_t ureg, ulck;
    int res;
    bool fail = true;
    // Do calibration
    uint16_t regs_c0[] = {
        (reg << 8) | MAKE_LMS6002D_TOP_DC_REG(31),
        (reg << 8) | MAKE_LMS6002D_TOP_DC_OP(0, 0, 1, addr),
        (reg << 8) | MAKE_LMS6002D_TOP_DC_OP(1, 0, 1, addr),
        (reg << 8) | MAKE_LMS6002D_TOP_DC_OP(0, 0, 1, addr),
    };

    res = lms6002d_spi_post(obj, regs_c0, SIZEOF_ARRAY(regs_c0));
    if (res)
        return res;

    for (unsigned t = 0; t < 100; t++) {
        usleep(7);

        // read clbr_done
        res = lms6002d_spi_rd(obj, (reg) | TOP_DC_CAL, &ulck);
        if (res)
            return res;

        // read dc_lock
        if (GET_LMS6002D_TOP_DC_CAL_CLBR_DONE(ulck))
            continue;

        res = lms6002d_spi_rd(obj, (reg) | TOP_DC_REG, &ureg);
        if (res)
            return res;
        ureg &= 0x1f;

        if (ureg == 0x1f) {
            uint16_t p[] = { (reg << 8) | MAKE_LMS6002D_TOP_DC_REG(0),
                             (reg << 8) | MAKE_LMS6002D_TOP_DC_OP(0, 0, 1, addr),
                             (reg << 8) | MAKE_LMS6002D_TOP_DC_OP(1, 0, 1, addr),
                             (reg << 8) | MAKE_LMS6002D_TOP_DC_OP(0, 0, 1, addr),
                           };

            res = lms6002d_spi_post(obj, p, SIZEOF_ARRAY(p));
            if (res)
                return res;

            continue;
        }

        fail = false;
        break;
    }
    USDR_LOG("6002", USDR_LOG_INFO, "calibration[%x:%d] done: %d; %d\n",
             reg, addr, fail, ureg);

    *pureg = ureg;
    return fail ? -EFAULT : 0;
}


int lms6002d_trf_enable(lms6002d_state_t* obj, bool en)
{
    SET_LMS6002D_TOP_ENREG_CLK_TX_DSM_SPI(obj->top_enreg, en);
    SET_LMS6002D_TOP_ENCFG_STXEN(obj->top_encfg, en);

    uint16_t regs[] = {
        MAKE_LMS6002D_REG_WR(TOP_ENREG, obj->top_enreg),
        MAKE_LMS6002D_REG_WR(TOP_ENCFG, obj->top_encfg),
    };
    return lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
}

int lms6002d_rfe_enable(lms6002d_state_t* obj, bool en)
{
    SET_LMS6002D_TOP_ENREG_CLK_RX_DSM_SPI(obj->top_enreg, en);
    SET_LMS6002D_TOP_ENCFG_SRXEN(obj->top_encfg, en);

    uint16_t regs[] = {
        MAKE_LMS6002D_REG_WR(TOP_ENREG, obj->top_enreg),
        MAKE_LMS6002D_REG_WR(TOP_ENCFG, obj->top_encfg),
    };
    return lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
}

int lms6002d_rxvga2_enable(lms6002d_state_t* obj, bool en)
{
    uint16_t regs[] = {
        MAKE_LMS6002D_RXVGA2_CTRL(7, en ? 1 : 0, 1),
    };
    return lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
}

int lms6002d_cal_lpf(lms6002d_state_t* obj)
{
    int res;
    uint8_t ureg;

    SET_LMS6002D_TOP_ENREG_CLK_LPF_CAL(obj->top_enreg, 1);

    uint16_t regs_0[] = {
        MAKE_LMS6002D_AFE_MISC(0, 1, 1, 1, 1, 1),
        MAKE_LMS6002D_REG_WR(TOP_ENREG, obj->top_enreg),
    };
    res = lms6002d_spi_post(obj, regs_0, SIZEOF_ARRAY(regs_0));
    if (res)
        return res;

    res = lms6002d_calibration_loop(obj, 0, 0, &ureg);
    if (res == 0) {
        uint16_t regs_c1[] = {
            MAKE_LMS6002D_RXLPF_DACBP(0, ureg),
            MAKE_LMS6002D_TXLPF_DACBP(0, ureg),
        };
        res = lms6002d_spi_post(obj, regs_c1, SIZEOF_ARRAY(regs_c1));
        if (res)
            return res;

    }

    SET_LMS6002D_TOP_ENREG_CLK_LPF_CAL(obj->top_enreg, 0);

    regs_0[0] = MAKE_LMS6002D_AFE_MISC(1, 1, 1, 1, 1, 1); //Power down DC offset comparators
    regs_0[1] = MAKE_LMS6002D_REG_WR(TOP_ENREG, obj->top_enreg);
    res = lms6002d_spi_post(obj, regs_0, SIZEOF_ARRAY(regs_0));
    if (res)
        return res;

    return 0;
}


int lms6002d_cal_txrxlpfdc(lms6002d_state_t* obj, bool tx)
{
    int res;
    uint16_t breg = tx ? 0x30 : 0x50;
    uint8_t ureg_i, ureg_q;

    SET_LMS6002D_TOP_ENREG_CLK_RX_RPL_DCCAL(obj->top_enreg, !tx);
    SET_LMS6002D_TOP_ENREG_CLK_TX_RPL_DCCAL(obj->top_enreg, tx);

    uint16_t regs_0[] = {
        MAKE_LMS6002D_REG_WR(TOP_ENREG, obj->top_enreg),
    };

    res = lms6002d_spi_post(obj, regs_0, SIZEOF_ARRAY(regs_0));
    if (res)
        return res;

    res = lms6002d_calibration_loop(obj, breg, 0, &ureg_i);
    if (res)
        goto fail_i;

    res = lms6002d_calibration_loop(obj, breg, 1, &ureg_q);
    if (res)
        goto fail_q;

fail_i:
fail_q:
    SET_LMS6002D_TOP_ENREG_CLK_LPF_CAL(obj->top_enreg, 0);
    SET_LMS6002D_TOP_ENREG_CLK_RX_RPL_DCCAL(obj->top_enreg, 0);
    SET_LMS6002D_TOP_ENREG_CLK_TX_RPL_DCCAL(obj->top_enreg, 0);

    regs_0[0] = MAKE_LMS6002D_REG_WR(TOP_ENREG, obj->top_enreg);
    res = lms6002d_spi_post(obj, regs_0, SIZEOF_ARRAY(regs_0));
    if (res)
        return res;

    return 0;
}

int lms6002d_cal_vga2(lms6002d_state_t* obj)
{
    int res;
    uint8_t ureg_dcref = ~0, ureg_2ai = ~0, ureg_2aq = ~0, ureg_2bi = ~0, ureg_2bq = ~0;

    SET_LMS6002D_TOP_ENREG_CLK_RX_VGA2_DCCAL(obj->top_enreg, 1);

    uint16_t regs_0[] = {
        MAKE_LMS6002D_RXVGA2_PD_CALIB(0, 0),
        MAKE_LMS6002D_REG_WR(TOP_ENREG, obj->top_enreg),
        // MAKE_LMS6002D_RXVGA2_CTRL(7, 1, 1),
        MAKE_LMS6002D_RXVGA2_GAIN(0, 10),
    };

    uint16_t regs_1[] = {
        MAKE_LMS6002D_RXVGA2_GAIN(10, 0),
    };

    res = lms6002d_spi_post(obj, regs_0, SIZEOF_ARRAY(regs_0));
    if (res)
        goto fail_cal;

    res = lms6002d_calibration_loop(obj, 0x60, 0, &ureg_dcref);
    if (res)
        goto fail_cal;

    res = lms6002d_calibration_loop(obj, 0x60, 1, &ureg_2ai);
    if (res)
        goto fail_cal;

    res = lms6002d_calibration_loop(obj, 0x60, 2, &ureg_2aq);
    if (res)
        goto fail_cal;

    res = lms6002d_spi_post(obj, regs_1, SIZEOF_ARRAY(regs_1));
    if (res)
        goto fail_cal;

    res = lms6002d_calibration_loop(obj, 0x60, 3, &ureg_2bi);
    if (res)
        goto fail_cal;

    res = lms6002d_calibration_loop(obj, 0x60, 4, &ureg_2bq);
    if (res)
        goto fail_cal;

fail_cal:
    SET_LMS6002D_TOP_ENREG_CLK_RX_VGA2_DCCAL(obj->top_enreg, 0);
    regs_0[0] = MAKE_LMS6002D_RXVGA2_PD_CALIB(1, 1); // Turn off VGA2 comparators
    regs_0[1] = MAKE_LMS6002D_REG_WR(TOP_ENREG, obj->top_enreg);
    res = lms6002d_spi_post(obj, regs_0, SIZEOF_ARRAY(regs_0));
    if (res)
        return res;

    USDR_LOG("6002", USDR_LOG_INFO, "VGA2 calibration done [%d, %d, %d, %d, %d]\n",
             ureg_dcref, ureg_2ai, ureg_2aq, ureg_2bi, ureg_2bq);
    return 0;
}

int lms6002d_cal_lpf_bandwidth(lms6002d_state_t* obj, unsigned bcode)
{
    // TURN ON tx, SET tx to 320Mhz
    int res = 0;
    uint8_t rcal = ~0;
    if (bcode >= LPF_BANDS)
        return -EINVAL;

    bool txen = GET_LMS6002D_TOP_ENCFG_STXEN(obj->top_encfg);

    res = res ? res : lms6002d_trf_enable(obj, 1);
    res = res ? res : lms6002d_tune_pll(obj, true, 320000000);

    uint16_t regs_0[] = {
        MAKE_LMS6002D_TOP_LPF_CTRL(0, 0, 0, 0),
        MAKE_LMS6002D_TOP_LPF_CAL(0, 0, bcode),
        MAKE_LMS6002D_TOP_LPF_CAL(1, 0, bcode),
        MAKE_LMS6002D_TOP_LPF_CTRL(0, 0, 0, 1),
        MAKE_LMS6002D_TOP_LPF_CTRL(0, 0, 0, 0),
    };
    res = res ? res : lms6002d_spi_post(obj, regs_0, SIZEOF_ARRAY(regs_0));

    usleep(10);

    // read clbr_done
    res = res ? res : lms6002d_spi_rd(obj, TOP_DC_CAL, &rcal);

    USDR_LOG("6002", USDR_LOG_INFO, "LPFBW code=%d cal=%02x val=%d\n",
             bcode, rcal, GET_LMS6002D_TOP_DC_CAL_RC_LPF(rcal));

    // Update calibration code
    obj->rclpfcal[bcode] = GET_LMS6002D_TOP_DC_CAL_RC_LPF(rcal) | 0x80;

    // Store calibration value
    if (!txen) {
        SET_LMS6002D_TOP_ENCFG_STXEN(obj->top_encfg, 0);
    }
    uint16_t regs_1[] = {
        MAKE_LMS6002D_TOP_LPF_CTRL(0, 1, 0, 0),
        MAKE_LMS6002D_TOP_LPF_CAL(0, 0, bcode),
        MAKE_LMS6002D_REG_WR(TOP_ENCFG, obj->top_encfg),
    };
    res = res ? res : lms6002d_spi_post(obj, regs_1, SIZEOF_ARRAY(regs_1));
    return res;
}

int lms6002d_set_rxfedc(lms6002d_state_t* obj, int8_t dci, int8_t dcq)
{
    uint16_t regs[] = {
        MAKE_LMS6002D_RFE_IN1SEL_DCI(1, dci),
        MAKE_LMS6002D_RFE_INLOAD_DCQ(1, dcq),
    };
    return lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
}

int lms6002d_set_tia_cfb(lms6002d_state_t* obj, uint8_t value)
{
    uint16_t regs[] = {
        MAKE_LMS6002D_RFE_CFB_TIA(value),
    };
    return lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
}

int lms6002d_set_tia_rfb(lms6002d_state_t* obj, uint8_t value)
{
    uint16_t regs[] = {
        MAKE_LMS6002D_RFE_RFB_TIA(value),
    };
    return lms6002d_spi_post(obj, regs, SIZEOF_ARRAY(regs));
}
