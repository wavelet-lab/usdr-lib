// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "lms7002m.h"
#include "def_lms7002m.h"

#include <usdr_logging.h>

#include <string.h>


#define CLAMP(x, mi, ma) ((x) < (mi) ? (mi) : (x) > (ma) ? (ma) : (x))

#define STATIC_ASSERT(COND, MSG) typedef char static_assertion_##MSG[(COND)?1:-1]

// Sanity checks
// STATIC_ASSERT(RFE_LNAH == 0, RFE_LNAH_RIGHT);

enum sxx_vco_params {
    SXX_VCOL_MIN = 1900000000U,
    SXX_VCOL_MAX = 2611000000U,

    SXX_VCOM_MIN = 2481000000U,
    SXX_VCOM_MAX = 3377000000U,

    SXX_VCOH_MIN = 3153000000U,
    SXX_VCOH_MAX = 3999000000U,
};

enum cgen_vco_params {
    CGEN_VCO_MIN = 2000000000U,
    CGEN_VCO_MAX = 2700000000U,
    CGEN_VCO_MID = CGEN_VCO_MIN / 2 + CGEN_VCO_MAX / 2,

    CGEN_OUT_DIV_MIN = 2,
    CGEN_OUT_DIV_MAX = 512,
};

typedef int (*lms7002m_trim_vco_func_t)(lms7002m_state_t*, int);

struct pll_cfg {
    unsigned nint;
    unsigned frac;
};


static int lms7002m_spi_post(lms7002m_state_t* obj, uint32_t* regs, unsigned count)
{
    int res;
    for (unsigned i = 0; i < count; i++) {
        res = lowlevel_spi_tr32(obj->dev, obj->subdev, obj->lsaddr, regs[i], NULL);
        if (res)
            return res;

        USDR_LOG("7002", USDR_LOG_NOTE, "%d/%d reg wr [mac:%d] %08x\n", i, count,
                 GET_LMS7002M_LML_0X0020_MAC(obj->reg_amac),
                 regs[i]);

        if (((regs[i] >> 16) & 0x7fff) == LML_0x0020) {
            obj->reg_amac = regs[i];
        }
    }

    return 0;
}

static int lms7002m_spi_rd(lms7002m_state_t* obj, uint16_t addr, uint16_t* data)
{
    uint32_t rd;
    int res = lowlevel_spi_tr32(obj->dev, obj->subdev, obj->lsaddr, ((unsigned)addr << 16), &rd);
    if (res)
        return res;

    USDR_LOG("7002", USDR_LOG_NOTE, "reg rd %04x => %04x\n", addr, rd & 0xffff);
    *data = (uint16_t)rd;
    return 0;
}

int lms7002m_create(lldev_t dev, unsigned subdev, unsigned lsaddr,
                    uint32_t lms_ldo_mask,
                    bool txrx_clk,
                    lms7002m_state_t* out)
{
    int res;
    uint16_t ver;
    out->dev = dev;
    out->subdev = subdev;
    out->lsaddr = lsaddr;

    uint32_t reset_regs[] = {
        MAKE_LMS7002M_LML_0x0020(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, LMS7_CH_AB),
        MAKE_LMS7002M_LML_0x0020(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, LMS7_CH_AB),

        MAKE_LMS7002M_LML_0x002E(0), //Enable MIMO, this bit only hardwires MAC[1]
        MAKE_LMS7002M_REG_WR(LDO_0x0092, lms_ldo_mask >> 0),
        MAKE_LMS7002M_REG_WR(LDO_0x0093, lms_ldo_mask >> 16),
        MAKE_LMS7002M_LDO_0x0095( 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 ),
        MAKE_LMS7002M_LDO_0x0096( 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 ),
        MAKE_LMS7002M_LDO_0x00A6( 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1),
//        MAKE_LMS7002M_XBUF_0x0085( 1, 1, 0, 0, 1, 1, 0, 0, 1 ),
        MAKE_LMS7002M_XBUF_0x0085( 1, 1, 0, 0, txrx_clk ? 1 : 0, txrx_clk ? 1 : 0, 0, 0, 1 ),

        // Disable RF
        MAKE_LMS7002M_AFE_0x0082(0, AFE_0X0082_MODE_INTERLEAVE_AFE_2ADCS, AFE_0X0082_MUX_AFE_1_MUXOFF, AFE_0X0082_MUX_AFE_2_MUXOFF,
                                 1, 1, 1, 1, 1, 0),
    };

    res = lms7002m_spi_post(out, reset_regs, SIZEOF_ARRAY(reset_regs));
    if (res)
        return res;

    res = lms7002m_spi_rd(out, LML_0x002F, &ver);
    if (res)
        return res;

    USDR_LOG("7002", USDR_LOG_INFO, "LMS7002M VER:%d REV:%d MASK:%d",
             GET_LMS7002M_LML_0X002F_VER(ver),
             GET_LMS7002M_LML_0X002F_REV(ver),
             GET_LMS7002M_LML_0X002F_MASK(ver));

    if (GET_LMS7002M_LML_0X002F_VER(ver) != 7)
        return -ENODEV;
    if (GET_LMS7002M_LML_0X002F_REV(ver) != 1)
        return -ENODEV;


    out->reg_mac = reset_regs[1];

    memset(out->rfe, 0, sizeof(out->rfe));
    memset(out->rbb, 0, sizeof(out->rbb));
    //memset(out->tbb, 0, sizeof(out->tbb));
    memset(out->trf, 0, sizeof(out->trf));

    out->reg_en_dir[0] = out->reg_en_dir[1] = 0;
    out->reg_rxtsp_dscpcfg[0] = out->reg_rxtsp_dscpcfg[1] = 0;
    out->reg_rxtsp_dscmode[0] = out->reg_rxtsp_dscmode[1] = 0;
    out->reg_rxtsp_hbdo_iq[0] = out->reg_rxtsp_hbdo_iq[1] = 0;
    out->reg_txtsp_dscpcfg[0] = out->reg_txtsp_dscpcfg[1] = 0;
    out->reg_txtsp_dscmode[0] = out->reg_txtsp_dscmode[1] = 0;
    out->reg_txtsp_hbdo_iq[0] = out->reg_txtsp_hbdo_iq[1] = 0;

    out->rfe[0].tia = out->rfe[1].tia = 1;
    out->rfe[0].lna = out->rfe[1].lna = 12;

    out->reg_tbb_gc_corr[0] = 0;
    out->reg_tbb_gc_corr[1] = 0;
    out->reg_tbb_gc[0] = 12;
    out->reg_tbb_gc[1] = 12;
    return 0;
}

int lms7002m_destroy(lms7002m_state_t *m)
{
    m->reg_en_dir[0] = 0;
    m->reg_en_dir[1] = 0;

    uint32_t regs[2 * 2 + 1], j = 0;
    for (unsigned i = 0; i < 2; i++) {
        uint16_t mac = m->reg_mac;
        SET_LMS7002M_LML_0X0020_MAC(mac, i + 1);

        regs[j++] = MAKE_LMS7002M_REG_WR(LML_0x0020, mac);
        regs[j++] = MAKE_LMS7002M_REG_WR(SXX_0x0124, m->reg_en_dir[i]);
    };
    regs[j++] = MAKE_LMS7002M_AFE_0x0082(4,
                                         AFE_0X0082_MODE_INTERLEAVE_AFE_2ADCS,
                                         AFE_0X0082_MUX_AFE_1_MUXOFF,
                                         AFE_0X0082_MUX_AFE_2_MUXOFF,
                                         1,
                                         1,
                                         1,
                                         1,
                                         1,
                                         0);

    return lms7002m_spi_post(m, regs, j);
}


static struct pll_cfg _pll_calc(unsigned fref, unsigned vco)
{
    struct pll_cfg res;
    res.nint = vco / fref;
    res.frac = (vco - res.nint * fref) * ((uint64_t)1 << 20) / fref;
    return res;
}


int lms7002m_cgen_trim_vco(lms7002m_state_t* m, int vco_cap)
{
    uint16_t reg;
    uint32_t cgen_regs[] = { MAKE_LMS7002M_CGEN_0x008B(15, (unsigned)vco_cap, 0) };
    int res = lms7002m_spi_post(m, cgen_regs, SIZEOF_ARRAY(cgen_regs));
    if (res)
        return res;

    res = lms7002m_spi_rd(m, CGEN_0x008C, &reg);
    if (res)
        return res;

    return (int)((GET_LMS7002M_CGEN_0X008C_VCO_CMPHO(reg) << 1) |
                  GET_LMS7002M_CGEN_0X008C_VCO_CMPLO(reg));
}

int lms7002m_sxx_trim_vco(lms7002m_state_t* m, int vco_cap)
{
    uint16_t reg;
    uint32_t cgen_regs[] = { MAKE_LMS7002M_SXX_0x0121(16, (unsigned)vco_cap, m->temp, 0) };
    int res = lms7002m_spi_post(m, cgen_regs, SIZEOF_ARRAY(cgen_regs));
    if (res)
        return res;

    res = lms7002m_spi_rd(m, SXX_0x0123, &reg);
    if (res)
        return res;

    return (int)((GET_LMS7002M_SXX_0X0123_VCO_CMPHO(reg) << 1) |
                  GET_LMS7002M_SXX_0X0123_VCO_CMPLO(reg));
}


static int _lms7002m_vco_range(lms7002m_state_t* m, lms7002m_trim_vco_func_t f,
                               unsigned start, uint8_t* phi, uint8_t* plo,
                               const char *name)
{
    int i;
    int lo = 0, hi = -1;
    int res;

    // Using binary search to find lowest range
    if (start > 255) {
        i = 128;
        for (int j = 6; j >= 0; j--) {
            switch ((res = f(m, i))) {
            case LMS7002M_VCO_OK:
            case LMS7002M_VCO_HIGH:
                i -= (1 << j);
                break;
            case LMS7002M_VCO_LOW:
                i += (1 << j);
                break;
            case LMS7002M_VCO_FAIL:
                return -EIO;
            default:
                return res;
            }
        }

        // Backup by one just to be sure we don't miss it
        lo = i;
        i = i > 1 ? i - 1 : 0;
    } else {
        i = (int)start;
    }

    unsigned log_s = i;
    unsigned log_b = lo;

    for (; i < 256; i++) {
        switch ((res = f(m, i))) {
        case LMS7002M_VCO_OK:
            hi = i;
            if (lo > i)
                lo = i;
            break;
        case LMS7002M_VCO_HIGH:
            if (hi == -1) {
                hi = (i == 0) ? 0 : i - 1;
            }
            goto find_high;
        case LMS7002M_VCO_LOW:
            lo = i + 1;
            break;
        case LMS7002M_VCO_FAIL:
            return -EIO;
        default:
            return res;
        }
    }

find_high:
    if (hi == -1)
        hi = 0;

    USDR_LOG("7002", USDR_LOG_INFO, "%s binary result: %d; Probed range [%d .. %d] => Good range [%d; %d]",
             name, log_b, log_s, i, lo, hi);

    if (lo > 255) {
        lo = 255;
    }

    *phi = (uint8_t)hi;
    *plo = (uint8_t)lo;
    return 0;
}


int lms7002m_mac_set(lms7002m_state_t* m, unsigned mac)
{
    if (GET_LMS7002M_LML_0X0020_MAC(m->reg_mac) == mac) {
        return 0;
    }

    SET_LMS7002M_LML_0X0020_MAC(m->reg_mac, mac);
    uint32_t regs[] = {
        MAKE_LMS7002M_REG_WR(LML_0x0020, m->reg_mac),
    };
    return lms7002m_spi_post(m, regs, SIZEOF_ARRAY(regs));
}

static bool _lms7002m_check_chan(lms7002m_state_t* m, unsigned idx)
{
    return (GET_LMS7002M_LML_0X0020_MAC(m->reg_mac) & (idx + 1)) ? true : false;
}

static bool _lms7002m_is_none(lms7002m_state_t* m)
{
    return (GET_LMS7002M_LML_0X0020_MAC(m->reg_mac) == LMS7_CH_NONE);
}

int lms7002m_limelight_reset(lms7002m_state_t* m)
{
    uint16_t reg_mac_rst = m->reg_mac;
    SET_LMS7002M_LML_0X0020_LRST_TX_B(reg_mac_rst, 1);
    SET_LMS7002M_LML_0X0020_MRST_TX_B(reg_mac_rst, 1);
    SET_LMS7002M_LML_0X0020_LRST_TX_A(reg_mac_rst, 1);
    SET_LMS7002M_LML_0X0020_MRST_TX_A(reg_mac_rst, 1);
    SET_LMS7002M_LML_0X0020_LRST_RX_B(reg_mac_rst, 1);
    SET_LMS7002M_LML_0X0020_MRST_RX_B(reg_mac_rst, 1);
    SET_LMS7002M_LML_0X0020_LRST_RX_A(reg_mac_rst, 1);
    SET_LMS7002M_LML_0X0020_MRST_RX_A(reg_mac_rst, 1);
    SET_LMS7002M_LML_0X0020_SRST_RXFIFO(reg_mac_rst, 1);
    SET_LMS7002M_LML_0X0020_SRST_TXFIFO(reg_mac_rst, 1);

    uint32_t regs[] = {
        // Reset LML FIFO
        MAKE_LMS7002M_REG_WR(LML_0x0020, reg_mac_rst),
        MAKE_LMS7002M_REG_WR(LML_0x0020, m->reg_mac),
    };

    return lms7002m_spi_post(m, regs, SIZEOF_ARRAY(regs));
}

int lms7002m_limelight_fifo_reset(lms7002m_state_t* m, bool rx, bool tx)
{
    uint16_t reg_mac_rst = m->reg_mac;
    if (rx)
        SET_LMS7002M_LML_0X0020_SRST_RXFIFO(reg_mac_rst, 1);
    if (tx)
        SET_LMS7002M_LML_0X0020_SRST_TXFIFO(reg_mac_rst, 1);

    uint32_t regs[] = {
        // Reset LML FIFO
        MAKE_LMS7002M_REG_WR(LML_0x0020, reg_mac_rst),
        MAKE_LMS7002M_REG_WR(LML_0x0020, m->reg_mac),
    };

    return lms7002m_spi_post(m, regs, SIZEOF_ARRAY(regs));
}

int lms7002m_limelight_l_reset(lms7002m_state_t* m, bool rx, bool tx)
{
    uint16_t reg_mac_rst = m->reg_mac;
    if (rx) {
        SET_LMS7002M_LML_0X0020_LRST_RX_B(reg_mac_rst, 1);
        SET_LMS7002M_LML_0X0020_LRST_RX_A(reg_mac_rst, 1);
    }
    if (tx) {
        SET_LMS7002M_LML_0X0020_LRST_TX_B(reg_mac_rst, 1);
        SET_LMS7002M_LML_0X0020_LRST_TX_A(reg_mac_rst, 1);
    }

    uint32_t regs[] = {
        // Reset LML FIFO
        MAKE_LMS7002M_REG_WR(LML_0x0020, reg_mac_rst),
        //MAKE_LMS7002M_REG_WR(LML_0x0020, m->reg_mac),
    };

     lms7002m_spi_post(m, regs, SIZEOF_ARRAY(regs));


     usleep(1000);

    uint32_t regs2[] = {
        // Reset LML FIFO
        //MAKE_LMS7002M_REG_WR(LML_0x0020, reg_mac_rst),
        MAKE_LMS7002M_REG_WR(LML_0x0020, m->reg_mac),
    };


    return lms7002m_spi_post(m, regs2, SIZEOF_ARRAY(regs2));

}

int lms7002m_limelight_toggle_ntx(lms7002m_state_t* m)
{
    uint32_t regs[] = {
        MAKE_LMS7002M_CDS_0x00AD(0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1),
        MAKE_LMS7002M_CDS_0x00AD(0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1),
    };

    return lms7002m_spi_post(m, regs, SIZEOF_ARRAY(regs));
}

int lms7002m_limelight_configure(lms7002m_state_t* m, lms7002m_limelight_conf_t params)
{
    unsigned txmclk = (params.txdiv <= 1) ? LML_0X002B_MCLK1SRC_TXTSPCLKA : LML_0X002B_MCLK1SRC_TXTSPCLKA_DIV;
    unsigned rxmclk = (params.rxdiv <= 1) ? LML_0X002B_MCLK1SRC_RXTSPCLKA : LML_0X002B_MCLK1SRC_RXTSPCLKA_DIV;

    unsigned rxmux = params.rx_lfsr ? LML_0X002A_RX_MUX_LFSR :
                         params.rx_tx_dig_loopback ? LML_0X002A_RX_MUX_TXFIFO : LML_0X002A_RX_MUX_RXTSP;

    unsigned rdclk = (params.rx_ext_rd_fclk /* || params.rx_tx_dig_loopback */ ) ?
                         ((params.rx_port) ? LML_0X002A_RXRDCLK_MUX_FCLK1 : LML_0X002A_RXRDCLK_MUX_FCLK2) :
                         ((params.rx_port) ? LML_0X002A_RXRDCLK_MUX_MCLK1 : LML_0X002A_RXRDCLK_MUX_MCLK2);
    uint16_t reg_mac = m->reg_mac;
    SET_LMS7002M_LML_0X0020_SRST_RXFIFO(reg_mac, 1);
    SET_LMS7002M_LML_0X0020_SRST_TXFIFO(reg_mac, 1);

    uint32_t regs[] = {
        MAKE_LMS7002M_LML_0x0022(0,
                                 params.rx_port ? params.txsisoddr : params.rxsisoddr,
                                 0,
                                 params.rx_port ? params.rxsisoddr : params.txsisoddr,
                                 params.ds_high, //DIQ2_DS,
                                 0, //DIQ2_PE,
                                 0, //IQ_SET_EN_2_PE,
                                 0, //TXNRX2_PE,
                                 0, //FCLK2_PE,
                                 0, //MCLK2_PE,
                                 params.ds_high, //DIQ1_DS,
                                 0, //DIQ1_PE,
                                 0, //IQ_SET_EN_1_PE,
                                 0, //TXNRX1_PE,
                                 0, //FCLK1_PE,
                                 0),
        MAKE_LMS7002M_LML_0x0023(0, LML_0X0023_DIQDIR2_INPUT, 0, LML_0X0023_ENABLEDIR2_INPUT,
                                 0, LML_0X0023_DIQDIR1_INPUT, 0, LML_0X0023_ENABLEDIR1_INPUT,
                                 1, //Enable LML
                                 0,
                                 !params.rx_port,
                                 LML_0X0023_LML2_MODE_TRXIQ,
                                 0,
                                 params.rx_port,
                                 LML_0X0023_LML1_MODE_TRXIQ ),
        MAKE_LMS7002M_LML_0x002A(rxmux,
                                 params.rx_port ? LML_0X002A_TX_MUX_PORT2 : LML_0X002A_TX_MUX_PORT1,
                                 LML_0X002A_TXRDCLK_MUX_TXTSPCLK,
                                 params.rx_port ? LML_0X002A_TXWRCLK_MUX_FCLK2 : LML_0X002A_TXWRCLK_MUX_FCLK1,
                                 rdclk,
                                 LML_0X002A_RXWRCLK_MUX_RXTSPCLK ),
        MAKE_LMS7002M_LML_0x002B(0, 1, 0, 0,
                                 params.rx_port ? txmclk : rxmclk,
                                 params.rx_port ? rxmclk : txmclk,
                                 (params.txdiv > 1) ? 1u : 0,
                                 (params.rxdiv > 1) ? 1u : 0),
        MAKE_LMS7002M_LML_0x002C( params.txdiv / 2u - 1u, params.rxdiv / 2u - 1u ),
        MAKE_LMS7002M_CDS_0x00AD(0, 0, 0, 0, 0, 1, 1, 1, 1, 1, params.rxsisoddr && params.rxdiv == 1 ? 0 : 1, 1, 1),
        MAKE_LMS7002M_CDS_0x00AE(3, 3, 0, 0, 0, 0, 0, 0),
        MAKE_LMS7002M_REG_WR(LML_0x0020, reg_mac),
        MAKE_LMS7002M_REG_WR(LML_0x0020, m->reg_mac)
    };

    return lms7002m_spi_post(m, regs, SIZEOF_ARRAY(regs));
}



int _lms7002m_fill_pos(lms7002m_lml_map_t l, lms7002m_lml_map_t* o)
{
    lms7002m_lml_map_t p = {{0, 0, 0, 0}};
    for (unsigned i = 0; i < 4; i++) {
        switch (l.m[i]) {
        case LML_0X0024_LML1_S0S_AI: p.m[LML_AI] = i; break;
        case LML_0X0024_LML1_S0S_AQ: p.m[LML_AQ] = i; break;
        case LML_0X0024_LML1_S0S_BI: p.m[LML_BI] = i; break;
        case LML_0X0024_LML1_S0S_BQ: p.m[LML_BQ] = i; break;
        default:
            return -EINVAL;
        }
    }

    *o = p;
    return 0;
}


int lms7002m_limelight_map(lms7002m_state_t* m, lms7002m_lml_map_t l1m, lms7002m_lml_map_t l2m)
{
    lms7002m_lml_map_t l1p, l2p;
    int res = 0;
    res = res ? res : _lms7002m_fill_pos(l1m, &l1p);
    res = res ? res : _lms7002m_fill_pos(l2m, &l2p);
    if (res)
        return res;

    uint32_t lml_regs[] = {
        MAKE_LMS7002M_LML_0x0024(l1m.m[3], l1m.m[2], l1m.m[1], l1m.m[0],
                                 l1p.m[LML_BQ], l1p.m[LML_BI], l1p.m[LML_AQ], l1p.m[LML_AI]),
        MAKE_LMS7002M_LML_0x0027(l2m.m[3], l2m.m[2], l2m.m[1], l2m.m[0],
                                 l2p.m[LML_BQ], l2p.m[LML_BI], l2p.m[LML_AQ], l2p.m[LML_AI]),
    };

    return lms7002m_spi_post(m, lml_regs, SIZEOF_ARRAY(lml_regs));
}


int lms7002m_cgen_disable(lms7002m_state_t* m)
{
    uint32_t reg_cgen_en = MAKE_LMS7002M_CGEN_ENABLE(0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0);
    return lms7002m_spi_post(m, &reg_cgen_en, 1);
}


int lms7002m_cgen_tune(lms7002m_state_t* m, unsigned fref, unsigned outfreq, unsigned txdiv_ord)
{
    // VCO div selection
    unsigned div2 = (CGEN_VCO_MID / outfreq + 1) / 2;
    if (div2 < CGEN_OUT_DIV_MIN / 2)
        div2 = CGEN_OUT_DIV_MIN / 2;
    else if (div2 > CGEN_OUT_DIV_MAX / 2)
        div2 = CGEN_OUT_DIV_MAX / 2;

    unsigned vco = div2 * outfreq * 2;
    struct pll_cfg vc = _pll_calc(fref, vco);

    if ((vco < CGEN_VCO_MIN) || (vco > CGEN_VCO_MAX)) {
        // Out of range
        USDR_LOG("7002", USDR_LOG_WARNING, "CGEN: VCO=%u is out of range, VCO may not lock!", vco);
    }

    USDR_LOG("7002", USDR_LOG_NOTE, "CGEN: VCO=%u DIV/2=%u N=%u frac=%u",
             vco, div2, vc.nint, vc.frac);

    uint32_t cgen_regs[] = {
        MAKE_LMS7002M_CGEN_ENABLE(0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1),
        MAKE_LMS7002M_CGEN_0x0087(vc.frac),
        MAKE_LMS7002M_CGEN_0x0088(vc.nint - 1, vc.frac >> 16),
        MAKE_LMS7002M_CGEN_0x0089(0, 0, 0, txdiv_ord, div2 - 1, 0),
    };

    int res = lms7002m_spi_post(m, cgen_regs, SIZEOF_ARRAY(cgen_regs));
    if (res)
        return res;

    usleep(20);

    uint8_t hi = 255, lo = 0;
    res = _lms7002m_vco_range(m, &lms7002m_cgen_trim_vco, (unsigned)-1, &hi, &lo, "CGEN");
    if (res < 0)
        return res;

    if (hi < lo) {
        USDR_LOG("7002", USDR_LOG_WARNING, "CGEN: Can't find sutable VCO cap!");
        goto tune_failed;
    }

    res = lms7002m_cgen_trim_vco(m, (hi + lo) / 2u);
    if (res < 0)
        return res;
    if (res != LMS7002M_VCO_OK) {
        USDR_LOG("7002", USDR_LOG_WARNING, "CGEN: Tuning is out of range %d => %d!\n",
                 (hi + lo) / 2u, res);
        return -ERANGE;
    }

    //TODO: Disable COMPARATOR power
    return 0;

tune_failed:
    lms7002m_cgen_disable(m);
    return -ERANGE;
}


int lms7002m_sxx_disable(lms7002m_state_t* m, lms7002m_sxx_path_t path)
{
    // SXR[0] / SXT[1]
    uint16_t mac = m->reg_mac;
    unsigned dir_idx = path == SXX_RX ? 0 : 1;

    SET_LMS7002M_LML_0X0020_MAC(mac, path == SXX_RX ? LMS7_CH_A : LMS7_CH_B);
    SET_LMS7002M_SXX_0X0124_EN_DIR_SXX(m->reg_en_dir[dir_idx], 0);

    uint32_t sxx_regs[] = {
        MAKE_LMS7002M_REG_WR(LML_0x0020, mac),
        MAKE_LMS7002M_REG_WR(SXX_0x0124, m->reg_en_dir[dir_idx]),
        MAKE_LMS7002M_REG_WR(LML_0x0020, m->reg_mac),
    };
    return lms7002m_spi_post(m, sxx_regs, SIZEOF_ARRAY(sxx_regs));
}

static int _lms7002m_sxx_pll_vco_set(lms7002m_state_t* m,
                                     unsigned fref, unsigned vco)
{
    struct pll_cfg pll = _pll_calc(fref, vco);
    uint32_t pll_regs[] = {
        MAKE_LMS7002M_SXX_0x011D(pll.frac),
        MAKE_LMS7002M_SXX_0x011E(pll.nint - 4, pll.frac >> 16),
    };
    return lms7002m_spi_post(m, pll_regs, SIZEOF_ARRAY(pll_regs));
}

int lms7002m_sxx_tune(lms7002m_state_t* m, lms7002m_sxx_path_t path, unsigned fref, unsigned lofreq, bool lochen)
{
    // SXR[0] / SXT[1]
    uint16_t mac = m->reg_mac;
    unsigned dir_idx = path == SXX_RX ? 0 : 1;
    unsigned div = 0;
    unsigned vco = lofreq;
    const char* sxxn = path == SXX_RX ? "SXR" : "SXT";
    int res;

    SET_LMS7002M_LML_0X0020_MAC(mac, path == SXX_RX ? LMS7_CH_A : LMS7_CH_B);
    SET_LMS7002M_SXX_0X0124_EN_DIR_SXX(m->reg_en_dir[dir_idx], 1);

    if (vco > SXX_VCOH_MAX) {
        USDR_LOG("7002", USDR_LOG_WARNING, "%s: VCO=%u is out of range\n", sxxn, lofreq);
        return -ERANGE;
    }
    while (vco < SXX_VCOL_MIN) {
        if (div > 5) {
            // Unable to deliver frequency
            USDR_LOG("7002", USDR_LOG_WARNING, "%s: LO=%u is out of range\n", sxxn, vco);
            return -ERANGE;
        }
        div++;
        vco <<= 1;
    }

    uint32_t sxx_regs[] = {
        MAKE_LMS7002M_REG_WR(LML_0x0020, mac),
        MAKE_LMS7002M_REG_WR(SXX_0x0124, m->reg_en_dir[dir_idx]),
        MAKE_LMS7002M_SXX_0x0120(204, 192),
        MAKE_LMS7002M_SXX_0x0122(0, 20, 20) | (1u<<13),
        MAKE_LMS7002M_SXX_0x011C(1, //RESET_N
                                 0, //SPDUP_VCO
                                 0, //BYPLDO_VCO
                                 0, //EN_COARSEPLL
                                 1, //CURLIM_VCO
                                 1u, //EN_DIV2_DIVPROG
                                 0, //EN_INTONLY_SDM
                                 1, //EN_SDM_CLK
                                 0, //PD_FBDIV
                                 (lochen) ? 0 : 1u, //PD_LOCH_T2RBUF
                                 0, //PD_CP
                                 0, //PD_FDIV
                                 0, //PD_SDM
                                 0, //PD_VCO_COMP
                                 0, //PD_VCO
                                 1),
        MAKE_LMS7002M_SXX_0x011F(3, 3, 6, 0, 0, 0, 0),
    };
    res = lms7002m_spi_post(m, sxx_regs, SIZEOF_ARRAY(sxx_regs));
    if (res)
        return res;

    bool vcoit[4] = {
        (SXX_VCOL_MIN < vco) && (vco < SXX_VCOL_MAX),
        (SXX_VCOM_MIN < vco) && (vco < SXX_VCOM_MAX),
        (SXX_VCOH_MIN < vco) && (vco < SXX_VCOH_MAX),
        (SXX_VCOH_MIN/2 < vco) && (vco < SXX_VCOH_MAX/2) && (div < 6),
    };

    USDR_LOG("7002", USDR_LOG_INFO, "%s: initial VCO=%u DIV=%u VCOs:%d%d%d%d",
             sxxn, vco, 1 << div,
             vcoit[0], vcoit[1], vcoit[2], vcoit[3]);

    static const unsigned vcono[4] = { 0, 1, 2, 2 };
    unsigned pvco_idx;
    int pcap;

    for (unsigned t = 0; t < 8; t++) {
        unsigned vrange[4] = { 0, 0, 0, 0 };
        unsigned vmid[4] = { 0, 0, 0, 0 };
        unsigned lidx = 0;

        pcap = -1;
        pvco_idx = 0;
        uint8_t plo = 0, phi = 0;
        for (unsigned i = 0; i < 4; i++) {
            if (!vcoit[i])
                continue;

            lidx = i;
            res = _lms7002m_sxx_pll_vco_set(m, i == 3 ? fref / 2 : fref, vco);
            if (res)
                return res;

            m->temp = vcono[i];
            res = _lms7002m_vco_range(m, &lms7002m_sxx_trim_vco, (unsigned)-1, &phi, &plo, sxxn);
            if (res != 0)
                return res;

            if (phi < plo)
                continue;

            int mid = ((int)plo + phi) / 2;
            if (phi >= plo) {
                vrange[i] = phi - plo + 1;
                vmid[i] = mid;
            }
        }

        unsigned best_idx = 0;
        unsigned best_val = 0;
        for (unsigned d = 0; d < 4; d++) {
            if (best_val < vrange[d]) {
                best_val = vrange[d];
                best_idx = d;
            }
        }

        USDR_LOG("7002", USDR_LOG_INFO, "%s: calibration => [ %d, %d, %d, %d ] => [ %d, %d, %d, %d ] best = %d last = %d\n", sxxn,
                 vrange[0], vrange[1], vrange[2], vrange[3],
                 vmid[0], vmid[1], vmid[2], vmid[3], best_idx, lidx);

        if (best_val > 0) {
            if (best_idx != lidx) {
                //Restore previous PLL settings
                res = _lms7002m_sxx_pll_vco_set(m, best_idx == 3 ? fref / 2 : fref, vco);
                if (res) {
                    return res;
                }
            }
            pcap = vmid[best_idx];
            pvco_idx = best_idx;
            break;
        }

        usleep(1000);
    }


    if (pcap == -1) {
        USDR_LOG("7002", USDR_LOG_INFO, "%s: Unable to tune to VCO=%u", sxxn, vco);
        return -ERANGE;
    }

    if (pvco_idx == 3) {
        div++;
    }

    // TODO disable comparators
    uint32_t sxx_fin_regs[] = {
        MAKE_LMS7002M_SXX_0x011F(3, 3, div, 0, 0, 0, 0),
        MAKE_LMS7002M_SXX_0x0121(16, (unsigned)pcap, vcono[pvco_idx], 0),
        MAKE_LMS7002M_REG_WR(LML_0x0020, m->reg_mac),
    };
    res = lms7002m_spi_post(m, sxx_fin_regs, SIZEOF_ARRAY(sxx_fin_regs));
    if (res)
        return res;

    return 0;
}


int lms7002m_afe_enable(lms7002m_state_t* m, bool rxa, bool rxb, bool txa, bool txb)
{
    uint32_t regs[] = {
        MAKE_LMS7002M_AFE_0x0082(4,
                                 AFE_0X0082_MODE_INTERLEAVE_AFE_2ADCS,
                                 AFE_0X0082_MUX_AFE_1_MUXOFF,
                                 AFE_0X0082_MUX_AFE_2_MUXOFF,
                                 0, //PD_AFE,
                                 rxa ? 0 : 1u, //PD_RX_AFE1
                                 rxb ? 0 : 1u, //PD_RX_AFE2
                                 txa ? 0 : 1u, //PD_TX_AFE1
                                 txb ? 0 : 1u, //PD_TX_AFE2
                                 rxa || rxb || txa || txb ? 1u : 0),
    };
    return lms7002m_spi_post(m, regs, SIZEOF_ARRAY(regs));
}

int lms7002m_dc_corr_en(lms7002m_state_t* m, bool rxaen, bool rxben, bool txaen, bool txben)
{
    uint32_t regs[] = {
        MAKE_LMS7002M_DC_PD((rxaen || rxben || txaen || txben) ? 1u : 0, // DCMODE,
                            rxben ? 0 : 1u, //PD_DCDAC_RXB,
                            rxaen ? 0 : 1u, //PD_DCDAC_RXA,
                            txben ? 0 : 1u, //PD_DCDAC_TXB,
                            txaen ? 0 : 1u, //PD_DCDAC_TXA,
                            rxben ? 0 : 1u, //PD_DCCMP_RXB,
                            rxaen ? 0 : 1u, //PD_DCCMP_RXA,
                            txben ? 0 : 1u, //PD_DCCMP_TXB,
                            txaen ? 0 : 1u //PD_DCCMP_TXA)
                            ),
        MAKE_LMS7002M_DC_0x05C2(0, 0, 0, 0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0, 0),
        MAKE_LMS7002M_DC_0x05CB(255, 255),

        MAKE_LMS7002M_DC_0x05C2(0, 0, 0, 0, 0, 0, 0, 0,
                                rxben, rxben, rxaen, rxaen, txben, txben, txaen, txaen),
    };

    return lms7002m_spi_post(m, regs, SIZEOF_ARRAY(regs));
}


int lms7002m_dc_corr(lms7002m_state_t* m, unsigned p, int16_t v)
{
    if (p > 7)
        return -EINVAL;

    uint32_t regs[] = {
        MAKE_LMS7002M_REG_WR(DC_0x05C3 + p, v & 0x7ff),
        MAKE_LMS7002M_REG_WR(DC_0x05C3 + p, (v & 0x7ff) | 0x8000),
    };
    return lms7002m_spi_post(m, regs, SIZEOF_ARRAY(regs));
}


int lms7002m_xxtsp_bst(lms7002m_state_t* m, lms7002m_xxtsp_t tsp)
{
    uint32_t reg_rxmod = MAKE_LMS7002M_RXTSP_0x0400(0,
                                                    RXTSP_0X0400_CAPSEL_RSSI, //CAPSEL
                                                    RXTSP_0X0400_CAPSEL_ADC_RXTSP_INPUT, //CAPSEL_ADC
                                                    RXTSP_0X0400_TSGFC_NEG6DB, //TSGFC,
                                                    RXTSP_0X0400_TSGFCW_DIV8, //TSGFCW,
                                                    0, //TSGDCLDQ
                                                    0, //TSGDCLDI
                                                    0, //TSGSWAPIQ,
                                                    RXTSP_0X0400_TSGMODE_DC, //TSGMODE,
                                                    RXTSP_0X0400_INSEL_LML, //INSEL,
                                                    0, //BSTART,
                                                    1);
    uint32_t reg_rxmod_s = reg_rxmod;
    SET_LMS7002M_RXTSP_0X0400_BSTART(reg_rxmod_s, 1);

    uint32_t reg_txmod = MAKE_LMS7002M_TXTSP_0x0200(TXTSP_0X0200_TSGFC_NEG6DB, //TSGFC,
                                                    TXTSP_0X0200_TSGFCW_DIV8, //TSGFCW,
                                                    0, //TSGDCLDQ
                                                    0, //TSGDCLDI
                                                    0, //TSGSWAPIQ,
                                                    TXTSP_0X0200_TSGMODE_DC, //TSGMODE,
                                                    TXTSP_0X0200_INSEL_LML, //INSEL,
                                                    0, //BSTART,
                                                    1u);
    uint32_t reg_txmod_s = reg_txmod;
    SET_LMS7002M_TXTSP_0X0200_BSTART(reg_txmod_s, 1);

    uint32_t xxtsp_regs[] = {
        (tsp == LMS_RXTSP) ? reg_rxmod : reg_txmod,
        (tsp == LMS_RXTSP) ? reg_rxmod_s : reg_txmod_s,
    };
    return lms7002m_spi_post(m, xxtsp_regs, SIZEOF_ARRAY(xxtsp_regs));
}

// xxTSP
int lms7002m_xxtsp_enable(lms7002m_state_t* m, lms7002m_xxtsp_t tsp, bool enable)
{
    uint32_t reg_rxdsp = MAKE_LMS7002M_RXTSP_0x040C(0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1);
    uint32_t reg_rxmod = MAKE_LMS7002M_RXTSP_0x0400(0,
                                                    RXTSP_0X0400_CAPSEL_RSSI, //CAPSEL
                                                    RXTSP_0X0400_CAPSEL_ADC_RXTSP_INPUT, //CAPSEL_ADC
                                                    RXTSP_0X0400_TSGFC_NEG6DB, //TSGFC,
                                                    RXTSP_0X0400_TSGFCW_DIV8, //TSGFCW,
                                                    0, //TSGDCLDQ
                                                    0, //TSGDCLDI
                                                    0, //TSGSWAPIQ,
                                                    RXTSP_0X0400_TSGMODE_DC, //TSGMODE,
                                                    RXTSP_0X0400_INSEL_LML, //INSEL,
                                                    0, //BSTART,
                                                    enable ? 1u : 0);
    uint32_t reg_txdsp = MAKE_LMS7002M_TXTSP_0x0208(0, 0, 0, 1, 1, 1, 1, 0, 1, 1);
    uint32_t reg_txmod = MAKE_LMS7002M_TXTSP_0x0200(TXTSP_0X0200_TSGFC_NEG6DB, //TSGFC,
                                                    TXTSP_0X0200_TSGFCW_DIV8, //TSGFCW,
                                                    0, //TSGDCLDQ
                                                    0, //TSGDCLDI
                                                    0, //TSGSWAPIQ,
                                                    TXTSP_0X0200_TSGMODE_DC, //TSGMODE,
                                                    TXTSP_0X0200_INSEL_LML, //INSEL,
                                                    0, //BSTART,
                                                    enable ? 1u : 0);
    uint32_t xxtsp_regs[] = {
        (tsp == LMS_RXTSP) ? reg_rxdsp : reg_txdsp,
        (tsp == LMS_RXTSP) ? reg_rxmod : reg_txmod,
    };
    uint16_t *cfgregs = (tsp == LMS_RXTSP) ? m->reg_rxtsp_dscpcfg : m->reg_txtsp_dscpcfg;
    uint16_t *modregs = (tsp == LMS_RXTSP) ? m->reg_rxtsp_dscmode : m->reg_txtsp_dscmode;
    unsigned mac = GET_LMS7002M_LML_0X0020_MAC(m->reg_mac);
    if (mac & LMS7_CH_A) {
        cfgregs[0] = xxtsp_regs[0];
        modregs[0] = xxtsp_regs[1];
    }
    if (mac & LMS7_CH_B) {
        cfgregs[1] = xxtsp_regs[0];
        modregs[1] = xxtsp_regs[1];
    }
    return lms7002m_spi_post(m, xxtsp_regs, SIZEOF_ARRAY(xxtsp_regs));
}

int lms7002m_xxtsp_int_dec(lms7002m_state_t* m, lms7002m_xxtsp_t tsp, unsigned intdec_ord)
{
    unsigned mac = GET_LMS7002M_LML_0X0020_MAC(m->reg_mac);
    if (mac == LMS7_CH_NONE)
        return -EINVAL;

    if (tsp == LMS_RXTSP) {
        SET_LMS7002M_RXTSP_0X0403_HBD_OVR(m->reg_rxtsp_hbdo_iq[0], intdec_ord - 1u);
        SET_LMS7002M_RXTSP_0X0403_HBD_OVR(m->reg_rxtsp_hbdo_iq[1], intdec_ord - 1u);
    } else {
        SET_LMS7002M_TXTSP_0X0203_HBI_OVR(m->reg_txtsp_hbdo_iq[0], intdec_ord - 1u);
        SET_LMS7002M_TXTSP_0X0203_HBI_OVR(m->reg_txtsp_hbdo_iq[1], intdec_ord - 1u);
    }

    uint16_t* hbd = (tsp == LMS_RXTSP) ? m->reg_rxtsp_hbdo_iq : m->reg_txtsp_hbdo_iq;
    uint16_t maca = m->reg_mac, macb = m->reg_mac;
    SET_LMS7002M_LML_0X0020_MAC(maca, LMS7_CH_A);
    SET_LMS7002M_LML_0X0020_MAC(macb, LMS7_CH_B);

    uint32_t regs[] = {
        MAKE_LMS7002M_REG_WR(LML_0x0020, maca),
        MAKE_LMS7002M_REG_WR((tsp == LMS_RXTSP) ? RXTSP_0x0403 : TXTSP_0x0203, hbd[0]),
        MAKE_LMS7002M_REG_WR(LML_0x0020, macb),
        MAKE_LMS7002M_REG_WR((tsp == LMS_RXTSP) ? RXTSP_0x0403 : TXTSP_0x0203, hbd[1]),
        MAKE_LMS7002M_REG_WR(LML_0x0020, m->reg_mac),
    };
    return lms7002m_spi_post(m, regs, SIZEOF_ARRAY(regs));
}

static int _lms7002m_xxtsp_wregwith_dspcfg(lms7002m_state_t* m, lms7002m_xxtsp_t tsp, uint32_t* regs, const unsigned reg_count)
{
    unsigned mac = GET_LMS7002M_LML_0X0020_MAC(m->reg_mac);
    if (mac == LMS7_CH_NONE)
        return -EINVAL;

    uint16_t* dscpcfg = (tsp == LMS_RXTSP) ? m->reg_rxtsp_dscpcfg : m->reg_txtsp_dscpcfg;
    uint16_t maca = m->reg_mac, macb = m->reg_mac;
    SET_LMS7002M_LML_0X0020_MAC(maca, LMS7_CH_A);
    SET_LMS7002M_LML_0X0020_MAC(macb, LMS7_CH_B);

    uint32_t regs_ab[] = {
        MAKE_LMS7002M_REG_WR(LML_0x0020, maca),
        MAKE_LMS7002M_REG_WR((tsp == LMS_RXTSP) ? RXTSP_0x040C : TXTSP_0x0208, dscpcfg[0]),
        MAKE_LMS7002M_REG_WR(LML_0x0020, macb),
        MAKE_LMS7002M_REG_WR((tsp == LMS_RXTSP) ? RXTSP_0x040C : TXTSP_0x0208, dscpcfg[1]),
        MAKE_LMS7002M_REG_WR(LML_0x0020, m->reg_mac),
    };

    uint32_t *pregs = (mac == LMS7_CH_AB) ? regs_ab : (mac == LMS7_CH_A) ? &regs_ab[1] : &regs_ab[3];
    const unsigned pregs_cnt = (mac == LMS7_CH_AB) ? SIZEOF_ARRAY(regs_ab) : 1;
    const unsigned all_regs_count = pregs_cnt + reg_count;
    uint32_t regs_all[all_regs_count];
    memcpy(regs_all, regs, reg_count * sizeof(uint32_t));
    memcpy(regs_all + reg_count, pregs, pregs_cnt * sizeof(uint32_t));

    return lms7002m_spi_post(m, regs_all, all_regs_count);
}

static void _lms7002m_mask_field_set(lms7002m_state_t* m, uint16_t reg[2], unsigned offset, unsigned mask, unsigned val)
{
    for (unsigned i = 0; i < 2; i++) {
        if (!_lms7002m_check_chan(m, i))
            continue;

        reg[i] = (reg[i] & ~mask) | ((val << offset) & mask);
    }
}

int lms7002m_rxtsp_dc_corr(lms7002m_state_t* m, bool byp, unsigned wnd)
{
    _lms7002m_mask_field_set(m, m->reg_rxtsp_dscpcfg, RXTSP_0X040C_DC_BYP_OFF, RXTSP_0X040C_DC_BYP_MSK, byp ? 1 : 0);
    uint32_t regs[] = {
        MAKE_LMS7002M_RXTSP_0x0404(0, wnd),
    };
    return _lms7002m_xxtsp_wregwith_dspcfg(m, LMS_RXTSP, regs, SIZEOF_ARRAY(regs));
}

int lms7002m_xxtsp_gen(lms7002m_state_t* m, lms7002m_xxtsp_t tsp, lms7002m_xxtsp_gen_t gen,
                       int16_t dci, int16_t dcq)
{
    uint16_t reg_cfg;
    reg_cfg = (uint16_t)MAKE_LMS7002M_RXTSP_0x0400(
        0,
        RXTSP_0X0400_CAPSEL_RSSI,
        RXTSP_0X0400_CAPSEL_ADC_RXTSP_INPUT,
        dci == 0 ? RXTSP_0X0400_TSGFC_FS : RXTSP_0X0400_TSGFC_NEG6DB,
        dcq == 0 ? RXTSP_0X0400_TSGFCW_DIV4 : RXTSP_0X0400_TSGFCW_DIV8,
        0,
        0,
        0,
        gen == XXTSP_DC ? RXTSP_0X0400_TSGMODE_DC : RXTSP_0X0400_TSGMODE_NCO,
        gen == XXTSP_NORMAL ? RXTSP_0X0400_INSEL_LML : RXTSP_0X0400_INSEL_TEST,
        0,
        1);

    uint32_t gen_regs[] = {
        MAKE_LMS7002M_REG_WR(tsp == LMS_RXTSP ? RXTSP_0x0400 : TXTSP_0x0200, reg_cfg),
        MAKE_LMS7002M_REG_WR(tsp == LMS_RXTSP ? RXTSP_0x040B : TXTSP_0x020C, dci),
        MAKE_LMS7002M_REG_WR(tsp == LMS_RXTSP ? RXTSP_0x0400 : TXTSP_0x0200, reg_cfg | RXTSP_0X0400_TSGDCLDI_MSK),
        MAKE_LMS7002M_REG_WR(tsp == LMS_RXTSP ? RXTSP_0x040B : TXTSP_0x020C, dcq),
        MAKE_LMS7002M_REG_WR(tsp == LMS_RXTSP ? RXTSP_0x0400 : TXTSP_0x0200, reg_cfg | RXTSP_0X0400_TSGDCLDQ_MSK),
    };

    return lms7002m_spi_post(m, gen_regs, gen == XXTSP_DC ? SIZEOF_ARRAY(gen_regs) : 1);
}

int lms7002m_xxtsp_cmix(lms7002m_state_t* m, lms7002m_xxtsp_t tsp, int32_t freq)
{
    uint16_t *dscpcfg = (tsp == LMS_RXTSP) ? m->reg_rxtsp_dscpcfg : m->reg_txtsp_dscpcfg;
    _lms7002m_mask_field_set(m, dscpcfg, RXTSP_0X040C_CMIX_BYP_OFF, RXTSP_0X040C_CMIX_BYP_MSK, freq == 0 ? 1 : 0);
    _lms7002m_mask_field_set(m, dscpcfg, RXTSP_0X040C_CMIX_SC_OFF, RXTSP_0X040C_CMIX_SC_MSK, freq < 0 ? 1 : 0);

    if (freq < 0) {
        freq = -freq;
    }

    uint32_t regs[] = {
        (tsp == LMS_RXTSP) ? MAKE_LMS7002M_RXNCO_0x0442((uint32_t)freq >> 16) : MAKE_LMS7002M_TXNCO_0x0242((uint32_t)freq >> 16),
        (tsp == LMS_RXTSP) ? MAKE_LMS7002M_RXNCO_0x0443((uint32_t)freq) : MAKE_LMS7002M_TXNCO_0x0243((uint32_t)freq),
        (tsp == LMS_RXTSP) ? MAKE_LMS7002M_RXNCO_0x0440(1, 0, RXNCO_0X0440_MODE_FCW) : MAKE_LMS7002M_TXNCO_0x0240(1, 0, TXNCO_0X0240_MODE_FCW),
    };

    return _lms7002m_xxtsp_wregwith_dspcfg(m, tsp, regs, SIZEOF_ARRAY(regs));
}

int lms7002m_xxtsp_iq_gcorr(lms7002m_state_t* m, lms7002m_xxtsp_t tsp, unsigned ig, unsigned qg)
{
    uint16_t *dscpcfg = (tsp == LMS_RXTSP) ? m->reg_rxtsp_dscpcfg : m->reg_txtsp_dscpcfg;
    _lms7002m_mask_field_set(m, dscpcfg, RXTSP_0X040C_GC_BYP_OFF, RXTSP_0X040C_GC_BYP_MSK, (ig == 0 && qg == 0) ? 1 : 0);

    uint32_t regs[] = {
        (tsp == LMS_RXTSP) ? MAKE_LMS7002M_RXTSP_0x0401(qg) : MAKE_LMS7002M_TXTSP_0x0201(qg),
        (tsp == LMS_RXTSP) ? MAKE_LMS7002M_RXTSP_0x0402(ig) : MAKE_LMS7002M_TXTSP_0x0202(ig),
    };

    return _lms7002m_xxtsp_wregwith_dspcfg(m, tsp, regs, SIZEOF_ARRAY(regs));
}

int lms7002m_xxtsp_iq_phcorr(lms7002m_state_t* m, lms7002m_xxtsp_t tsp, int acorr)
{
    uint16_t *dscpcfg = (tsp == LMS_RXTSP) ? m->reg_rxtsp_dscpcfg : m->reg_txtsp_dscpcfg;
    _lms7002m_mask_field_set(m, dscpcfg, RXTSP_0X040C_PH_BYP_OFF, RXTSP_0X040C_PH_BYP_MSK, (acorr == 0) ? 1 : 0);

    if (tsp == LMS_RXTSP) {
        if (m->reg_mac & LMS7_CH_A) {
            SET_LMS7002M_RXTSP_0X0403_IQCORR(m->reg_rxtsp_hbdo_iq[0], acorr);
        }
        if (m->reg_mac & LMS7_CH_B) {
            SET_LMS7002M_RXTSP_0X0403_IQCORR(m->reg_rxtsp_hbdo_iq[1], acorr);
        }
    } else {
        if (m->reg_mac & LMS7_CH_A) {
            SET_LMS7002M_TXTSP_0X0203_IQCORR(m->reg_txtsp_hbdo_iq[0], acorr);
        }
        if (m->reg_mac & LMS7_CH_B) {
            SET_LMS7002M_TXTSP_0X0203_IQCORR(m->reg_txtsp_hbdo_iq[1], acorr);
        }
    }

    // hbbovr should be the same for both channels, no need to set individual values for A & B
    uint16_t* hbd = (tsp == LMS_RXTSP) ? m->reg_rxtsp_hbdo_iq : m->reg_txtsp_hbdo_iq;
    uint32_t regs[] = {
        MAKE_LMS7002M_REG_WR((tsp == LMS_RXTSP) ? RXTSP_0x0403 : TXTSP_0x0203, hbd[ m->reg_mac & LMS7_CH_A ? 0 : 1 ]),
    };
    return lms7002m_spi_post(m, regs, SIZEOF_ARRAY(regs));
}


int lms7002m_rfe_path(lms7002m_state_t* m, lms7002m_rfe_path_t p, lms7002m_rfe_mode_t mode)
{
    if (_lms7002m_is_none(m))
        return -EINVAL;

    for (unsigned i = 0; i < 2; i++) {
        if (!_lms7002m_check_chan(m, i))
            continue;

        m->rfe[i].en = mode != RFE_MODE_DISABLE;
        m->rfe[i].lb = mode == RFE_MODE_LOOPBACKRF;
        m->rfe[i].path = p;
    }

    SET_LMS7002M_SXX_0X0124_EN_DIR_RFE(m->reg_en_dir[0], m->rfe[0].en || m->rfe[1].en);
    SET_LMS7002M_SXX_0X0124_EN_DIR_RFE(m->reg_en_dir[1], m->rfe[1].en);

    uint32_t regs[2 * 5 + 1], j = 0;
    for (unsigned i = 0; i < 2; i++) {
        uint16_t mac = m->reg_mac;
        SET_LMS7002M_LML_0X0020_MAC(mac, i + 1);
        regs[j++] = MAKE_LMS7002M_REG_WR(LML_0x0020, mac);
        regs[j++] = MAKE_LMS7002M_REG_WR(SXX_0x0124, m->reg_en_dir[i]);
        regs[j++] = MAKE_LMS7002M_RFE_0x010C(8, 8,
                                             m->rfe[i].en && (m->rfe[i].lb ? 1 : 0),
                                             m->rfe[i].en && m->rfe[i].lb && (m->rfe[i].path == RFE_LNAW || m->rfe[i].path == RFE_LNAH ? 0 : 1),
                                             m->rfe[i].en && m->rfe[i].lb && (m->rfe[i].path == RFE_LNAL ? 0 : 1),
                                             !m->rfe[i].en,
                                             !m->rfe[i].en,
                                             1,
                                             !m->rfe[i].en,
                                             m->rfe[i].en);
        regs[j++] = MAKE_LMS7002M_RFE_0x010D(m->rfe[i].path,
                                             1,
                                             !m->rfe[i].en || (m->rfe[i].lb && (m->rfe[i].path == RFE_LNAW || m->rfe[i].path == RFE_LNAH ? 0 : 1)),
                                             !m->rfe[i].en || (m->rfe[i].lb && (m->rfe[i].path == RFE_LNAL ? 0 : 1)),
                                             !m->rfe[i].en || (!m->rfe[i].lb && (m->rfe[i].path == RFE_LNAL ? 0 : 1)),
                                             !m->rfe[i].en || (!m->rfe[i].lb && (m->rfe[i].path == RFE_LNAW ? 0 : 1)),
                                             (i == 0) && m->rfe[i + 1].en);
        regs[j++] = MAKE_LMS7002M_RFE_0x010F(m->rfe[i].lb ? 3 : 0, 2, 2);
    };
    regs[j++] = MAKE_LMS7002M_REG_WR(LML_0x0020, m->reg_mac);

    return lms7002m_spi_post(m, regs, j);
}

static int _lms7002m_rfe_update_gains(lms7002m_state_t* m)
{
    uint32_t regs[2 * 2 + 1], j = 0;
    for (unsigned i = 0; i < 2; i++) {
        uint16_t mac = m->reg_mac;
        SET_LMS7002M_LML_0X0020_MAC(mac, i + 1);
        regs[j++] = MAKE_LMS7002M_REG_WR(LML_0x0020, mac);
        regs[j++] = MAKE_LMS7002M_RFE_0x0113(
            m->rfe[i].lb ? 1 : m->rfe[i].lna,
            m->rfe[i].lb ? m->rfe[i].lbg : 0,
            m->rfe[i].tia);
    };

    regs[j++] = MAKE_LMS7002M_REG_WR(LML_0x0020, m->reg_mac);
    return lms7002m_spi_post(m, regs, j);
}

enum {
    IGNORE_IDX = 100000, // Not allowed tap
};

static unsigned _find_idx(int value, const int* list, unsigned count)
{
    unsigned i;
    for (i = 0; i < count; i++) {
        if (list[i] == IGNORE_IDX)
            continue;

        if (list[i] < value)
            break;
    }
    return i;
}

int lms7002m_rfe_gain(lms7002m_state_t* m, lms7002m_rfe_gain_t gain, int gainx10, int *goutx10)
{
    static const int lna_attens[] = {
        IGNORE_IDX, 300, 270, 240, 210, 180, 150, 120, 90, 60, 50, 40, 30, 20, 10, 0
    };
    static const int tia_attens[] = {
        IGNORE_IDX, 120, 30, 0
    };
    static const int lb_attens[] = {
        400, 240, 170, 140, 110, 90, 75, 62, 50, 40, 30, 24, 16, 10, 5, 0
    };
    unsigned idx;

    switch (gain) {
    case RFE_GAIN_LNA:
        idx = _find_idx(-gainx10, lna_attens, SIZEOF_ARRAY(lna_attens) - 1);
        *goutx10 = -lna_attens[idx];
        goto update_vals;
    case RFE_GAIN_TIA:
        idx = _find_idx(-gainx10, tia_attens, SIZEOF_ARRAY(tia_attens) - 1);
        *goutx10 = -tia_attens[idx];
        goto update_vals;
    case RFE_GAIN_RFB:
        idx = _find_idx(-gainx10, lb_attens, SIZEOF_ARRAY(lb_attens) - 1);
        *goutx10 = -lb_attens[idx];
        goto update_vals;
    update_vals:
        for (unsigned i = 0; i < 2; i++) {
            if (!_lms7002m_check_chan(m, i))
                continue;

            switch (gain) {
            case RFE_GAIN_LNA: m->rfe[i].lna = idx; break;
            case RFE_GAIN_TIA: m->rfe[i].tia = idx; break;
            case RFE_GAIN_RFB: m->rfe[i].lbg = idx; break;
            default: break;
            }
        }
    default: break;
    }

    return _lms7002m_rfe_update_gains(m);
}


// TRF
int lms7002m_trf_path(lms7002m_state_t* m,lms7002m_trf_path_t path, lms7002m_trf_mode_t mode)
{
    if (_lms7002m_is_none(m))
        return -EINVAL;

    for (unsigned i = 0; i < 2; i++) {
        if (!_lms7002m_check_chan(m, i))
            continue;

        m->trf[i].en = mode != TRF_MODE_DISABLE;
        m->trf[i].lb = mode == TRF_MODE_LOOPBACK;
        m->trf[i].path = path;
    }

    SET_LMS7002M_SXX_0X0124_EN_DIR_TRF(m->reg_en_dir[0], m->rfe[0].en || m->rfe[1].en);
    SET_LMS7002M_SXX_0X0124_EN_DIR_TRF(m->reg_en_dir[1], m->rfe[1].en);

    uint32_t regs[2 * 4 + 2], j = 0;
    for (unsigned i = 0; i < 2; i++) {
        uint16_t mac = m->reg_mac;
        SET_LMS7002M_LML_0X0020_MAC(mac, i + 1);
        regs[j++] = MAKE_LMS7002M_REG_WR(LML_0x0020, mac);
        regs[j++] = MAKE_LMS7002M_REG_WR(SXX_0x0124, m->reg_en_dir[i]);
        regs[j++] = MAKE_LMS7002M_TRF_0x0100(
            0,
            i == 0 ? m->trf[1].en : 0,
            2, //3, //EN_AMPHF_PDET_TRF,
            1,
            1,
            0,
            0,
            (i == 0 ? m->trf[1].en : 0) | m->trf[i].en);
        regs[j++] = MAKE_LMS7002M_TRF_0x0101(
            3,     //F_TXPAD_TRF,
            m->trf[i].lb ? m->trf[i].lbloss : LB_LOSS_24,  //L_LOOPB_TXPAD_TRF,
            m->trf[i].gain,    //LOSS_LIN_TXPAD_TRF,
            m->trf[i].gain,    //LOSS_MAIN_TXPAD_TRF,
            m->trf[i].lb);     //EN_LOOPB_TXPAD_TRF
    };
    regs[j++] = MAKE_LMS7002M_REG_WR(LML_0x0020, m->reg_mac);
    regs[j++] = MAKE_LMS7002M_TRF_0x0103(
        (path == TRF_B1) ? 1u : 0,
        (path == TRF_B2) ? 1u : 0,
        16, 18);

    return lms7002m_spi_post(m, regs, j);
}

int lms7002m_trf_gain(lms7002m_state_t* m, lms7002m_trf_gain_t gt, int gainx10, int *goutx10)
{
    if (_lms7002m_is_none(m))
        return -EINVAL;

    int atten = -gainx10 / 10;
    if (gt == TRF_GAIN_PAD) {
        if (atten > 52) {
            atten = 31;
            if (goutx10)
                *goutx10 = -520;
        } else if (atten > 10) {
            atten = (atten + 10) / 2;
            if (goutx10)
                *goutx10 = -10 * (atten * 2 - 10);
        } else {
            if (goutx10)
                *goutx10 = -10 * atten;
        }
    } else {
        if (atten > 24)
            atten = LB_LOSS_24;
        else if (atten > 20)
            atten = LB_LOSS_21;
        else if (atten > 13)
            atten = LB_LOSS_14;
        else
            atten = LB_LOSS_0;
    }


    for (unsigned i = 0; i < 2; i++) {
        if (!_lms7002m_check_chan(m, i))
            continue;

        if (gt == TRF_GAIN_PAD)
            m->trf[i].gain = atten;
        else
            m->trf[i].lbloss = atten;
    }


    uint32_t regs[2 * 3 + 2], j = 0;
    for (unsigned i = 0; i < 2; i++) {
        uint16_t mac = m->reg_mac;
        unsigned lb_loss = m->trf[i].lb ? m->trf[i].lbloss : LB_LOSS_24;
        SET_LMS7002M_LML_0X0020_MAC(mac, i + 1);
        regs[j++] = MAKE_LMS7002M_REG_WR(LML_0x0020, mac);
        regs[j++] = MAKE_LMS7002M_TRF_0x0101(
            3,     //F_TXPAD_TRF,
            lb_loss,  //L_LOOPB_TXPAD_TRF,
            m->trf[i].gain,    //LOSS_LIN_TXPAD_TRF,
            m->trf[i].gain,    //LOSS_MAIN_TXPAD_TRF,
            m->trf[i].lb);     //EN_LOOPB_TXPAD_TRF

        USDR_LOG("7002", USDR_LOG_INFO, "trf_gain[%d] lb_loss=%d loss=%d en_lb=%d\n",
                 i,  lb_loss, m->trf[i].gain, m->trf[i].lb);
    };
    regs[j++] = MAKE_LMS7002M_REG_WR(LML_0x0020, m->reg_mac);

    return lms7002m_spi_post(m, regs, j);
}

// RBB
int lms7002m_rbb_path(lms7002m_state_t* m, lms7002m_rbb_path_t path, lms7002m_rbb_mode_t mode)
{
    bool en = mode != RBB_MODE_DISABLE;
    _lms7002m_mask_field_set(m, m->reg_en_dir, SXX_0X0124_EN_DIR_RBB_OFF, SXX_0X0124_EN_DIR_RBB_MSK, en);

    uint32_t rbb_regs[] = {
        MAKE_LMS7002M_RBB_0x0115(
            (mode == RBB_MODE_LOOPBACK && path == RBB_HBF) ? 1u : 0,
            (mode == RBB_MODE_LOOPBACK && path == RBB_LBF) ? 1u : 0,
            (path == RBB_HBF) ? 0 : 1u,
            (path == RBB_LBF) ? 0 : 1u,
            0,
            en),

        MAKE_LMS7002M_RBB_0x0118(
            (path == RBB_LBF) ? RBB_0X0118_INPUT_CTL_PGA_RBB_LPFL :
                (path == RBB_HBF) ? RBB_0X0118_INPUT_CTL_PGA_RBB_LPFH :
                (mode == RBB_MODE_LOOPBACK && path == RBB_BYP) ? RBB_0X0118_INPUT_CTL_PGA_RBB_TBB : RBB_0X0118_INPUT_CTL_PGA_RBB_BYPASS,
            24,
            24),
    };

    return lms7002m_spi_post(m, rbb_regs, SIZEOF_ARRAY(rbb_regs));
}


int lms7002m_rbb_pga(lms7002m_state_t* m, int gainx10)
{
    static const uint8_t rcc_corr[] = { 31, 30, 29, 29, 28, 27, 26, 26, 25, 24, 24, 23, 23, 22, 22, 21,
                                        21, 20, 20, 19, 19, 19, 18, 18, 18, 17, 17, 17, 16, 16, 16, 16 };
    if (gainx10 < 0)
        gainx10 = 0;
    else if (gainx10 > 310)
        gainx10 = 310;

    unsigned gain = gainx10 / 10;
    unsigned rcc_ctl = rcc_corr[gain];
    unsigned c_ctl = (gain < 8) ? 3u : (gain < 13) ? 2u : (gain < 21) ? 1u : 0;

    uint32_t regs[] = {
        MAKE_LMS7002M_RBB_0x0119(0, 20, 20, gain),
        MAKE_LMS7002M_RBB_0x011A(rcc_ctl, c_ctl)
    };
    return lms7002m_spi_post(m, regs, SIZEOF_ARRAY(regs));
}

int lms7002m_rbb_lpf_raw(lms7002m_state_t* m, lms7002m_lpf_params_t params)
{
    uint32_t regs[] = {
        MAKE_LMS7002M_RBB_0x0116(params.r, params.rcc, params.c),
        MAKE_LMS7002M_RBB_0x0117(params.rcc, params.c),
    };
    return lms7002m_spi_post(m, regs, SIZEOF_ARRAY(regs));
}

int lms7002m_rbb_lpf_def(unsigned bw, bool lpf_l, lms7002m_lpf_params_t *params)
{
    if (lpf_l) {
        int rcc_ctl_lpfl_rbb = 0;
        int c_ctl_lpfl_rbb = (int)(2160000000U/bw - 103);

        c_ctl_lpfl_rbb = CLAMP(c_ctl_lpfl_rbb, 0, 2047);

        if (bw > 15000000)
            rcc_ctl_lpfl_rbb = 5;
        else if (bw > 10000000)
            rcc_ctl_lpfl_rbb = 4;
        else if (bw > 5000000)
            rcc_ctl_lpfl_rbb = 3;
        else if (bw > 3000000)
            rcc_ctl_lpfl_rbb = 2;
        else if (bw > 1400000)
            rcc_ctl_lpfl_rbb = 1;
        else
            rcc_ctl_lpfl_rbb = 0;

        params->rcc = rcc_ctl_lpfl_rbb;
        params->c = c_ctl_lpfl_rbb;
        params->r = 16;
    } else {
        int c_ctl_lpfh_rbb = (int)(6000.0e6/bw - 50);
        int rcc_ctl_lpfh_rbb = (int)(bw/10e6 - 3);

        c_ctl_lpfh_rbb = CLAMP(c_ctl_lpfh_rbb, 0, 255);
        rcc_ctl_lpfh_rbb = CLAMP(rcc_ctl_lpfh_rbb, 0, 7);

        params->rcc = rcc_ctl_lpfh_rbb;
        params->c = c_ctl_lpfh_rbb;
        params->r = 16;
    }

    return 0;
}

int lms7002m_tbb_path(lms7002m_state_t* m, lms7002m_tbb_path_t path, lms7002m_tbb_mode_t mode)
{
    bool en = mode != TBB_MODE_DISABLE;
    _lms7002m_mask_field_set(m, m->reg_en_dir, SXX_0X0124_EN_DIR_TBB_OFF, SXX_0X0124_EN_DIR_TBB_MSK, en);

    uint32_t tbb_regs[] = {
        MAKE_LMS7002M_TBB_0x0105(
            0, //STATPULSE_TBB,
            mode == TBB_MODE_LOOPBACK_SWAPIQ ? 1 : 0,
            mode == TBB_MODE_NORMAL ? TBB_0X0105_LOOPB_NORMAL : TBB_0X0105_LOOPB_LB_TBB_OUT,
            (path == TBB_HBF) ? 0 : 1u, //PD_LPFH_TBB,
            0, //PD_LPFIAMP_TBB,
            (path == TBB_LAD) ? 0 : 1u, //PD_LPFLAD_TBB,
            (path == TBB_LAD) ? 0 : 1u, //PD_LPFS5_TBB,
            en),
        MAKE_LMS7002M_TBB_0x010A(TBB_0X010A_TSTIN_TBB_DISABLED, 0, 0, 0),
        MAKE_LMS7002M_TBB_LPFS5(1),
    };

    return lms7002m_spi_post(m, tbb_regs, SIZEOF_ARRAY(tbb_regs));
}

int lms7002m_tbb_lpf_def(unsigned bw, bool lpf_l, lms7002m_lpf_params_t *params)
{
    if (lpf_l) {
        if (bw < 1000000)
            bw = 1000000;
        else if (bw > 20000000)
            bw = 20000000;

        const double f = bw/1e6;
        const double p1 = 1.29858903647958E-16;
        const double p2 = -0.000110746929967704;
        const double p3 = 0.00277593485991029;
        const double p4 = 21.0384293169607;
        const double p5 = -48.4092606238297;
        int rcal_lpflad_tbb = (int)(f*f*f*f*p1 + f*f*f*p2 + f*f*p3 + f*p4 + p5);
        params->r = rcal_lpflad_tbb;
        params->rcc = (bw + 1000000) / 2000000;
    } else {
        if (bw < 20000000)
            bw = 20000000;
        else if (bw > 80000000)
            bw = 80000000;

        const double f = bw/1e6;
        const double p1 = 1.10383261611112E-06;
        const double p2 = -0.000210800032517545;
        const double p3 = 0.0190494874803309;
        const double p4 = 1.43317445923528;
        const double p5 = -47.6950779298333;
        int rcal_lpfh_tbb = (int)(f*f*f*f*p1 + f*f*f*p2 + f*f*p3 + f*p4 + p5);
        params->r = rcal_lpfh_tbb;
        params->rcc = (bw + 500000) / 1000000;
    }

    params->r = CLAMP(params->r, 0, 255);
    params->rcc = CLAMP(params->rcc, 1, 63);
    return 0;
}

int lms7002m_tbb_lpf_raw(lms7002m_state_t* m, lms7002m_lpf_params_t params)
{
    // TODO channel A/B
    int gc = m->reg_tbb_gc_corr[0];
    m->reg_tbb_gc[0] = m->reg_tbb_gc[1] = params.rcc;

    gc = CLAMP(gc + params.rcc, 1, 63);

    uint32_t tbb_regs[] = {
        MAKE_LMS7002M_TBB_0x0109(params.r, params.r),
        MAKE_LMS7002M_TBB_0x0108(gc, 12, 12),
    };

    return lms7002m_spi_post(m, tbb_regs, SIZEOF_ARRAY(tbb_regs));
}

int lms7002m_tbb_gain(lms7002m_state_t* m, int8_t gain)
{
    int gc = m->reg_tbb_gc[0];
    m->reg_tbb_gc_corr[0] = m->reg_tbb_gc_corr[1] = gain;

    gc = CLAMP(gain + gc, 1, 63);

    uint32_t tbb_regs[] = {
        MAKE_LMS7002M_TBB_0x0108(gc, 12, 12),
    };

    return lms7002m_spi_post(m, tbb_regs, SIZEOF_ARRAY(tbb_regs));
}

lms7002m_trf_path_t lms7002m_trf_from_rfe_path(lms7002m_rfe_path_t rfe_path)
{
    switch (rfe_path) {
    case RFE_LNAL: return TRF_B2;
    case RFE_LNAW: return TRF_B1;
    case RFE_LNAH: return TRF_B1;
    default: return TRF_MUTE;
    }
}
