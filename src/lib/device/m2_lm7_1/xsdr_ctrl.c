// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "xsdr_ctrl.h"

#include "../hw/lp8758/lp8758.h"
#include "../hw/tmp114/tmp114.h"
#include "../hw/tmp108/tmp108.h"
#include "../hw/dac80501/dac80501.h"


#include <usdr_logging.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#include "../cal/cal_lo_iqimb.h"
#include "../ipblks/streams/sfe_rx_4.h"
#include "../ipblks/xlnx_mmcm.h"

#ifndef MAX
#define MAX(x,y) (((x) > (y)) ? (x) : (y))
#endif


//         TXA     RXB
//         TXB     RXA
// SSDR RF routing infortmation
// LMS8_CHA <= TXOUT_A_2 <= BAND2
// LMS8_CHB <= TXOUT_B_2 <= BAND2
// LMS8_CHC => RXIN_A_2  => LNA_H
// LMS8_CHD => RXIN_B_2  => LNA_H


enum {
    XSDR_INT_REFCLK = 26000000,
};

// 1001011 - PDAC80501MDQFT
// 1100010 - MCP4725A1T
enum BUSIDX_mp_lm7_1_rev000 {
    I2C_BUS_LP8758_FPGA    = MAKE_LSOP_I2C_ADDR(0, 0, I2C_DEV_PMIC_FPGA),
    I2C_BUS_LP8758_LMSINIT = MAKE_LSOP_I2C_ADDR(0, 1, I2C_DEV_PMIC_FPGA),

    I2C_BUS_TMP_108        = MAKE_LSOP_I2C_ADDR(0, 0, I2C_DEV_TMP108A_A0_SDA),
    I2C_BUS_TMP_114        = MAKE_LSOP_I2C_ADDR(0, 0, I2C_DEV_TMP114NB),

    I2C_BUS_DAC_MCP4725    = MAKE_LSOP_I2C_ADDR(0, 0, 0x62),
    I2C_BUS_DAC80501       = MAKE_LSOP_I2C_ADDR(0, 0, I2C_DEV_DAC80501M_A0_SCL),

    I2C_BUS_FRONTEND       = MAKE_LSOP_I2C_ADDR(0, 1, 0),
};

static int _dac_mcp4725_set_vout(lldev_t dev, subdev_t subdev, unsigned val)
{
    uint8_t data[2] = { ((val >> 12) & 0x0f), ((val >> 4) & 0xff) };
    return lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        USDR_LSOP_I2C_DEV, I2C_BUS_DAC_MCP4725,
                                        0, NULL, 2, data);
}

static int _dac_mcp4725_get_vout(lldev_t dev, subdev_t subdev, uint32_t *val)
{
    return lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        USDR_LSOP_I2C_DEV, I2C_BUS_DAC_MCP4725,
                                        4, val, 0, NULL);
}


enum dac_x0501_regs {
    NOOP = 0,    //No operation NOOP Register
    DEVID = 1,   //Device identification DEVID Register
    SYNC = 2,    //Synchronization SYNC Register
    CONFIG = 3,  //Configuration CONFIG Register
    GAIN = 4,    //Gain GAIN Register
    TRIGGER = 5, //Trigger TRIGGER Register
    STATUS = 7,  //Status STATUS Register
    DAC = 8,     //Digital-to-analog converter
};

static int _dac_x0501_set_reg(lldev_t dev, subdev_t subdev, uint8_t reg, uint16_t val)
{
    uint8_t data[3] = { reg, (val >> 8), (val) };
    return lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        USDR_LSOP_I2C_DEV, I2C_BUS_DAC80501,
                                        0, NULL, 3, data);
}

static int _dac_x0501_get_reg(lldev_t dev, subdev_t subdev, uint8_t reg, uint16_t* oval)
{
    return lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        USDR_LSOP_I2C_DEV, I2C_BUS_DAC80501,
                                        2, oval, 1, &reg);
}

static int dev_gpo_set(lldev_t dev, unsigned bank, unsigned data)
{
    return lowlevel_reg_wr32(dev, 0, 0, ((bank & 0x7f) << 24) | (data & 0xff));
}

static int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, 0, 16 + (bank / 4), data);
}

static int _xsdr_init_revx(xsdr_dev_t *d, unsigned hwid);
static int _xsdr_init_revo(xsdr_dev_t *d);

static int _xsdr_checkpwr(xsdr_dev_t *d)
{
    if (!d->pwr_en) {
       return xsdr_pwren(d, true);
    }
    return 0;
}

int xsdr_set_samplerate(xsdr_dev_t *d,
                        unsigned rxrate, unsigned txrate,
                        unsigned adcclk, unsigned dacclk)
{
    return xsdr_set_samplerate_ex(d, rxrate, txrate, adcclk, dacclk, 0);
}

int xsdr_configure_lml_mmcm(xsdr_dev_t *d)
{
    bool nomul = d->base.lml_mode.rxsisoddr || (d->base.rxtsp_div > 1);
    unsigned rx_mclk = d->base.cgen_clk / d->base.rxcgen_div / d->base.lml_mode.rxdiv;
    unsigned io_clk  = (nomul) ? rx_mclk : rx_mclk * 2;
    unsigned vco_div_io = (MMCM_VCO_MAX  + io_clk - 1) / io_clk;
    unsigned rb;
    int res = 0;
    struct mmcm_config_raw cfg_raw;
    memset(&cfg_raw, 0, sizeof(cfg_raw));

    if (vco_div_io > 63)
        vco_div_io = 63;

    cfg_raw.type = MT_7SERIES_MMCM;
    cfg_raw.ports[CLKOUT_PORT_0].period_l = (vco_div_io + 1) / 2;
    cfg_raw.ports[CLKOUT_PORT_0].period_h = vco_div_io / 2;
    cfg_raw.ports[CLKOUT_PORT_1].period_l = (vco_div_io + 1) / 2;
    cfg_raw.ports[CLKOUT_PORT_1].period_h = vco_div_io / 2;

    cfg_raw.ports[CLKOUT_PORT_2].period_l = vco_div_io;
    cfg_raw.ports[CLKOUT_PORT_2].period_h = vco_div_io;
    cfg_raw.ports[CLKOUT_PORT_3].period_l = vco_div_io;
    cfg_raw.ports[CLKOUT_PORT_3].period_h = vco_div_io;

    cfg_raw.ports[CLKOUT_PORT_4].period_l = vco_div_io;
    cfg_raw.ports[CLKOUT_PORT_4].period_h = vco_div_io;
    cfg_raw.ports[CLKOUT_PORT_5].period_l = vco_div_io;
    cfg_raw.ports[CLKOUT_PORT_5].period_h = vco_div_io;
    cfg_raw.ports[CLKOUT_PORT_6].period_l = vco_div_io;
    cfg_raw.ports[CLKOUT_PORT_6].period_h = vco_div_io;

    cfg_raw.ports[CLKOUT_PORT_0].delay = 1;

    if (nomul) {
        cfg_raw.ports[CLKOUT_PORT_FB].period_l =(vco_div_io + 1) / 2;
        cfg_raw.ports[CLKOUT_PORT_FB].period_h = vco_div_io / 2;
    } else {
        cfg_raw.ports[CLKOUT_PORT_FB].period_l = vco_div_io;
        cfg_raw.ports[CLKOUT_PORT_FB].period_h = vco_div_io;
    }
    USDR_LOG("XDEV", USDR_LOG_ERROR, "MMCM set to MCLK = %.3f IOCLK = %.3f Mhz IODIV = %d\n",
             rx_mclk / (1.0e6), io_clk / (1.0e6), vco_div_io);

    res = (res) ? res : lowlevel_reg_wr32(d->base.lmsstate.dev, d->base.lmsstate.subdev, REG_CFG_PHY_0,
                                          0x80000000 | 0x10000 | 0xF);
    usleep(100);
    res = (res) ? res : lowlevel_reg_wr32(d->base.lmsstate.dev, d->base.lmsstate.subdev, REG_CFG_PHY_0,
                                          0x80000000 | 0x10000 | 0xD);
    usleep(100);
    res = (res) ? res : lowlevel_reg_wr32(d->base.lmsstate.dev, d->base.lmsstate.subdev, REG_CFG_PHY_0,
                                          0x80000000 | 0x10000 | 0x1);

    if (res)
        return res;

    usleep(1000);
    res = mmcm_init_raw(d->base.lmsstate.dev, d->base.lmsstate.subdev, 0, &cfg_raw);
    if (res)
        return res;

    res = (res) ? res : lowlevel_reg_wr32(d->base.lmsstate.dev, d->base.lmsstate.subdev, REG_CFG_PHY_0,
                                          0x80000000 | 0x10000 | 0x0);

    usleep(10000);
    res = (res) ? res : lowlevel_reg_wr32(d->base.lmsstate.dev, d->base.lmsstate.subdev, REG_CFG_PHY_0,
                                           0x01000000);
    if (res)
        return res;

    for (unsigned k = 0; k < 10; k++) {
        // Wait for lock
        res = lowlevel_reg_rd32(d->base.lmsstate.dev, d->base.lmsstate.subdev,
                                REG_CFG_PHY_0, &rb);


        USDR_LOG("XDEV", USDR_LOG_INFO, "MMCM FLAGS:%08x\n", rb);
        if (rb & (1 << 16))
            return 0;

        usleep(5000);
    }
    return 0;

    return -ERANGE;
#if 0
    res = lowlevel_reg_wr32(d->base.base.dev, d->base.subdev, REG_CFG_PHY_0,
                      0x80000000 | 0x10000 | 0x1);
    if (res)
        return res;
    res = lowlevel_reg_wr32(d->base.base.dev, d->base.subdev, REG_CFG_PHY_0,
                      0x80000000 | 0x10000 | 0x0);
    if (res)
        return res;



    ////////////////////

    res = (res) ? res : lowlevel_reg_wr32(d->base.base.dev, d->base.subdev, REG_CFG_PHY_0,
                                          0x80000000 | 0x10000 | 0x1);
    usleep(1000);
    res = (res) ? res : lowlevel_reg_wr32(d->base.base.dev, d->base.subdev, REG_CFG_PHY_0,
                                          0x80000000 | 0x10000 | 0x0);

    res = (res) ? res : lowlevel_reg_wr32(d->base.base.dev, d->base.subdev, REG_CFG_PHY_0,
                                           0x01000000);
    for (unsigned k = 0; k < 10; k++) {
        // Wait for lock
        res = lowlevel_reg_rd32(d->base.base.dev, d->base.subdev,
                                REG_CFG_PHY_0, &rb);
    }
#endif
    return res;
}

int xsdr_phy_tune(xsdr_dev_t *d, unsigned val)
{
    int res = mmcm_set_phdigdelay_raw(d->base.lmsstate.dev, d->base.lmsstate.subdev, 0, CLKOUT_PORT_0, val);
    return res;
}

int xsdr_hwchans_cnt(xsdr_dev_t *d, bool rx, unsigned chans)
{
    unsigned old_chans_rx = d->hwchans_rx;
    unsigned old_chans_tx = d->hwchans_tx;

    if (rx) {
        d->hwchans_rx = chans;
    } else {
        d->hwchans_tx = chans;
    }

    if ((old_chans_rx != d->hwchans_rx) || (old_chans_tx != d->hwchans_tx)) {
        return xsdr_rfic_streaming_xflags(d, 0, 0);
    }
    return 0;
}

int xsdr_set_samplerate_ex(xsdr_dev_t *d,
                           unsigned rxrate, unsigned txrate,
                           unsigned adcclk, unsigned dacclk,
                           unsigned flags)
{
    const unsigned l1_pid = (d->hwid) & 0x7;
    const unsigned l2_pid = (d->hwid >> 4) & 0x7;
    const bool lml1_rx_valid = (l1_pid == 3 || l1_pid == 4 || l1_pid == 5 || l1_pid == 6)     || l1_pid == 7;
    const bool lml2_rx_valid = (l2_pid == 3 || l2_pid == 4 || l2_pid == 5 || l2_pid == 6)     || l2_pid == 7;
    const unsigned rx_port = (lml2_rx_valid && !lml1_rx_valid) ? 2 : 1;
    const unsigned rx_port_1 = (rx_port == 1);
    lldev_t dev = d->base.lmsstate.dev;
    subdev_t subdev = d->base.lmsstate.subdev;
    unsigned sisosdrflag;
    int res;

    res = _xsdr_checkpwr(d);
    if (res)
        return res;

    if (rxrate == 0 && txrate == 0) {
        rxrate = 1e6;
    }

    // TODO: Check if MMCM is present
    bool mmcmrx = false;
    unsigned m_flags = flags | ((d->siso_sdr_active_rx && d->hwchans_rx == 1) ? XSDR_LML_SISO_DDR_RX : 0)
                       | ((d->siso_sdr_active_tx && d->hwchans_tx == 1) ? XSDR_LML_SISO_DDR_TX : 0);

    res = lms7002m_samplerate(&d->base, rxrate, txrate, adcclk, dacclk, m_flags, rx_port_1);
    if (res)
        return res;

    d->s_rxrate = rxrate;
    d->s_txrate = txrate;
    d->s_adcclk = adcclk;
    d->s_dacclk = dacclk;
    d->s_flags = m_flags;

    if (d->afe_active == false) {
        // Need AFE for reference cloking
        lms7002m_afe_enable(&d->base.lmsstate, true, true, true, true); // TODO: Check if rx & tx, a & b is required!

        // wait for clock to stabilize
        usleep(10000);

        d->afe_active = true;
    }

    if (mmcmrx) {
        res = xsdr_configure_lml_mmcm(d);
        if (res)
            return res;
    }

    sisosdrflag = d->base.lml_mode.rxsisoddr ? 8 : 0;
    res = lowlevel_reg_wr32(dev, subdev, REG_CFG_PHY_0, 0x80000007 | sisosdrflag);
    usleep(100);
    res = lowlevel_reg_wr32(dev, subdev, REG_CFG_PHY_0, 0x80000000 | sisosdrflag);


    if (mmcmrx) {
        // Configure PHY (reset)
        // TODO phase search
        for (unsigned h = 0; h < 16; h++) {
            res = lowlevel_reg_wr32(dev, subdev, REG_CFG_PHY_0, 0x80000007 | sisosdrflag);
            usleep(100);
            res = lowlevel_reg_wr32(dev, subdev, REG_CFG_PHY_0, 0x80000000 | sisosdrflag);


            USDR_LOG("XDEV", USDR_LOG_INFO, "PHASE=%d\n", h);
            res = lowlevel_reg_wr32(dev, subdev, REG_CFG_PHY_0, 0x00000000);
            unsigned tmp; //, tmp2;
            for (unsigned k = 0; k < 100; k++) {
                res = lowlevel_reg_rd32(dev, subdev, REG_CFG_PHY_0, &tmp);
            }

            mmcm_set_digdelay_raw(d->base.lmsstate.dev, d->base.lmsstate.subdev, 0, CLKOUT_PORT_0, h);
        }
    }

    // Switch to clock meas
    res = lowlevel_reg_wr32(dev, subdev, REG_CFG_PHY_0, 0x02000000);

    // uint32_t m;
    // res = lowlevel_reg_rd32(dev, subdev, REG_CFG_PHY_0, &m);
    // USDR_LOG("XDEV", USDR_LOG_INFO, "MEAS=%08x\n", m);

    return res;

}


int xsdr_clk_debug_info(xsdr_dev_t *d)
{
    lldev_t dev = d->base.lmsstate.dev;
    // subdev_t subdev = d->base.lmsstate.subdev;
    unsigned crx, ctx, caux;
    int res = 0;

    res = res ? res : dev_gpi_get32(dev, IGPI_MEAS_RXCLK, &crx);
    res = res ? res : dev_gpi_get32(dev, IGPI_MEAS_TXCLK, &ctx);
    res = res ? res : dev_gpi_get32(dev, IGPI_CLK1PPS, &caux);
    if (res)
        return res;

    USDR_LOG("XDEV", USDR_LOG_WARNING, "PHY - RX %08x (%d) / TX %08x (%d) / AUX %08x (%d)\n",
             crx, crx & 0xfffffff,
             ctx, ctx & 0xfffffff,
             caux, caux & 0xfffffff);
    return res;
}

int xsdr_set_rx_port_switch(xsdr_dev_t *d, unsigned path)
{
    USDR_LOG("XDEV", USDR_LOG_INFO, "RXSW:%d\n", path);
    return dev_gpo_set(d->base.lmsstate.dev, IGPO_RXSW, path);
}

int xsdr_set_tx_port_switch(xsdr_dev_t *d, unsigned path)
{
    USDR_LOG("XDEV", USDR_LOG_INFO, "TXSW:%d\n", path);
    return dev_gpo_set(d->base.lmsstate.dev, IGPO_TXSW, path);
}

static
int _xsdr_antenna_port_switch(lms7002_dev_t *d, int dir, unsigned path)
{
    xsdr_dev_t *dev = container_of(d, xsdr_dev_t, base);

    if (dir == RFIC_LMS7_TX) {
        return xsdr_set_tx_port_switch(dev, path);
    } else if (dir == RFIC_LMS7_RX) {
        return xsdr_set_rx_port_switch(dev, path);
    }

    return -EINVAL;
}



//////////////////////////////////////////////////////////////////////////////

static bool _xsdr_run_params_stream_is_swap(unsigned chs, unsigned flags)
{
    return (chs == LMS7_CH_AB && (flags & RFIC_SWAP_AB)) ||
            chs == LMS7_CH_A;
}
static bool _xsdr_run_params_stream_is_mimo(unsigned chs, unsigned flags)
{
    return (chs == LMS7_CH_AB && !(flags & RFIC_SISO_MODE));
}

static
const lms7002m_lml_map_t lms7nfe_get_lml_portcfg(bool rx, unsigned chs, unsigned flags, bool no_siso_map)
{
    static const lms7002m_lml_map_t diqarray_rx[] = {
        // MIMO modes
        {{ LML_AI, LML_AQ, LML_BI, LML_BQ }},
        {{ LML_AQ, LML_AI, LML_BQ, LML_BI }},
        {{ LML_BI, LML_BQ, LML_AI, LML_AQ }},
        {{ LML_BQ, LML_BI, LML_AQ, LML_AI }},

        // SISO modes
        {{ LML_AI, LML_AQ, LML_AI, LML_AQ }},
        {{ LML_AQ, LML_AI, LML_AQ, LML_AI }},
        {{ LML_BI, LML_BQ, LML_BI, LML_BQ }},
        {{ LML_BQ, LML_BI, LML_BQ, LML_BI }},
    };
#if 0
    static const lms7002m_lml_map_t diqarray_tx[] = {
        // MIMO modes
        {{ LML_BI, LML_AI, LML_BQ, LML_AQ }},
        {{ LML_BQ, LML_AQ, LML_BI, LML_AI }},
        {{ LML_AI, LML_BI, LML_AQ, LML_BQ }},
        {{ LML_AQ, LML_BQ, LML_AI, LML_BI }},

        {{ LML_BI, LML_AI, LML_BQ, LML_AQ }},
        {{ LML_BQ, LML_AQ, LML_BI, LML_AI }},
        {{ LML_AI, LML_BI, LML_AQ, LML_BQ }},
        {{ LML_AQ, LML_BQ, LML_AI, LML_BI }},
    // SISO modes
        {{ LML_AI, LML_AI, LML_AQ, LML_AQ }},
        {{ LML_AQ, LML_AQ, LML_AI, LML_AI }},
        {{ LML_BI, LML_BI, LML_BQ, LML_BQ }},
        {{ LML_BQ, LML_BQ, LML_BI, LML_BI }},
        // MIMO test modes (swap IQ_B)
        {{ LML_BQ, LML_AI, LML_BI, LML_AQ }},
        {{ LML_BI, LML_AQ, LML_BQ, LML_AI }},
        {{ LML_AQ, LML_BI, LML_AI, LML_BQ }},
        {{ LML_AI, LML_BQ, LML_AQ, LML_BI }},
        // MIMO test modes (swap IQ_A)
        {{ LML_BI, LML_AQ, LML_BQ, LML_AI }},
        {{ LML_BQ, LML_AI, LML_BI, LML_AQ }},
        {{ LML_AI, LML_BQ, LML_AQ, LML_BI }},
        {{ LML_AQ, LML_BI, LML_AI, LML_BQ }},
    };

    const lms7002m_lml_map_t *diqarray = (rx) ? diqarray_rx : diqarray_tx;
#endif
    const lms7002m_lml_map_t *diqarray = diqarray_rx;
    unsigned diqidx = 0;
    if (flags & RFIC_SWAP_IQ)
        diqidx |= 1;

    if (_xsdr_run_params_stream_is_swap(chs, flags))
        diqidx |= 2;

    if (!no_siso_map && !_xsdr_run_params_stream_is_mimo(chs, flags))
        diqidx |= 4;

    USDR_LOG("XDEV", USDR_LOG_WARNING, "%s_diqidx=%d\n", rx ? "rx" : "tx", diqidx);
    assert(diqidx < 8);
    return diqarray[diqidx];
}

#if 0   //unused, DO NOT DELETE
static
const lms7002m_lml_map_t lms7nfe_get_lml_portcfg_o(unsigned chs, unsigned flags, bool no_siso_map)
{
#if 0
    static const struct lml_map diqarray[] = {
        // MIMO modes
        {{ LML_AI, LML_BI, LML_AQ, LML_BQ }},
        {{ LML_AQ, LML_BQ, LML_AI, LML_BI }},
        {{ LML_BI, LML_AI, LML_BQ, LML_AQ }},
        {{ LML_BQ, LML_AQ, LML_BI, LML_AI }},

        // SISO modes
        {{ LML_AI, LML_AQ, LML_AI, LML_AQ }},
        {{ LML_AQ, LML_AI, LML_AQ, LML_AI }},
        {{ LML_BI, LML_BQ, LML_BI, LML_BQ }},
        {{ LML_BQ, LML_BI, LML_BQ, LML_BI }},
    };

#endif
    static const lms7002m_lml_map_t diqarray[16] = {
        // MIMO modes
        {{ LML_BI, LML_AI, LML_BQ, LML_AQ }},
        {{ LML_BQ, LML_AQ, LML_BI, LML_AI }},
        {{ LML_AI, LML_BI, LML_AQ, LML_BQ }},
        {{ LML_AQ, LML_BQ, LML_AI, LML_BI }},
        // SISO modes
        {{ LML_AI, LML_AI, LML_AQ, LML_AQ }},
        {{ LML_AQ, LML_AQ, LML_AI, LML_AI }},
        {{ LML_BI, LML_BI, LML_BQ, LML_BQ }},
        {{ LML_BQ, LML_BQ, LML_BI, LML_BI }},
        // MIMO test modes (swap IQ_B)
        {{ LML_BQ, LML_AI, LML_BI, LML_AQ }},
        {{ LML_BI, LML_AQ, LML_BQ, LML_AI }},
        {{ LML_AQ, LML_BI, LML_AI, LML_BQ }},
        {{ LML_AI, LML_BQ, LML_AQ, LML_BI }},
        // MIMO test modes (swap IQ_A)
        {{ LML_BI, LML_AQ, LML_BQ, LML_AI }},
        {{ LML_BQ, LML_AI, LML_BI, LML_AQ }},
        {{ LML_AI, LML_BQ, LML_AQ, LML_BI }},
        {{ LML_AQ, LML_BI, LML_AI, LML_BQ }},
    };

    unsigned diqidx = 0;
    if (flags & RFIC_SWAP_IQ)
        diqidx |= 1;

    if (_xsdr_run_params_stream_is_swap(chs, flags))
        diqidx |= 2;

    if (!no_siso_map && !_xsdr_run_params_stream_is_mimo(chs, flags))
        diqidx |= 4;
//    else if (flags & RFIC_SWAP_IQB)
//        diqidx |= 8;
//    else if (flags & RFIC_SWAP_IQA)
//        diqidx |= 12;

    USDR_LOG("XDEV", USDR_LOG_WARNING, "diqidx=%d\n", diqidx);
    assert(diqidx < (sizeof(diqarray)/sizeof(diqarray[0])));
    return diqarray[diqidx];
}
#endif

int xsdr_rfic_streaming_xflags(xsdr_dev_t *d,
                               unsigned xor_rx_flags,
                               unsigned xor_tx_flags)
{
    int res;
    unsigned rxf = (d->hwchans_rx == 1) ? RFIC_SISO_MODE : 0;
    unsigned txf = (d->hwchans_tx == 1) ? RFIC_SISO_MODE : 0;

    d->dsp_rxcfg = xor_rx_flags;
    d->base.map_rx = lms7nfe_get_lml_portcfg(true, d->base.lml_rx_chs, rxf | (d->base.lml_rx_flags ^ d->dsp_rxcfg), d->base.rx_no_siso_map);
    d->base.map_tx = lms7nfe_get_lml_portcfg(false, d->base.lml_tx_chs, txf | (d->base.lml_tx_flags ^ xor_tx_flags), d->base.tx_no_siso_map);
    res = lms7002m_limelight_map(&d->base.lmsstate,
                                 d->base.lml_mode.rx_port == 1 ? d->base.map_rx : d->base.map_tx,
                                 d->base.lml_mode.rx_port == 1 ? d->base.map_tx : d->base.map_rx);
    if (res)
        return res;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // res = lms7002m_limelight_configure(&d->base.lmsstate, d->base.lml_mode);
    // if (res)
    //     return res;

    return 0;
}

int xsdr_rfic_streaming_up(xsdr_dev_t *d, unsigned dir,
                           unsigned rx_chs, unsigned rx_flags,
                           unsigned tx_chs, unsigned tx_flags)
{
    return lms7002m_streaming_up(&d->base, dir, (lms7002m_mac_mode_t)rx_chs, rx_flags, (lms7002m_mac_mode_t)tx_chs, tx_flags);
}


int xsdr_rfic_streaming_down(xsdr_dev_t *d, unsigned dir)
{
    d->afe_active = false;

    return lms7002m_streaming_down(&d->base, dir);
}

int xsdr_rfic_bb_set_freq(xsdr_dev_t *d,
                        unsigned channel,
                        bool dir_tx,
                        int64_t freq)
{
   return lms7002m_bb_set_freq(&d->base, channel, dir_tx, freq);
}

int xsdr_rfic_bb_set_badwidth(xsdr_dev_t *d,
                           unsigned channel,
                           bool dir_tx,
                           unsigned bw,
                           unsigned* actualbw)
{
    return lms7002m_bb_set_badwidth(&d->base, channel, dir_tx, bw, actualbw);
}

int xsdr_rfic_set_gain(xsdr_dev_t *d,
                     unsigned channel,
                     unsigned gain_type,
                     int gain,
                     double *actualgain)
{
    return lms7002m_set_gain(&d->base, channel, gain_type, gain, actualgain);
}

int xsdr_rfic_fe_set_freq(xsdr_dev_t *d,
                       unsigned channel,
                       unsigned type,
                       double freq,
                       double *actualfreq)
{
    if (d->ssdr && freq > 3.7e9) {
        int res = 0;
        d->lms7_lob = 1.01e9;

        res = res ? res : dev_gpo_set(d->base.lmsstate.dev, IGPO_LMS8_CTRL, 0x81);
        res = res ? res : lms8001_tune(&d->lms8, d->base.fref, freq - d->lms7_lob);

        dev_gpo_set(d->base.lmsstate.dev, IGPO_LMS8_CTRL, 0x80);
        if (res)
            return res;

        USDR_LOG("XDEV", USDR_LOG_INFO, "Setting FREQ  %.3f Mhz, LNB %.3f Mhz\n", freq / 1.0e6, d->lms7_lob / 1.0e6);
        freq = d->lms7_lob;
    } else {
        d->lms7_lob = 0;
    }

    return lms7002m_fe_set_freq(&d->base, channel, type, freq, actualfreq);
}


int xsdr_rfic_rfe_set_path(xsdr_dev_t *d,
                           unsigned path)
{
    return lms7002m_rfe_set_path(&d->base, (rfic_lms7_rf_path_t)path);
}

int xsdr_rfic_tfe_set_path(xsdr_dev_t *d,
                           unsigned path)
{
    return lms7002m_tfe_set_path(&d->base, (rfic_lms7_rf_path_t)path);
}

int xsdr_rfic_fe_set_lna(xsdr_dev_t *d,
                         unsigned channel,
                         unsigned lna)
{
    return lms7002m_fe_set_lna(&d->base, channel, lna);
}

int xsdr_tx_antennat_port_cfg(xsdr_dev_t *d, unsigned mask)
{
    lldev_t dev = d->base.lmsstate.dev;
    unsigned subdev = 0;
    int res = 0;

    d->dsp_txcfg = mask;
    // 2 - mute_a
    // 1 - mute_b

    if (mask & 2) {
        res = res ? res : lms7002m_mac_set(&d->base.lmsstate, LMS7_CH_A);
        res = res ? res : lms7002m_trf_path(&d->base.lmsstate, TRF_MUTE, TRF_MODE_NORMAL);
    }

    if (mask & 1) {
        res = res ? res : lms7002m_mac_set(&d->base.lmsstate, LMS7_CH_B);
        res = res ? res : lms7002m_trf_path(&d->base.lmsstate, TRF_MUTE, TRF_MODE_NORMAL);
    }

    res = res ? res : lowlevel_reg_wr32(dev, subdev, M2PCI_REG_WR_TXDMA_COMB, (1 << 11) | ((mask & 7) << 8) | 1);
    return res;
}


int xsdr_ctor(lldev_t dev, xsdr_dev_t *d)
{
    memset(d, 0, sizeof(xsdr_dev_t));
    d->base.lmsstate.dev = dev;

    d->base.on_ant_port_sw = &_xsdr_antenna_port_switch;
    d->base.on_get_lml_portcfg = &lms7nfe_get_lml_portcfg;

    return 0;
}

int _xsdr_init_revx(xsdr_dev_t *d, unsigned hwid)
{
    lldev_t dev = d->base.lmsstate.dev;
    unsigned subdev = 0;
    int res;
    bool pg = false;

    enum tx_switch_cfg {
        TX_SW_NORMAL = 0,
        TX_SW_HARD_W = 1,
        TX_SW_HARD_H = 2,
    };
    enum tx_switch_cfg txcfg =
            getenv("USDR_TX_W") ? TX_SW_HARD_W :
            getenv("USDR_TX_H") ? TX_SW_HARD_H : TX_SW_NORMAL;

    // TODO Read configuration from FLASH

    // Antenna band switch configuration
    d->base.cfg_auto_rx[0].stop_freq = 2200e6;
    d->base.cfg_auto_rx[0].band = RFE_LNAW;
    d->base.cfg_auto_rx[0].sw = 0;
    d->base.cfg_auto_rx[0].swlb = 1;
    strncpy(d->base.cfg_auto_rx[0].name0, "LNAW", sizeof(d->base.cfg_auto_rx[0].name0));
    strncpy(d->base.cfg_auto_rx[0].name1, "W", sizeof(d->base.cfg_auto_rx[0].name1));
    d->base.cfg_auto_rx[1].stop_freq = 4000e6;
    d->base.cfg_auto_rx[1].band = RFE_LNAH;
    d->base.cfg_auto_rx[1].sw = 1;
    d->base.cfg_auto_rx[1].swlb = 0;
    strncpy(d->base.cfg_auto_rx[1].name0, "LNAH", sizeof(d->base.cfg_auto_rx[1].name0));
    strncpy(d->base.cfg_auto_rx[1].name1, "H", sizeof(d->base.cfg_auto_rx[1].name1));
    d->base.cfg_auto_rx[2].stop_freq = 4000e6;
    d->base.cfg_auto_rx[2].band = RFE_LNAL;
    d->base.cfg_auto_rx[2].sw = 1;
    d->base.cfg_auto_rx[2].swlb = 0;
    strncpy(d->base.cfg_auto_rx[2].name0, "LNAL", sizeof(d->base.cfg_auto_rx[2].name0));
    strncpy(d->base.cfg_auto_rx[2].name1, "EXT", sizeof(d->base.cfg_auto_rx[2].name1));

    d->base.cfg_auto_tx[0].stop_freq = (txcfg == TX_SW_HARD_H) ? 0 :
                                  (txcfg == TX_SW_HARD_W) ? 4000e6 : 2200e6;
    d->base.cfg_auto_tx[0].band = 1;
    d->base.cfg_auto_tx[0].sw = 1;
    d->base.cfg_auto_tx[0].swlb = 0;
    strncpy(d->base.cfg_auto_tx[0].name0, "W", sizeof(d->base.cfg_auto_tx[0].name0));
    strncpy(d->base.cfg_auto_tx[0].name1, "B1", sizeof(d->base.cfg_auto_tx[0].name1));
    d->base.cfg_auto_tx[1].stop_freq = 4000e6;
    d->base.cfg_auto_tx[1].band = 2;
    d->base.cfg_auto_tx[1].sw = 0;
    d->base.cfg_auto_tx[1].swlb = 1;
    strncpy(d->base.cfg_auto_tx[1].name0, "H", sizeof(d->base.cfg_auto_tx[1].name0));
    strncpy(d->base.cfg_auto_tx[1].name1, "B2", sizeof(d->base.cfg_auto_tx[1].name1));

    if (hwid == SSDR_DEV) {
        // QPC8019Q   0: RFC1 -- HF; 1: RFC2 -- LF

        d->base.cfg_auto_rx[0].stop_freq = 3000e6;
        d->base.cfg_auto_tx[0].stop_freq = 3000e6;

        d->base.cfg_auto_rx[0].sw = 1;
        d->base.cfg_auto_rx[0].swlb = 0;

        d->base.cfg_auto_rx[1].sw = 0;
        d->base.cfg_auto_rx[1].swlb = 1;
    }

    res = dev_gpo_set(dev, IGPO_LMS_PWR, 0);
    if (res)
        return res;

    uint16_t rev = 0xffff;
    res = lp8758_get_rev(dev, subdev, I2C_BUS_LP8758_FPGA, &rev);
    if (res)
        return res;

    if (rev == 0xe001) {
        d->pmic_ch145_valid = true;
    }

    USDR_LOG("XDEV", USDR_LOG_INFO, "PMIC_RFIC ver %04x (%d)\n",
             rev, d->pmic_ch145_valid);

    if (hwid == SSDR_DEV) {
        res = lp8758_vout_set(dev, subdev, I2C_BUS_LP8758_FPGA, 1, 2040);
    } else {
        // TODO check if we need this rail
        res = lp8758_vout_set(dev, subdev, I2C_BUS_LP8758_FPGA, 1, 1480);
    }

    // LMS Vcore boost to 1.25V
    res = res ? res : lp8758_vout_set(dev, subdev, I2C_BUS_LP8758_FPGA, 2, 1260);
    res = res ? res : lp8758_vout_set(dev, subdev, I2C_BUS_LP8758_FPGA, 3, 1850);

    res = res ? res : lp8758_vout_ctrl(dev, subdev, I2C_BUS_LP8758_FPGA, 0, 1, 1); //1v0 -- less affected
    res = res ? res : lp8758_vout_ctrl(dev, subdev, I2C_BUS_LP8758_FPGA, 1, 1, 1); //2v5 -- less affected
    res = res ? res : lp8758_vout_ctrl(dev, subdev, I2C_BUS_LP8758_FPGA, 2, 1, 1); //1v2
    res = res ? res : lp8758_vout_ctrl(dev, subdev, I2C_BUS_LP8758_FPGA, 3, 1, 1); //1v8

    // Wait for power to settle
    for (unsigned i = 0; !res && !pg && (i < 100); i++) {
        usleep(1000);
        res = res ? res : lp8758_check_pg(dev, subdev, I2C_BUS_LP8758_FPGA, 0xf, &pg);
    }

    if (!pg) {
        USDR_LOG("XDEV", USDR_LOG_INFO, "Couldn't set PMIC voltages!\n");
        return -EIO;
    }

    // Enable internal clocking by default
    res = res ? res : dev_gpo_set(d->base.lmsstate.dev, IGPO_CLK_CFG, 1);
    if (hwid == SSDR_DEV) {
        uint32_t chipver = ~0;

        // Check LMS8 presence
        res = res ? res : dev_gpo_set(dev, IGPO_LMS8_CTRL, 0x0);
        res = res ? res : dev_gpo_set(dev, IGPO_LDOLMS_EN, 1); // Enable LDOs
        res = res ? res : dev_gpo_set(dev, IGPO_LMS_PWR, 9);   // LMS
        usleep(100000);

        res = res ? res : lowlevel_spi_tr32(dev, d->base.lmsstate.subdev, 0, 0x002F0000, &chipver);
        USDR_LOG("XDEV", USDR_LOG_INFO, "LMS7002 version %08x\n", chipver);

        res = res ? res : dev_gpo_set(dev, IGPO_LMS8_CTRL, 0x81);
        usleep(100000);

        res = res ? res : lowlevel_spi_tr32(dev, d->base.lmsstate.subdev, 0, 0x800000ff, &chipver);
        res = res ? res : lowlevel_spi_tr32(dev, d->base.lmsstate.subdev, 0, 0x000f0000, &chipver);
        USDR_LOG("XDEV", USDR_LOG_INFO, "LMS8001 version %08x\n", chipver);

        res = res ? res : lms8001_create(dev, d->base.lmsstate.subdev, 0, &d->lms8);

        res = res ? res : dev_gpo_set(dev, IGPO_LMS8_CTRL, 0x80);
        // res = res ? res : dev_gpo_set(dev, IGPO_LDOLMS_EN, 0); // Enable LDOs
        // res = res ? res : dev_gpo_set(dev, IGPO_LMS_PWR, 0);

        if (chipver != 0x00004040) {
            USDR_LOG("XDEV", USDR_LOG_ERROR, "LMS8001 not detected!\n");
        }

    }
    return res;
}

int xsdr_trspi_lms8(xsdr_dev_t *d, uint32_t out, uint32_t* in)
{
    int res = 0;
    lldev_t dev = d->base.lmsstate.dev;

    if (!d->ssdr)
        return -EINVAL;

    res = res ? res : dev_gpo_set(dev, IGPO_LMS8_CTRL, 0x81);
    usleep(100);
    res = res ? res : lowlevel_spi_tr32(dev, d->base.lmsstate.subdev, 0, out, in);
    usleep(100);
    res = res ? res : dev_gpo_set(dev, IGPO_LMS8_CTRL, 0x80);

    return res;
}

int _xsdr_init_revo(xsdr_dev_t *d)
{
    lldev_t dev = d->base.lmsstate.dev;
    unsigned subdev = 0;
    int res = 0;
    int mid_range;
    uint16_t rev;
    bool bpg;
    uint32_t cfg;

    // Antenna band switch configuration
    d->base.cfg_auto_rx[0].stop_freq = 1100e6;
    d->base.cfg_auto_rx[0].band = RFE_LNAL;
    d->base.cfg_auto_rx[0].sw = 1;
    d->base.cfg_auto_rx[0].swlb = 3;
    strncpy(d->base.cfg_auto_rx[0].name0, "LNAL", sizeof(d->base.cfg_auto_rx[0].name0));
    strncpy(d->base.cfg_auto_rx[0].name1, "L", sizeof(d->base.cfg_auto_rx[0].name1));
    d->base.cfg_auto_rx[1].stop_freq = 2700e6;
    d->base.cfg_auto_rx[1].band = RFE_LNAW;
    d->base.cfg_auto_rx[1].sw = 0;
    d->base.cfg_auto_rx[1].swlb = 3;
    strncpy(d->base.cfg_auto_rx[1].name0, "LNAW", sizeof(d->base.cfg_auto_rx[1].name0));
    strncpy(d->base.cfg_auto_rx[1].name1, "W", sizeof(d->base.cfg_auto_rx[1].name1));
    d->base.cfg_auto_rx[2].stop_freq = 4000e6;
    d->base.cfg_auto_rx[2].band = RFE_LNAH;
    d->base.cfg_auto_rx[2].sw = 2;
    d->base.cfg_auto_rx[2].swlb = 3;
    strncpy(d->base.cfg_auto_rx[2].name0, "LNAH", sizeof(d->base.cfg_auto_rx[2].name0));
    strncpy(d->base.cfg_auto_rx[2].name1, "H", sizeof(d->base.cfg_auto_rx[2].name1));


    d->base.cfg_auto_tx[0].stop_freq = 2200e6;
    d->base.cfg_auto_tx[0].band = 2;
    d->base.cfg_auto_tx[0].sw = 0;
    d->base.cfg_auto_tx[0].swlb = 1;
    strncpy(d->base.cfg_auto_tx[0].name0, "W", sizeof(d->base.cfg_auto_tx[0].name0));
    strncpy(d->base.cfg_auto_tx[0].name1, "B2", sizeof(d->base.cfg_auto_tx[0].name1));
    d->base.cfg_auto_tx[1].stop_freq = 4000e6;
    d->base.cfg_auto_tx[1].band = 1;
    d->base.cfg_auto_tx[1].sw = 1;
    d->base.cfg_auto_tx[1].swlb = 0;
    strncpy(d->base.cfg_auto_tx[1].name0, "H", sizeof(d->base.cfg_auto_tx[1].name0));
    strncpy(d->base.cfg_auto_tx[1].name1, "B1", sizeof(d->base.cfg_auto_tx[1].name1));

    // Set external GPIOs to 3.3V
    res = res ? res : dev_gpo_set(dev, IGPO_IOVCCSEL, 1);
    // Take control of second I2C bus
    res = res ? res : dev_gpo_set(dev, IGPO_LMS_PWR, 1 << 4);

    rev = 0xffff;
    res = res ? res : lp8758_get_rev(dev, subdev, I2C_BUS_LP8758_LMSINIT, &rev);
    if (res)
        return res;

    USDR_LOG("XDEV", USDR_LOG_INFO, "PMIC_LMS7 ver %04x\n", rev);
    if (rev != 0xe001) {
        return -EIO;
    }

    // Set LMS7 voltages
    res = res ? res : lp8758_vout_set(dev, subdev, I2C_BUS_LP8758_LMSINIT, 0, 2060);
    res = res ? res : lp8758_vout_set(dev, subdev, I2C_BUS_LP8758_LMSINIT, 1, 3360);
    res = res ? res : lp8758_vout_set(dev, subdev, I2C_BUS_LP8758_LMSINIT, 2, 1760);
    res = res ? res : lp8758_vout_set(dev, subdev, I2C_BUS_LP8758_LMSINIT, 3, 1500);
    // Force-PWM mode for all LMS7 & clock
    res = res ? res : lp8758_vout_ctrl(dev, subdev, I2C_BUS_LP8758_LMSINIT, 0, 1, 1);
    res = res ? res : lp8758_vout_ctrl(dev, subdev, I2C_BUS_LP8758_LMSINIT, 1, 1, 1);
    res = res ? res : lp8758_vout_ctrl(dev, subdev, I2C_BUS_LP8758_LMSINIT, 2, 1, 1);
    res = res ? res : lp8758_vout_ctrl(dev, subdev, I2C_BUS_LP8758_LMSINIT, 3, 1, 1);

    // wait for power good on all rails
    for (unsigned i = 0; i < 100; i++) {
        bpg = false;
        res = res ? res : lp8758_check_pg(dev, subdev, I2C_BUS_LP8758_LMSINIT, 0xf, &bpg);
        if (res)
            return res;

        if (bpg)
            break;

        usleep(1000);
    }
    if (!bpg) {
        USDR_LOG("XDEV", USDR_LOG_ERROR, "PMIC_LMS7: couldn't set LMS7 volatges, giving up!\n");
        return -EIO;
    }

    // Switch second I2C to gpio control
    res = res ? res : dev_gpo_set(dev, IGPO_LMS_PWR, 0);

    rev = 0xffff;
    res = res ? res : lp8758_get_rev(dev, subdev, I2C_BUS_LP8758_FPGA, &rev);
    if (res)
        return res;

    USDR_LOG("XDEV", USDR_LOG_INFO, "PMIC_RFIC ver %04x\n", rev);
    if (rev != 0xe001) {
        return -EIO;
    }

    // Improve spour performance
    res = res ? res : lp8758_vout_ctrl(dev, subdev, I2C_BUS_LP8758_FPGA, 0, 1, 1); //1v0
    res = res ? res : lp8758_vout_ctrl(dev, subdev, I2C_BUS_LP8758_FPGA, 1, 1, 1); //vio
    res = res ? res : lp8758_vout_ctrl(dev, subdev, I2C_BUS_LP8758_FPGA, 2, 1, 1); //1v2
    res = res ? res : lp8758_vout_ctrl(dev, subdev, I2C_BUS_LP8758_FPGA, 3, 1, 1); //1v8

    uint16_t devid;
    res = res ? res : _dac_x0501_get_reg(dev, subdev, DEVID, &devid);
    if (res)
        return res;

    USDR_LOG("XDEV", USDR_LOG_INFO, "DAC_ID=%x\n", devid);
    switch (devid) {
    case 0x0194:
    case 0x0195:
    case 0x2195:
        break;
    case 0xbeef:
    case 0xffff:
        goto rev4_check;
    default:
        return -EIO;
    }

    // TODO TCXO internal / external
    res = _dac_x0501_set_reg(dev, subdev, GAIN, 0x0101);
    if (res)
        return res;

    mid_range = (((3.0f / 2) * 1.1f) * 65535) / 2.5f;
    res = _dac_x0501_set_reg(dev, subdev, DAC, mid_range);
    if (res)
        return res;

    d->dac_old_r5 = true;
    USDR_LOG("XDEV", USDR_LOG_INFO, "Detected r5\n");
    return 0;

rev4_check:
    res = _dac_mcp4725_set_vout(dev, subdev, (1.099f / 2) * 65535);
    if (res)
        return res;

    res = _dac_mcp4725_get_vout(dev, subdev, &cfg);
    if (res)
        return res;

    if (cfg == 0xdeadbeef) {
        USDR_LOG("XDEV", USDR_LOG_ERROR, "MCP Config = %08x\n", cfg);
    }

    // unsigned q, r;
    // for (q = 0; q < 64; q++) {
    //     lowlevel_reg_rd32(dev, d->base.lmsstate.subdev, q, &r);
    // }

    // for (q = 1; q < 64; q++) {
    //     if (q == 15 || q == 7 || q == 8 || q == 9)
    //         continue;

    //     // if (q == 12)
    //     //     continue;

    //     lowlevel_reg_wr32(dev, d->base.lmsstate.subdev, q, 0);
    //     lowlevel_reg_rd32(dev, d->base.lmsstate.subdev, 0, &r);
    // }

    d->dac_old_r5 = false;
    USDR_LOG("XDEV", USDR_LOG_INFO, "Detected r4\n");
    return 0;
}

static
int _xsdr_pwren_revx(xsdr_dev_t *d, bool on)
{
    int res;
    lldev_t dev = d->base.lmsstate.dev;

    USDR_LOG("XDEV", USDR_LOG_INFO, "RFIC PWR:%d\n", on);
    if (on && !d->pmic_ch145_valid)
        return -EIO;

    res = dev_gpo_set(dev, IGPO_LDOLMS_EN, on ? 1 : 0); // Enable LDOs
    if (res)
        return res;

    if (d->ssdr) {
        // Haevy load on 1.8VA
        usleep(100000);
    }

    return 0;
}

static
int _xsdr_pwren_revo(xsdr_dev_t *d, bool on)
{
    return 0;
}

int xsdr_pwren(xsdr_dev_t *d, bool on)
{
    int res;
    lldev_t dev = d->base.lmsstate.dev;

    res = dev_gpo_set(dev, IGPO_LMS_PWR, 0); //Disble, put into reset
    if (res)
        return res;
    usleep(5000);

    res = (d->new_rev) ?
                _xsdr_pwren_revx(d, on) :
                _xsdr_pwren_revo(d, on);
    if (res)
        return res;
    usleep(5000);

    res = dev_gpo_set(dev, IGPO_LMS_PWR, 1); //Enable LDO, put into reset
    if (res)
        return res;
    usleep(1000);

    res = dev_gpo_set(dev, IGPO_LMS_PWR, 9); //Enable LDO, reset release
    if (res)
        return res;
    usleep(2500);


    res = lms7002m_create(d->base.lmsstate.dev, d->base.lmsstate.subdev, SPI_LMS7,
                          (d->new_rev) ? 0 : 0x01B10D15,
                          1 || !d->ssdr,
                          &d->base.lmsstate);
    if (res)
        return res;

    d->pwr_en = on;
    return res;
}

int xsdr_init(xsdr_dev_t *d)
{
    uint32_t hwid, hwcfg_devid;
    lldev_t dev = d->base.lmsstate.dev;
    int res;

    res = dev_gpi_get32(dev, IGPI_HWID, &hwid);
    if (res)
        return res;

    hwcfg_devid = (hwid >> 16) & 0xff;
    USDR_LOG("XDEV", USDR_LOG_ERROR, "HWID %08x\n", hwid);

    d->hwid = hwid;
    d->hwchans_rx = 2; // Defaults to MIMO;
    d->hwchans_tx = 2; // Defaults to MIMO;
    d->siso_sdr_active_rx = false;
    d->siso_sdr_active_tx = false;

    res = lms7002m_init(&d->base, dev, 0, XSDR_INT_REFCLK);
    if (res)
        return res;


    switch (hwcfg_devid) {
    case XSDR_DEV: d->new_rev = true; d->ssdr = false; break;
    case XTRX_DEV: d->new_rev = false; d->ssdr = false; break;
    case SSDR_DEV: d->new_rev = true; d->ssdr = true; break;
    default:
        USDR_LOG("XDEV", USDR_LOG_ERROR, "unsupported hwcfg_devid=%02x\n", hwcfg_devid);

        if (getenv("XSDR_FORCE")) {
            d->new_rev = true;
        } else {
            return -EINVAL;
        }
    }

    res = (d->new_rev) ? _xsdr_init_revx(d, hwcfg_devid) : _xsdr_init_revo(d);
    if (res)
        return res;

    return 0;
}

int xsdr_set_extref(xsdr_dev_t *d, bool ext, uint32_t freq)
{
    bool usb = strstr(lowlevel_get_devname(d->base.lmsstate.dev), "usb") != 0;
    uint8_t clk_cfg = (usb ? 1 : 0) | (ext ? 2 : 0);

    d->base.fref = (ext) ? freq : XSDR_INT_REFCLK;

    // TODO retrigger samplerate / TX / RX

    return dev_gpo_set(d->base.lmsstate.dev, IGPO_CLK_CFG, clk_cfg);
}

int xsdr_dtor(xsdr_dev_t *d)
{
    lldev_t dev = d->base.lmsstate.dev;
    int res = 0;

    if (d->base.lmsstate.dev) {
        res = (res) ? res : xsdr_rfic_streaming_down(d, RFIC_LMS7_RX | RFIC_LMS7_TX);
        res = (res) ? res : lms7002m_destroy(&d->base.lmsstate);
    }
    res = (res) ? res : dev_gpo_set(dev, IGPO_LMS_PWR, 0);
    res = (res) ? res : dev_gpo_set(dev, IGPO_LDOLMS_EN, 0);
    res = (res) ? res : dev_gpo_set(dev, IGPO_LED, 0);

    if (d->ssdr) {
        res = res ? res : lp8758_vout_set(dev, d->base.lmsstate.subdev, I2C_BUS_LP8758_FPGA, 1, 900);
        res = res ? res : lp8758_vout_ctrl(dev, d->base.lmsstate.subdev, I2C_BUS_LP8758_FPGA, 1, 0, 1);
    }

    USDR_LOG("XDEV", USDR_LOG_INFO, "destroyed\n");
    return res;
}

int xsdr_prepare(xsdr_dev_t *d, bool rxen, bool txen)
{
    lldev_t dev = d->base.lmsstate.dev;
    int res = 0;

    if (d->base.cgen_clk == 0) {
        const unsigned default_rate = 1000000;

        USDR_LOG("XDEV", USDR_LOG_WARNING, "clock rate isn't set, defaulting to %d!\n", default_rate);
        res = xsdr_set_samplerate_ex(d,
                                     rxen ? default_rate : 0,
                                     txen ? default_rate : 0,
                                     0, 0, XSDR_SR_MAXCONVRATE | XSDR_SR_EXTENDED_CGEN);
    }

    if (rxen) {
        res = (res) ? res : dev_gpo_set(dev, IGPO_DSP_RST, 1);
        res = (res) ? res : dev_gpo_set(dev, IGPO_DSP_RST, 0);
    }
    res = (res) ? res : dev_gpo_set(dev, IGPO_LMS_PWR, IGPO_LMS_PWR_LDOEN | IGPO_LMS_PWR_NRESET |
                                    (rxen ? IGPO_LMS_PWR_RXEN : 0) |
                                    (txen ? IGPO_LMS_PWR_TXEN : 0));
    res = (res) ? res : dev_gpo_set(dev, IGPO_LED, 1);
    if (res) {
        return res;
    }

    d->base.rx_run[0] = rxen;
    d->base.rx_run[1] = rxen;
    d->base.tx_run[0] = txen;
    d->base.tx_run[1] = txen;

    res = xsdr_rfic_streaming_up(d,
                                 (rxen ? RFIC_LMS7_RX : 0) | (txen ? RFIC_LMS7_TX : 0),
                                 LMS7_CH_AB, 0,
                                 LMS7_CH_AB, 0);

    if (txen) {
        // TODO: Add proper delay calibration
        const unsigned coeff = 0x30;
        res = (res) ? res : dev_gpo_set(d->base.lmsstate.dev, IGPO_PHYCAL, coeff | 1);
        res = (res) ? res : dev_gpo_set(d->base.lmsstate.dev, IGPO_PHYCAL, coeff | 0);
        if (res) {
            return res;
        }
    }

    lms7002m_limelight_reset(&d->base.lmsstate);

    return res;
}



int xsdr_rfe_pwrdc_get(xsdr_dev_t *d, int *meas1000db)
{
    int32_t val[2];
    int res = lowlevel_reg_rdndw(d->base.lmsstate.dev, 0, M2PCI_REG_RD_AVGIDC, (uint32_t*)&val[0], 2);
    if (res)
        return res;

    int64_t i = val[0];
    int64_t q = val[1];
    uint64_t pwr = i * i + q * q;
    if (pwr == 0)
        return -EAGAIN;

    *meas1000db = (1000 * 10 * log10(pwr) - 186639);
    return 0;
}
// Calibration

int xsdrcal_set_nco_offset(void* param, int channel, int32_t freqoffset)
{
    xsdr_dev_t *d = (xsdr_dev_t *)param;
    //d->rxdsp_freq_offset = freqoffset;
    return sfe_rf4_nco_freq(d->base.lmsstate.dev, 0, CSR_RFE4_BASE, freqoffset);
}

int xsdrcal_do_meas_nco_avg(void* param, int channel, unsigned logduration, int* func)
{
    xsdr_dev_t *d = (xsdr_dev_t *)param;
    int res;
    unsigned idx = 0;
    int meas1000db;
    int accum = 0;
    int aidx = 1; //4;

    // 64 - 8M
    for (unsigned g = 16; g < 24; g++, idx++) {
        if (logduration <= (1 << g))
            break;

        // Do accumulation of power vs I/Q, might lead to more reliable results,
        // aidx <<= idx;
        // idx = 0;
    }

    for (unsigned k = 0; k < aidx; k++) {
        res = sfe_rf4_nco_enable(d->base.lmsstate.dev, 0, CSR_RFE4_BASE, func ? true : false, idx);
        if (res)
            return res;

        if (!func)
            return 0;

        for (unsigned k = 0; k < 8000; k++) {
            res = xsdr_rfe_pwrdc_get(d, &meas1000db);
            if (res != -EAGAIN) {
                accum += meas1000db;
                break;
            }

            usleep(1000);
        }
    }

    *func = accum / aidx;
    return res;
}

int xsdrcal_set_tx_testsig(void* param, int channel, int32_t freqoffset, unsigned pwr)
{
    xsdr_dev_t *d = (xsdr_dev_t *)param;
    return lms7002m_set_tx_testsig(&d->base, channel, freqoffset, pwr);
}

int xsdrcal_set_corr_param(void* param, int channel, int corr_type, int value)
{
    xsdr_dev_t *d = (xsdr_dev_t *)param;
    return lms7002m_set_corr_param(&d->base, channel, corr_type, value);
}

static
int xsdrcal_init_calibrate(xsdr_dev_t *d, struct calibrate_ops* ops, unsigned channel)
{
    ops->adcrate = d->base.cgen_clk / d->base.rxcgen_div;
    ops->dacrate = d->base.cgen_clk / d->base.txcgen_div;
    ops->rxsamplerate = ops->adcrate / d->base.rxtsp_div;
    ops->txsamplerate = ops->dacrate / d->base.txtsp_div;

    ops->rxfrequency = d->base.rx_lo;
    ops->txfrequency = d->base.tx_lo;
    ops->channel = channel;
    ops->deflogdur = ops->rxsamplerate / 16;
    if (ops->deflogdur > 131072) {
        ops->deflogdur = 131072;
    }
    ops->defstop = -120000;
    ops->param = d;

    // Make very odd fraction not to fall harmonics into the same bins after nyquist
    ops->rxtxlo_frac = ((uint64_t)INT_MAX + 1) / 9.0187;
    ops->rxiqimb_frac = ((uint64_t)INT_MAX + 1) / 5.1031;
    ops->txiqimb_frac = ((uint64_t)INT_MAX + 1) / 11.1076;

    ops->txlo_iq_corr.max = 1023;
    ops->txlo_iq_corr.min = -1023;

    ops->tximb_iq_corr.max = 2047;
    ops->tximb_iq_corr.min = -2047;

    ops->tximb_ang_corr.max = 2047;
    ops->tximb_ang_corr.min = -2047;

    ops->rxlo_iq_corr.max = 63;
    ops->rxlo_iq_corr.min = -63;

    ops->rximb_iq_corr.max = 2047;
    ops->rximb_iq_corr.min = -2047;

    ops->rximb_ang_corr.max = 2047;
    ops->rximb_ang_corr.min = -2047;

    ops->set_nco_offset = &xsdrcal_set_nco_offset;
    ops->set_corr_param = &xsdrcal_set_corr_param;
    ops->do_meas_nco_avg = &xsdrcal_do_meas_nco_avg;
    ops->set_tx_testsig = &xsdrcal_set_tx_testsig;

    return 0;
}


// What affect callibrated values
//                | RX_LO |
// LNA gain       |   x   |
// TIA gain       |   x   |
// PGA gain       |   x   |
// BBF filter     |   x   |
// DAC/ADC sampl  |   x   |

//                | TX_LO |
// TBB filter     |   x   |


static int _xsdr_path_lb(xsdr_dev_t *d, unsigned rx_rfic_lna, unsigned tx_rfic_band,
                        unsigned channel, bool to_rx)
{
    int res;
    unsigned path;

    if (to_rx) {
        switch (rx_rfic_lna) {
        case RFE_LNAL: path = XSDR_RX_L_TX_B2_LB; break;
        case RFE_LNAW: path = XSDR_RX_W_TX_B1_LB; break;
        case RFE_LNAH: path = XSDR_RX_H_TX_B1_LB; break;
        default: return -EINVAL;
        }
    } else {
        switch (tx_rfic_band) {
        case TRF_B2: path = XSDR_RX_L_TX_B2_LB; break;
        case TRF_B1: path = XSDR_RX_W_TX_B1_LB; break;
        default: return -EINVAL;
        }
    }

    res = xsdr_rfic_fe_set_lna(d, channel == 0 ? LMS7_CH_A : LMS7_CH_B, path);
    if (res)
        return res;

    return 0;
}

// Modification tables
//       | RXLO | TXLO | Feedback band from | RXnco | TXgen/nco |
// RXLO  |  -   |  -   |          -         |   -   |     -     |
// RXIMB |  -   |  X   |    TX band to RX   |   X   |     X     |
// TXLO  |  X   |  -   |    RX band to TX   |   X   |     X     |
// TXIMB |  X   |  -   |    RX band to TX   |   X   |     X     |


int xsdr_calibrate(xsdr_dev_t *d, unsigned channel, unsigned param, int* sarray)
{
    int res = 0;
    struct calibrate_ops cops;
    bool norestore = (param & XSDR_DONT_SETBACK) == XSDR_DONT_SETBACK;
    bool externallb = (param & XSDR_CAL_EXT_FB) == XSDR_CAL_EXT_FB;
    uint8_t old_dsp_rxcfg = d->dsp_rxcfg;
    uint8_t old_dsp_txcfg = d->dsp_txcfg;
    uint8_t old_rx_lna = d->base.rx_rfic_path;
    uint8_t old_tx_lna = d->base.tx_rfic_path;
    uint8_t tx_loss[2] = { d->base.tx_loss[0] , d->base.tx_loss[1] };

    if (channel > 1) {
        return -EINVAL;
    }

    //int32_t old_rxdsp_freqoff = d->rxdsp_freq_offset;
    unsigned tx_lo = d->base.tx_lo;
    unsigned rx_lo = d->base.rx_lo;
    unsigned rx_rfic_lna = d->base.lmsstate.rfe[channel].path;
    unsigned tx_rfic_band = d->base.lmsstate.trf[channel].path;

    // TODO: txdsp != 0 || rxdsp != 0 makes incorrect calibration

    if (sarray) {
        memset(sarray, 0, sizeof(int) * 8);
    }

    res = (res) ? res : xsdrcal_init_calibrate(d, &cops, channel);
    res = (res) ? res : xsdr_rfic_streaming_xflags(d, channel == 1 ? RFIC_SWAP_AB : 0, 0);
    res = (res) ? res : lms7002m_mac_set(&d->base.lmsstate, channel == 0 ? LMS7_CH_A : LMS7_CH_B);
    if (res)
        return res;

    if ((param & XSDR_CAL_RXLO) && (rx_lo > 0)) {
        USDR_LOG("LMS7", USDR_LOG_INFO, "------------------ Calibration RXLO(%c) ------------------\n", 'A' + channel);

        // Do not touch anything since it may affect optimal I/Q correction values
        // Turn OFF digital RX LO cancellation in RSP
        res = (res) ? res : xsdrcal_set_nco_offset(d, channel, 0);
        //res = (res) ? res : lms7_rxtsp_dc_corr_off(&d->base.lmsstate);
        res = (res) ? res : lms7002m_rxtsp_dc_corr(&d->base.lmsstate, true, 0);
        res = (res) ? res : calibrate_rxlo(&cops);

        if (!norestore) {
            //res = (res) ? res : lms7_rxtsp_dc_corr(&d->base.lmsstate, 7);
            res = (res) ? res : lms7002m_rxtsp_dc_corr(&d->base.lmsstate, false, 7);
        }

        if (res)
            return res;

        if (sarray) {
            sarray[ 2 * 0 + 0] = cops.i;
            sarray[ 2 * 0 + 1] = cops.q;
        }
    }

    if ((param & (XSDR_CAL_TXLO | XSDR_CAL_RXIQIMB | XSDR_CAL_TXIQIMB)) == 0) {
        if (norestore)
            return 0;

        goto restore_rxcfg;
    }

    res = xsdr_tx_antennat_port_cfg(d, channel == 0 ? 1 : 2);
    res = (res) ? res : lms7002m_mac_set(&d->base.lmsstate, channel == 0 ? LMS7_CH_A : LMS7_CH_B);
    if (res)
        return res;

    if ((param & XSDR_CAL_RXIQIMB) && (rx_lo > 0)) {
        // TODO if TX was disabled enable TX
        USDR_LOG("LMS7", USDR_LOG_INFO, "------------------ Calibration RXIQIMB(%c) ------------------\n", 'A' + channel);
        if (!externallb) {
            res = (res) ? res : _xsdr_path_lb(d, rx_rfic_lna, tx_rfic_band, channel, true);
        }
        res = calibrate_rxiqimb(&cops);

        if (tx_lo) {
            res = (res) ? res : lms7002m_sxx_tune(&d->base.lmsstate,  SXX_RX, d->base.fref, tx_lo, false);
        } else {
            // Looks like TX was off, turn it off
            res = (res) ? res : lms7002m_sxx_disable(&d->base.lmsstate, SXX_RX);
            //res = (res) ? res : lms7_trf_disable(&d->lmsstate);
            //res = (res) ? res : lms7_afe_ctrl(&d->lmsstate, true, false, false, false);
        }
        if (res) {
            USDR_LOG("LMS7", USDR_LOG_WARNING, " RXIQIMB failed: res=%d\n", res);
            return res;
        }
        if (sarray) {
            sarray[ 2 * 2 + 0] = cops.i;
            sarray[ 2 * 2 + 1] = cops.q;
        }
    }

    if ((param & (XSDR_CAL_TXLO | XSDR_CAL_TXIQIMB)) && (tx_lo > 0)) {
        if (!externallb) {
            res = (res) ? res :  _xsdr_path_lb(d, rx_rfic_lna, tx_rfic_band, channel, false);
        }
        if (rx_lo == 0) {
            // No RBB[0] was set; defaulting to current rx samplerate 1000000
            ///////////////////////////////////////////////////////////////////////////////// HACK!!!!!!!!!!!!!!!!!
            res = (res) ? res : lms7002m_rbb_bandwidth(&d->base, 1000000, false);
        }
        if (res)
            return res;

        if (param & XSDR_CAL_TXLO) {
            USDR_LOG("LMS7", USDR_LOG_INFO, "------------------ Calibration TXLO(%c) ------------------\n", 'A' + channel);
            // res = (res) ? res : lms7_txtsp_dc_corr(&d->base.lmsstate, true);
            // res = (res) ? res : lms7002m_xxtsp_dc_corr(&d->base.lmsstate, LMS_TXTSP, false, 0);
            res = (res) ? res : calibrate_txlo(&cops);
            if (res) {
                USDR_LOG("LMS7", USDR_LOG_WARNING, " TXLO failed: res=%d\n", res);
                return res;
            }
            if (sarray) {
                sarray[ 2 * 1 + 0] = cops.i;
                sarray[ 2 * 1 + 1] = cops.q;
            }
        }

        if (param & XSDR_CAL_TXIQIMB) {
            USDR_LOG("LMS7", USDR_LOG_INFO, "------------------ Calibration TXIQIMB(%c) ------------------\n", 'A' + channel);
            res = (res) ? res : calibrate_txiqimb(&cops);
            if (res) {
                USDR_LOG("LMS7", USDR_LOG_WARNING, " TXIQIMB failed: res=%d\n", res);
                return res;
            }
            if (sarray) {
                sarray[ 2 * 3 + 0] = cops.i;
                sarray[ 2 * 3 + 1] = cops.q;
            }
        }

        if (rx_lo > 0) {
            res = (res) ? res : lms7002m_sxx_tune(&d->base.lmsstate, SXX_TX, d->base.fref, rx_lo, false);
        } else {
            res = (res) ? res : lms7002m_sxx_disable(&d->base.lmsstate, SXX_TX);
        }
        if (res) {
            USDR_LOG("LMS7", USDR_LOG_WARNING, "restore configuration failed: res=%d\n", res);
            return res;
        }
    }
    if (norestore)
        return 0;

    USDR_LOG("LMS7", USDR_LOG_INFO, "Calibration: restoring RXPATH=%d TXPATH=%d TXCFG=%d RXCFG=%d\n",
             old_rx_lna, old_tx_lna, old_dsp_txcfg, old_dsp_rxcfg);

    // Restore individual PAD attenuation
    if ((old_dsp_txcfg & 0x1) == 0) {
        res = (res) ? res : lms7002m_mac_set(&d->base.lmsstate, LMS7_CH_A);
        res = (res) ? res : lms7002m_trf_gain(&d->base.lmsstate, TRF_GAIN_PAD, -10 * tx_loss[0], NULL);
    }
    if ((old_dsp_txcfg & 0x2) == 0) {
        res = (res) ? res : lms7002m_mac_set(&d->base.lmsstate, LMS7_CH_B);
        res = (res) ? res : lms7002m_trf_gain(&d->base.lmsstate, TRF_GAIN_PAD, -10 * tx_loss[1], NULL);
    }
    res = (res) ? res : lms7002m_mac_set(&d->base.lmsstate, LMS7_CH_AB);
    res = (res) ? res : xsdr_rfic_rfe_set_path(d, old_rx_lna);
    res = (res) ? res : xsdr_rfic_tfe_set_path(d, old_tx_lna);
    res = (res) ? res : xsdr_tx_antennat_port_cfg(d, old_dsp_txcfg);
    res = (res) ? res : lms7002m_mac_set(&d->base.lmsstate, channel == 0 ? LMS7_CH_A : LMS7_CH_B);

restore_rxcfg:
    res = (res) ? res : xsdrcal_do_meas_nco_avg(d, channel, 0, NULL);
    res = (res) ? res : xsdr_rfic_streaming_xflags(d, old_dsp_rxcfg, 0);
    res = (res) ? res : xsdrcal_set_tx_testsig(d, channel, 0, UINT_MAX);

    return res;
}

int xsdr_gettemp(xsdr_dev_t *d, int* temp256)
{
    if (d->new_rev) {
        return tmp114_temp_get(d->base.lmsstate.dev, 0, I2C_BUS_TMP_114, temp256);
    } else {
        return tmp108_temp_get(d->base.lmsstate.dev, 0, I2C_BUS_TMP_108, temp256);
    }
}

int xsdr_trim_dac_vctcxo(xsdr_dev_t *d, uint16_t val)
{
    if (d->new_rev) {
        return -ENOTSUP;
    } else if (d->dac_old_r5) {
        return _dac_x0501_set_reg(d->base.lmsstate.dev, 0, DAC, val);
    } else {
        return _dac_mcp4725_set_vout(d->base.lmsstate.dev, 0, val);
    }
}



