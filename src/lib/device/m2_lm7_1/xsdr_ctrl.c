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

enum {
    DRP_MMCM_PORT_RX = 0,
    DRP_MMCM_PORT_TX = 1,
};

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
    USDR_LOG("XDEV", USDR_LOG_ERROR, "checkpwr: %d\n", d->pwr_en);
    if (!d->pwr_en) {
       return xsdr_pwren(d, true);
    }
    return 0;
}

int xsdr_config_rcvdly(xsdr_dev_t *d, unsigned type, unsigned val)
{
    lldev_t dev = d->base.lmsstate.dev;
    unsigned subdev = d->base.lmsstate.subdev;

    uint32_t cmd = (1 << 31) | (((type + 14) & 0x7f) << 24) | (val & 0xffff);

    return lowlevel_reg_wr32(dev, subdev, REG_CFG_PHY_0, cmd);
}

int xsdr_set_samplerate(xsdr_dev_t *d,
                        unsigned rxrate, unsigned txrate,
                        unsigned adcclk, unsigned dacclk)
{
    return xsdr_set_samplerate_ex(d, rxrate, txrate, adcclk, dacclk, 0);
}


enum {
    PHY_REG_PORT_CTRL    = 0,
    PHY_REG_MMCM_DRP     = 1,
    PHY_REG_MMCM_CTRL    = 2,
    PHY_REG_MMCM_PSINC   = 3,
    PHY_REG_DLY_VALUE    = 4,
    PHY_REG_PORT_IQSEL   = 5,
    PHY_REG_LFSR_GEN     = 6,
    PHY_REG_IQAB_GEN     = 7,
};

enum {
    PHY_REG_RXBANK_CTRL = 0,    // Control registers
    PHY_REG_RXBANK_MMCM = 1,    // MMCM registers
    PHY_REG_RXBANK_CLKMEAS = 2,
    PHY_REG_RXBANK_LFSRCHK = 3, // LFSR control
    PHY_REG_RXBANK_IQABCHK = 4, // IQAB control

    PHY_REG_RXBANK_CLKDLY = 14, // Clock delay
    PHY_REG_RXBANK_FRMDLY = 15, // Frame delay
};


int xsdr_phy_rx_reg(xsdr_dev_t *d, bool wr, uint8_t bank, uint8_t addr, uint16_t val)
{
    uint32_t reg = (wr ? 0x80000000 : 0) | (((uint32_t)bank & 0x7f) << 24) | ((uint32_t)addr << 16) | val;
    return lowlevel_reg_wr32(d->base.lmsstate.dev, d->base.lmsstate.subdev, REG_CFG_PHY_0, reg);
}

int xsdr_phy_en_lfsr_checker_mimo(xsdr_dev_t *d, bool en)
{
    int res = 0;
    if (!en) {
        res = res ? res : xsdr_phy_rx_reg(d, true, PHY_REG_RXBANK_IQABCHK, 0, 0);
        res = res ? res : xsdr_phy_rx_reg(d, true, PHY_REG_RXBANK_LFSRCHK, 0, 0x0f);
        return res;
    }

    res = res ? res : xsdr_phy_rx_reg(d, true, PHY_REG_RXBANK_IQABCHK, 0, 0);
    res = res ? res : xsdr_phy_rx_reg(d, true, PHY_REG_RXBANK_LFSRCHK, 0, 0x0f);
    res = res ? res : xsdr_phy_rx_reg(d, true, PHY_REG_RXBANK_LFSRCHK, 0, 0xf0);
    return res;
}

int xsdr_phy_en_iqab_checker_mimo(xsdr_dev_t *d, bool en)
{
    int res = 0;
    res = res ? res : xsdr_phy_rx_reg(d, true, PHY_REG_RXBANK_LFSRCHK, 0, 0x0f);
    res = res ? res : xsdr_phy_rx_reg(d, true, PHY_REG_RXBANK_IQABCHK, 0, 0);
    usleep(1);
    res = res ? res : xsdr_phy_rx_reg(d, true, PHY_REG_RXBANK_IQABCHK, 0, en ? 1 : 0);
    return res;
}

int xsdr_phy_lfsr_mimo_state_s(xsdr_dev_t *d, int ridx, uint32_t* v)
{
    int res = 0;
    res = res ? res : xsdr_phy_rx_reg(d, false, PHY_REG_RXBANK_LFSRCHK, ridx, 0);
    res = res ? res : usleep(1);
    res = res ? res : lowlevel_reg_rd32(d->base.lmsstate.dev, 0, REG_CFG_PHY_0, v);
    return res;
}

int xsdr_phy_lfsr_mimo_state(xsdr_dev_t *d, int type, uint32_t v[4])
{
    int res = 0;
    // Get BER statistics
    for (unsigned h = 0; h < 4; h++) {
        unsigned p = ((type & 0x3) << 2) | (h);
        res = res ? res : xsdr_phy_rx_reg(d, false, PHY_REG_RXBANK_LFSRCHK, p, 0);
        res = res ? res : usleep(1);
        res = res ? res : lowlevel_reg_rd32(d->base.lmsstate.dev, 0, REG_CFG_PHY_0, &v[h]);
    }

    return res;
}

int xsdr_phy_tx_reg(xsdr_dev_t *d, uint8_t addr, uint32_t val)
{
    return lowlevel_reg_wr32(d->base.lmsstate.dev, d->base.lmsstate.subdev, REG_CFG_PHY_1, (((uint32_t)addr) << 24) | (val & 0xffffff));
}

int xsdr_phy_en_lfsr_generator_mimo(xsdr_dev_t *d, bool en, bool lfsr)
{
    int res = 0;
    res = res ? res : xsdr_phy_tx_reg(d, PHY_REG_LFSR_GEN, 0);
    res = res ? res : usleep(1);
    res = res ? res : xsdr_phy_tx_reg(d, PHY_REG_IQAB_GEN, lfsr ? 0 : 1);
    res = res ? res : xsdr_phy_tx_reg(d, PHY_REG_LFSR_GEN, en ? 1 : 0);

    return res;
}

static int _xsdr_rxserdes_reset(xsdr_dev_t *d) {
    int res = 0;
    unsigned sisosdrflag = d->dpump ? 16 : d->base.lml_mode.rxsisoddr ? 8 : 0;
    res = res ? res : lowlevel_reg_wr32(d->base.lmsstate.dev, d->base.lmsstate.subdev, REG_CFG_PHY_0, 0x80000007 | sisosdrflag);
    res = res ? res : usleep(10);
    res = res ? res : lowlevel_reg_wr32(d->base.lmsstate.dev, d->base.lmsstate.subdev, REG_CFG_PHY_0, 0x80000000 | sisosdrflag);
    return res;
}

static int _xsdr_txserdes_reset(xsdr_dev_t *d) {
    int res = 0;
    // Reset OSERDESE2
    res = res ? res : xsdr_phy_tx_reg(d, PHY_REG_PORT_CTRL, 0);
    res = res ? res : usleep(1);
    res = res ? res : xsdr_phy_tx_reg(d, PHY_REG_PORT_CTRL, 1);
    return res;
}

int xsdr_override_drp(xsdr_dev_t *d, lsopaddr_t ls_op_addr,
                      size_t meminsz, void* pin, size_t memoutsz,
                      const void* pout)
{
    int res = 0;
    lldev_t dev = d->base.lmsstate.dev;
    unsigned subdev = d->base.lmsstate.subdev;
    unsigned port = (ls_op_addr >> 16) & 0xff;
    if (port != DRP_MMCM_PORT_TX)
        return -ENOENT;

    uint32_t drp_cmd = (ls_op_addr & 0x7f) << 16;
    if (meminsz == 2 && memoutsz == 0) {
    } else if (meminsz == 0 && memoutsz == 2) {
        drp_cmd |= (1 << 23) | (*((uint16_t*)pout));
    } else {
        return -EINVAL;
    }

    res = res ? res : xsdr_phy_tx_reg(d, PHY_REG_MMCM_DRP, drp_cmd);

    // Wait for transaction to process
    uint32_t rb;
    for (unsigned i = 0; i < 1000; i++) {
        res = res ? res : lowlevel_reg_rd32(dev, subdev, REG_CFG_PHY_1, &rb);
        if (res || (!(rb & (1 << 9))))
            break;

        usleep(10);
    }

    if (res)
        return res;

    USDR_LOG("XDEV", USDR_LOG_DEBUG, "MMCM DRP_TX CMD=%08x RB=%08x\n", drp_cmd, rb);

    if (meminsz) {
        *((uint16_t*)pin) = rb >> 16;
    }
    return 0;
}

static struct mmcm_config_raw g_tx_cfg_raw;


int  xsdr_upd_phase(xsdr_dev_t *d)
{
    if (d->tx_override_phase) {
        unsigned raw = d->tx_override_phase - 1;
        g_tx_cfg_raw.ports[CLKOUT_PORT_0].phase = raw & 7;
        g_tx_cfg_raw.ports[CLKOUT_PORT_0].delay = (raw >> 3);
    }

    USDR_LOG("XDEV", USDR_LOG_ERROR, "MMCM_TX set phase to %d.%d\n", g_tx_cfg_raw.ports[CLKOUT_PORT_0].delay, g_tx_cfg_raw.ports[CLKOUT_PORT_0].phase);
    mmcm_init_raw(d->base.lmsstate.dev, d->base.lmsstate.subdev, DRP_MMCM_PORT_TX, &g_tx_cfg_raw);

    // Reset MMCM
    xsdr_phy_tx_reg(d, PHY_REG_MMCM_CTRL, 1);
    usleep(1);
    xsdr_phy_tx_reg(d, PHY_REG_MMCM_CTRL, 0);
    return 0;
}

int xsdr_phy_tx_iqsel(xsdr_dev_t *d, uint8_t iqsel)
{
    return xsdr_phy_tx_reg(d, PHY_REG_PORT_IQSEL, iqsel);
}

static int _xsdr_mmcm_pd(xsdr_dev_t *d)
{
    return xsdr_phy_tx_reg(d, PHY_REG_MMCM_CTRL, 2);
}

static int g_clk_reduce = 0;
int xsdr_configure_lml_mmcm_tx(xsdr_dev_t *d, bool rx_master, unsigned rxphase, unsigned txphase, unsigned txphase_off)
{
    bool nomul = d->dpump ? false :
        (rx_master) ? d->base.lml_mode.rxsisoddr || (d->base.rxtsp_div > 1) :
                      d->base.lml_mode.txsisoddr || (d->base.txtsp_div > 1);
    unsigned mmcm_ctrl_sel = (rx_master) ? 0 : 4;
    unsigned tx_mclk = d->base.cgen_clk / d->base.txcgen_div / d->base.lml_mode.txdiv;
    unsigned rx_mclk = d->base.cgen_clk / d->base.rxcgen_div / d->base.lml_mode.rxdiv;
    unsigned io_mclk = (rx_master) ? rx_mclk : tx_mclk;
    unsigned io_clk  = (nomul) ? io_mclk : io_mclk * 2;
    unsigned vco_div_io = (MMCM_VCO_MAX  + io_clk - 1) / io_clk;

    vco_div_io += g_clk_reduce;

    if (vco_div_io > 63)
        vco_div_io = 63;

    int res = 0;
    struct mmcm_config_raw cfg_raw;
    memset(&cfg_raw, 0, sizeof(cfg_raw));

    if (vco_div_io * io_clk < MMCM_VCO_MIN && vco_div_io < 63) {
        if ((vco_div_io + 2) * io_clk > MMCM_VCO_MAX) {
            vco_div_io += 1;
        } else {
            vco_div_io += 2;
        }
    }

    res = res ? res : xsdr_phy_tx_reg(d, PHY_REG_MMCM_CTRL, mmcm_ctrl_sel | 0);
    res = res ? res : xsdr_phy_tx_reg(d, PHY_REG_PORT_IQSEL, d->base.lml_mode.txsisoddr ? 0b1010 : 0b1100);

    // 0 - IO_TX_DIV    ( was IO_TX_IQSEL -- individual phase delay )
    // 1 - IO_TX
    // 2 - IO_RX
    // 3 - n/a ( was LOGIC_TX )
    // 4 - n/a
    // 5 - FCLK_RX
    // 6 - FCLK_TX

#define SEP_CLKDIV

    cfg_raw.type = MT_7SERIES_MMCM;
#ifndef SEP_CLKDIV
    cfg_raw.ports[CLKOUT_PORT_0].period_l = (vco_div_io + 1) / 2; // IO_TX_IQSEL
    cfg_raw.ports[CLKOUT_PORT_0].period_h = vco_div_io / 2;       // IO_TX_IQSEL
#else
    cfg_raw.ports[CLKOUT_PORT_0].period_l = vco_div_io;           // IO_TX CLKDIV
    cfg_raw.ports[CLKOUT_PORT_0].period_h = vco_div_io;           // IO_TX CLKDIV
#endif
    cfg_raw.ports[CLKOUT_PORT_1].period_l = (vco_div_io + 1) / 2; // IO_TX
    cfg_raw.ports[CLKOUT_PORT_1].period_h = vco_div_io / 2;       // IO_TX

    cfg_raw.ports[CLKOUT_PORT_2].period_l = (vco_div_io + 1) / 2; // IO_RX
    cfg_raw.ports[CLKOUT_PORT_2].period_h = vco_div_io / 2;       // IO_RX
    cfg_raw.ports[CLKOUT_PORT_3].period_l = vco_div_io;           // not used
    cfg_raw.ports[CLKOUT_PORT_3].period_h = vco_div_io;           // not used

    cfg_raw.ports[CLKOUT_PORT_4].period_l = vco_div_io;           // not used
    cfg_raw.ports[CLKOUT_PORT_4].period_h = vco_div_io;           // not used
    cfg_raw.ports[CLKOUT_PORT_5].period_l = (vco_div_io + 1) / 2;
    cfg_raw.ports[CLKOUT_PORT_5].period_h = vco_div_io / 2;
    cfg_raw.ports[CLKOUT_PORT_6].period_l = (vco_div_io + 1) / 2;
    cfg_raw.ports[CLKOUT_PORT_6].period_h = vco_div_io / 2;       // FCLK_TX

    unsigned total_budget = 8 * vco_div_io;
    unsigned phase = (((tx_mclk < 2*35e6) || (tx_mclk > 2*60e6)) ? 4 : 5) * total_budget / 6;
    // unsigned phase_iq = 0; //4;

    cfg_raw.ports[CLKOUT_PORT_6].phase = phase % 8;
    cfg_raw.ports[CLKOUT_PORT_6].delay = phase / 8;

#ifndef SEP_CLKDIV
    cfg_raw.ports[CLKOUT_PORT_0].phase = phase_iq % 8;
    cfg_raw.ports[CLKOUT_PORT_0].delay = phase_iq / 8;

    if (d->tx_override_phase_iq || txphase || txphase_off) {
        //unsigned raw = (txphase != 0) ? txphase - 1 : d->tx_override_phase_iq - 1;

        unsigned raw = (txphase != 0) ? txphase_off : d->tx_override_phase_iq - 1;
        cfg_raw.ports[CLKOUT_PORT_0].phase = raw % 8;
        cfg_raw.ports[CLKOUT_PORT_0].delay = raw / 8;
    }
#else
    cfg_raw.ports[CLKOUT_PORT_0].phase = 0;
    cfg_raw.ports[CLKOUT_PORT_0].delay = 0;
#endif
    // if (d->tx_override_phase || txphase) {
    //     unsigned raw = (txphase != 0) ? txphase - 1 : d->tx_override_phase - 1;
    //     cfg_raw.ports[CLKOUT_PORT_1].phase = raw % 8;
    //     cfg_raw.ports[CLKOUT_PORT_1].delay = raw / 8;
    // }

    if (d->tx_override_phase || txphase || txphase_off) {
        //unsigned raw = (txphase != 0) ? ((txphase - 1 + txphase_off) % 64) : d->tx_override_phase - 1;

        unsigned raw = (txphase != 0) ? (txphase - 1) : d->tx_override_phase - 1;
        cfg_raw.ports[CLKOUT_PORT_6].phase = raw % 8;
        cfg_raw.ports[CLKOUT_PORT_6].delay = raw / 8;
    }

    if (d->rx_override_phase || rxphase) {
        unsigned raw = (rxphase != 0) ? rxphase - 1 : d->rx_override_phase - 1;
       cfg_raw.ports[CLKOUT_PORT_2].phase = raw % 8;
       cfg_raw.ports[CLKOUT_PORT_2].delay = raw / 8;

//        cfg_raw.ports[CLKOUT_PORT_FB].phase = raw % 8;
//        cfg_raw.ports[CLKOUT_PORT_FB].delay = raw / 8;
    }

    if (nomul) {
        cfg_raw.ports[CLKOUT_PORT_FB].period_l =(vco_div_io + 1) / 2;
        cfg_raw.ports[CLKOUT_PORT_FB].period_h = vco_div_io / 2;
    } else {
        cfg_raw.ports[CLKOUT_PORT_FB].period_l = vco_div_io;
        cfg_raw.ports[CLKOUT_PORT_FB].period_h = vco_div_io;
    }
    USDR_LOG("XDEV", USDR_LOG_INFO, "MMCM_TX set to MCLK = %.3f IOCLK = %.3f Mhz IODIV = %d FWDCLK_DELAY%s = %d (VCO %d) SISO_DDR=%d VCO=%.3f MHZ OFF=%d\n",
             tx_mclk / (1.0e6), io_clk / (1.0e6), vco_div_io,
             (d->tx_override_phase) ? "_OVR" : "",
             cfg_raw.ports[CLKOUT_PORT_0].delay, cfg_raw.ports[CLKOUT_PORT_0].phase,
             d->base.lml_mode.txsisoddr,
             tx_mclk * (cfg_raw.ports[CLKOUT_PORT_FB].period_l + cfg_raw.ports[CLKOUT_PORT_FB].period_h) / 1.0e6,
             txphase_off);

    res = res ? res : mmcm_init_raw(d->base.lmsstate.dev, d->base.lmsstate.subdev, DRP_MMCM_PORT_TX, &cfg_raw);

    // Reset MMCM
    res = res ? res : xsdr_phy_tx_reg(d, PHY_REG_MMCM_CTRL, mmcm_ctrl_sel | 1);
    usleep(10);
    res = res ? res : xsdr_phy_tx_reg(d, PHY_REG_MMCM_CTRL, mmcm_ctrl_sel | 0);
    if (res)
        return res;

    for (unsigned k = 0; k < 100; k++) {
        uint32_t rb;

        // Wait for lock
        res = res ? res : lowlevel_reg_rd32(d->base.lmsstate.dev, d->base.lmsstate.subdev,
                                            REG_CFG_PHY_1, &rb);
        if (res)
            break;

        USDR_LOG("XDEV", USDR_LOG_DEBUG, "MMCM FLAGS:%08x\n", rb);
        if (rb & (1 << 8)) {
             g_tx_cfg_raw = cfg_raw;
            return 0;
        }

        usleep(10);
    }

    USDR_LOG("XDEV", USDR_LOG_ERROR, "MMCM Redy flag timed out!\n");
    return -EIO;
}

int xsdr_configure_lml_mmcm_rx(xsdr_dev_t *d)
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
    USDR_LOG("XDEV", USDR_LOG_ERROR, "MMCM_RX set to MCLK = %.3f IOCLK = %.3f Mhz IODIV = %d\n",
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
    res = mmcm_init_raw(d->base.lmsstate.dev, d->base.lmsstate.subdev, DRP_MMCM_PORT_RX, &cfg_raw);
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
        if (res)
            break;

        USDR_LOG("XDEV", USDR_LOG_INFO, "MMCM FLAGS:%08x\n", rb);
        if (rb & (1 << 16))
            return 0;

        usleep(5000);
    }

    return res;
}

int xsdr_phy_tune_rx(xsdr_dev_t *d, unsigned val)
{
    int res = mmcm_set_phdigdelay_raw(d->base.lmsstate.dev, d->base.lmsstate.subdev, DRP_MMCM_PORT_RX, CLKOUT_PORT_0, val);
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

enum {
    PHY_CFG_VALID_MSK = 0x80,
    PHY_CFG_LML2_IS_RX = 0x40,
    PHY_CFG_TX_MMCM = 0x20,
    PHY_CFG_RX_MMCM = 0x10,
};

static bool noerrors_v4(unsigned errs[4], uint64_t* badness)
{
    if (badness) {
        *badness = (errs[0]*errs[0]) + (errs[1]*errs[1]) + (errs[2]*errs[2])  + (errs[3]*errs[3]);
    }
    return errs[0] == 0 && errs[1] == 0 && errs[2] == 0 && errs[3] == 0;
}

static bool noerrors_v2(unsigned errs[4], uint64_t* badness)
{
    if (badness) {
        *badness = (errs[0]*errs[0]) + (errs[1]*errs[1]);
    }
    return errs[0] == 0 && errs[1] == 0;
}

int xsdr_txphase_ovr(xsdr_dev_t *d, unsigned v)
{
    int res = 0;
    d->tx_override_phase_iq = v;
    d->tx_override_phase = v;

    res = res ? res : xsdr_configure_lml_mmcm_tx(d, false, d->lmlcal_rx_phase, 0, 0);
    res = res ? res : _xsdr_txserdes_reset(d);

    return res;
}

static int _xsdr_calibrate_txlfsr_check(xsdr_dev_t *d, unsigned check_to,
                                        unsigned errs[4], unsigned *piqserrs,
                                        uint64_t *pbadness)
{
    int res = 0;
    unsigned w;

    // Reset OSERDESE2
    res = res ? res : _xsdr_txserdes_reset(d);
    res = res ? res : usleep(1);
    res = res ? res : lms7002m_limelight_fifo_reset(&d->base.lmsstate, true, true);
    res = res ? res : _xsdr_rxserdes_reset(d); // In case of RX-TX in the same MMCM
    res = res ? res : usleep(20);
    res = res ? res : xsdr_phy_en_lfsr_checker_mimo(d, true);
    //res = res ? res : xsdr_phy_en_iqab_checker_mimo(d, true);

    for (w = 0; w < check_to; w++) {
        res = res ? res : usleep(100);
        res = res ? res : xsdr_phy_lfsr_mimo_state(d, LFSR_CNTR_BER, errs);
        res = res ? res : xsdr_phy_lfsr_mimo_state_s(d, LFSR_CNTR_IQS << 2, piqserrs);
        if (res || (!noerrors_v4(errs, pbadness)) || *piqserrs) {
            break;
        }
    }
    *pbadness *= 1.0 * check_to / (w + 1); // Rescale

    return 0;
}

static int _xsdr_calibrate_lml(xsdr_dev_t *d)
{
    int res = 0;
    bool mmcm_rx_only_path = (!d->base.tx_run[0] && !d->base.tx_run[1]);
    bool old_rx_run[2] = { d->base.rx_run[0], d->base.rx_run[1] };

    uint64_t rx_badness = UINT64_MAX;
    uint64_t tx_badness = UINT64_MAX;
    unsigned tx_iqerrs = UINT_MAX;

    g_clk_reduce = 0;

    // Fixup for SSDR_PRO
    if (d->ssdr_pro) {
        // RX SISO DDR
        res = res ? res : lowlevel_reg_wr32(d->base.lmsstate.dev, d->base.lmsstate.subdev, REG_CFG_PHY_0,
                                            d->siso_sdr_active_rx ? (1u << 9) : 0);
        return res;
    }

    if (d->mmcm_tx) {
        // Boost IO voltage for stable high speed link
        if (!d->siso_sdr_active_rx && d->new_rev && d->ssdr && (d->s_rxrate > 85e6 || d->s_txrate > 85e6)) {
            res = res ? res : xsdr_set_vio(d, 1910);
        } else if (!d->siso_sdr_active_rx && d->new_rev && !d->ssdr && (d->s_rxrate > 70e6 || d->s_txrate > 70e6)) {
            res = res ? res : xsdr_set_vio(d, 1940);
            res = res ? res : xsdr_set_lms125vdd(d, 1320);
        }

        // Fixup for 53-58 MSPS range, but still 58 to 60 might be unoperable on some chips, and 60+ works fine again
        if (!mmcm_rx_only_path && d->new_rev && !d->ssdr && (d->s_txrate >= 53e6 && d->s_txrate <= 60e6)) {
            res = res ? res : xsdr_set_lms125vdd(d, 1360);
        }

        if (!(d->base.rx_run[0] || d->base.rx_run[1])) {
            res = res ? res : dev_gpo_set(d->base.lmsstate.dev, IGPO_LMS_PWR, IGPO_LMS_PWR_LDOEN | IGPO_LMS_PWR_NRESET | IGPO_LMS_PWR_RXEN  | IGPO_LMS_PWR_TXEN);
            res = res ? res : usleep(1000);
            res = res ? res : lms7002m_streaming_up(&d->base, RFIC_LMS7_RX, LMS7_CH_AB, 0, 0, 0);
        }

        res = res ? res : xsdr_configure_lml_mmcm_tx(d, mmcm_rx_only_path, 0, 0, 0);
        res = res ? res : lms7002m_limelight_reset(&d->base.lmsstate);
        res = res ? res : _xsdr_rxserdes_reset(d);

        if (res)
            return res;

        // Autocalibration if RX phase wasn't set
        if (d->rx_override_phase == 0) {
            const unsigned check_to = 10;
            unsigned phase_m;
            int ph_ty_m = 0;
            unsigned iqserrs;
            unsigned errs[4] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };


            for (unsigned rxrty = 0; rxrty < 4; rxrty++) {
            uint64_t badness_m = UINT64_MAX;

            phase_m = 0;
            res = res ? res : lms7002m_set_lmlrx_mode(&d->base, XSDR_LMLRX_LFSR);

            int phase_min;
            int phase_max;

            //for (unsigned rty = 0; rty < 3; rty++) {
            phase_min = 65;
            phase_max = 0;

            badness_m = UINT64_MAX;

            for (unsigned ph = 1; ph < 65; ph++) {
                unsigned w;
                uint64_t badness = UINT64_MAX;

                res = res ? res : usleep(10);
                res = res ? res : xsdr_configure_lml_mmcm_tx(d, mmcm_rx_only_path, ph, 0, 0);
                res = res ? res : lms7002m_limelight_fifo_reset(&d->base.lmsstate, true, true);
                res = res ? res : _xsdr_rxserdes_reset(d);
                res = res ? res : usleep(10);
                res = res ? res : xsdr_phy_en_lfsr_checker_mimo(d, true);

                for (w = 0; w < check_to; w++) {
                    res = res ? res : usleep(100);
                    res = res ? res : xsdr_phy_lfsr_mimo_state(d, LFSR_CNTR_BER, errs);
                    if (res || (d->dpump ? !noerrors_v2(errs, &badness) : !noerrors_v4(errs, &badness))) {
                        break;
                    }
                }
                badness *= 1.0 * check_to / (w + 1); // Rescale

                USDR_LOG("XDEV", USDR_LOG_INFO, "PHASE_RX=%2d I=%2d [%6d/%6d/%6d/%6d] BD=%lld\n", ph - 1, w,
                         errs[0], errs[1], errs[2], errs[3], (long long)badness);
                if (res || (d->dpump ? noerrors_v2(errs, &badness) : noerrors_v4(errs, &badness))) {
                    phase_m = ph;
                    if (ph < phase_min)
                        phase_min = ph;
                    if (ph > phase_max)
                        phase_max = ph;

                    // Got more than 7 phases, we're safe; skip searching
                    if (phase_max - phase_min > 7)
                        break;
                } else if (phase_max >= phase_min) {
                    break;
                }

                if (badness_m > badness) {
                    badness_m = badness;
                    phase_m = ph;
                }
            }

            if (phase_max > phase_min) {
                phase_m = (phase_max + phase_min) / 2;
            }

            USDR_LOG("XDEV", USDR_LOG_WARNING, "Restoring RX pahse to %d (bandness=%" PRId64 ")  PH_MIN=%d PH_MAX=%d\n",
                     phase_m - 1, badness_m, phase_min, phase_max);

            // Try our best at least
            res = res ? res : xsdr_configure_lml_mmcm_tx(d, mmcm_rx_only_path, phase_m, 0, 0);
            res = res ? res : lms7002m_limelight_fifo_reset(&d->base.lmsstate, true, true);
            res = res ? res : _xsdr_rxserdes_reset(d);
            res = res ? res : usleep(10);
            res = res ? res : xsdr_phy_en_lfsr_checker_mimo(d, true);
            {
                uint64_t badness = UINT64_MAX;
                unsigned w;
                for (w = 0; w < check_to; w++) {
                    res = res ? res : usleep(100);
                    res = res ? res : xsdr_phy_lfsr_mimo_state(d, LFSR_CNTR_BER, errs);
                    if (res || (d->dpump ? !noerrors_v2(errs, &badness) : !noerrors_v4(errs, &badness))) {
                        break;
                    }
                }
                badness *= 1.0 * check_to / (w + 1); // Rescale
                rx_badness = badness;
                if (badness > 100 /*badness_m * 2*/) {
                    USDR_LOG("XDEV", USDR_LOG_WARNING, "RePHASE_RX=%2d I=%2d [%6d/%6d/%6d/%6d] BD=%lld\n", phase_m - 1, w,
                             errs[0], errs[1], errs[2], errs[3], (long long)badness);

                    g_clk_reduce++;
                    continue;
                }
            }

            break;
            }

            d->lmlcal_rx_phase = phase_m;

            if (mmcm_rx_only_path)
                goto no_tx;

            res = res ? res : lms7002m_set_lmlrx_mode(&d->base, XSDR_LMLRX_DIGLOOPBACK);
            res = res ? res : xsdr_phy_en_lfsr_generator_mimo(d, true, true);
            res = res ? res : xsdr_phy_en_lfsr_checker_mimo(d, true);
            //res = res ? res : xsdr_phy_en_iqab_checker_mimo(d, true);

            bool mmcx_rx_path = false;
            bool check_rx = false;
            uint64_t badness_m = UINT64_MAX;
            unsigned failed_rxcnt = 0;
            phase_m = 0;
            for (unsigned rty = 0; rty < 8; rty++) {
                const unsigned iq_phases[8] = { 0, 1, 2, 62, 63, 2, 1, 0};
                unsigned iq_ph = iq_phases[rty];

                for (unsigned ph = 1; ph < 65; ph++) {
                    //unsigned w;
                    uint64_t badness = UINT64_MAX;
                    res = res ? res : xsdr_configure_lml_mmcm_tx(d, mmcx_rx_path, d->lmlcal_rx_phase, ph, iq_ph);
                    if (check_rx) {
                        res = res ? res : lms7002m_set_lmlrx_mode(&d->base, XSDR_LMLRX_LFSR);
                        res = res ? res : lms7002m_limelight_fifo_reset(&d->base.lmsstate, true, true);
                        res = res ? res : _xsdr_rxserdes_reset(d);
                        res = res ? res : usleep(10);
                        res = res ? res : xsdr_phy_en_lfsr_checker_mimo(d, true);
                        res = res ? res : usleep(100);
                        res = res ? res : xsdr_phy_lfsr_mimo_state(d, LFSR_CNTR_BER, errs);
                        if (res || (d->dpump ? !noerrors_v2(errs, &badness) : !noerrors_v4(errs, &badness))) {
                            USDR_LOG("XDEV", USDR_LOG_WARNING, "FAIL_PHASE_RX=%2d PH=%2d [%6d/%6d/%6d/%6d] BD=%lld\n", d->lmlcal_rx_phase, ph - 1,
                                     errs[0], errs[1], errs[2], errs[3], (long long)badness);
                            if (badness > 20) {
                                if (failed_rxcnt < 6) {
                                    ph--;
                                    failed_rxcnt++;
                                    continue;
                                }
                            }
                        }
                        res = res ? res : lms7002m_set_lmlrx_mode(&d->base, XSDR_LMLRX_DIGLOOPBACK);
                        failed_rxcnt = 0;
                    }
                    res = res ? res : _xsdr_calibrate_txlfsr_check(d, check_to, errs, &iqserrs, &badness);

                    USDR_LOG("XDEV", USDR_LOG_INFO, "PHASE_TX=%2d  [%6d/%6d/%6d/%6d - %6d] BD=%.3e\n", ph - 1,
                             errs[0], errs[1], errs[2], errs[3], iqserrs, (double)badness);
                    if (res || (noerrors_v4(errs, &badness) /* && (iqserrs == 0)*/) || (rty > 1 && badness < 20)) {
                        phase_m = ph;

                        for (int g = 0; g < 12/*50*/; g++) {
                            // Check A/B & I/Q aligment is ok
                            res = res ? res : xsdr_phy_en_lfsr_generator_mimo(d, true, false);
                            res = res ? res : usleep(10);
                            res = res ? res : xsdr_phy_en_iqab_checker_mimo(d, true);
                            res = res ? res : usleep(1000);
                            res = res ? res : xsdr_phy_lfsr_mimo_state_s(d, LFSR_CNTR_IQS << 2, &iqserrs);
                            if (res)
                                return res;

                            if ((rty == 0 && iqserrs != 0) || iqserrs > 40) {
                                USDR_LOG("XDEV", USDR_LOG_INFO, "PHASE_TX[%d]=%2d ABIQ=%d\n", g, ph - 1, iqserrs);
                                unsigned msk[12] = { 0b1100, 0b0110, 0b0011, 0b1001,   0b1000, 0b0100, 0b0010, 0b0001,  0b1100, 0b0110, 0b1001, 0b1100 };

                                res = res ? res : xsdr_phy_tx_reg(d, PHY_REG_PORT_IQSEL, d->base.lml_mode.txsisoddr ? 0b1010 : msk[g + 1]);
                                //res = res ? res : _xsdr_txserdes_reset(d);
                                res = res ? res : usleep(10);
                                res = res ? res : lms7002m_limelight_reset(&d->base.lmsstate);
                                res = res ? res : usleep(10);

                            } else if (g > 0) {
                                USDR_LOG("XDEV", USDR_LOG_INFO, "PHASE_TX=%2d ABIQ=%d\n", ph - 1, iqserrs);
                                break;
                            }
                        }

                        if ((rty == 0 && iqserrs != 0) || iqserrs > 40) {
                            USDR_LOG("XDEV", USDR_LOG_INFO, "PHASE_TX=%2d ABIQ=%d\n", ph - 1, iqserrs);
                            res = res ? res : xsdr_phy_en_lfsr_generator_mimo(d, true, true);
                            continue;
                        }

                        tx_badness = badness;
                        tx_iqerrs = iqserrs;
                        goto phase_tx_calibrated;
                    }

                    if (badness_m > badness) {
                        badness_m = badness;
                        phase_m = ph;
                        ph_ty_m = rty;
                    }
                }
                // Try to toggle clock inversion
                res = res ? res : lms7002m_limelight_toggle_ntx(&d->base.lmsstate);
                check_rx = true;
            }
            USDR_LOG("XDEV", USDR_LOG_WARNING, "Restoring TX pahse to %d (bandness=%" PRId64 ")\n",
                     phase_m, badness_m);

            // Try our best at least
            res = res ? res : xsdr_configure_lml_mmcm_tx(d, mmcx_rx_path, d->lmlcal_rx_phase, phase_m, ph_ty_m);

            // Make sure it's a good value
            res = res ? res : _xsdr_calibrate_txlfsr_check(d, check_to, errs, &iqserrs, &badness_m);

            unsigned iqserrs2 = -1;

            // Check A/B & I/Q aligment is ok
            res = res ? res : xsdr_phy_en_lfsr_generator_mimo(d, true, false);
            res = res ? res : usleep(10);
            res = res ? res : xsdr_phy_en_iqab_checker_mimo(d, true);
            res = res ? res : usleep(1000);
            res = res ? res : xsdr_phy_lfsr_mimo_state_s(d, LFSR_CNTR_IQS << 2, &iqserrs2);
            if (res)
                return res;

            tx_badness = badness_m;
            tx_iqerrs = iqserrs2;

            USDR_LOG("XDEV", USDR_LOG_INFO, "RESTORE PHASE_TX=%2d  [%6d/%6d/%6d/%6d - %6d - %6d] BD=%.3e\n", phase_m - 1,
                     errs[0], errs[1], errs[2], errs[3], iqserrs, iqserrs2, (double)badness_m);

        phase_tx_calibrated:
            d->lmlcal_tx_phase = phase_m;
            res = res ? res : xsdr_phy_en_lfsr_generator_mimo(d, false, false);
            res = res ? res : xsdr_phy_en_lfsr_checker_mimo(d, false);
            res = res ? res : lms7002m_set_lmlrx_mode(&d->base, XSDR_LMLRX_NORMAL);
        }
    } else {

        unsigned dly = (d->tx_override_phase) ? (d->tx_override_phase - 1) : 3;
        res = res ? res : xsdr_phy_tx_reg(d, PHY_REG_DLY_VALUE, dly);
    }

no_tx:
    if (!old_rx_run[0] && !old_rx_run[1]) {
        // No RX, disable it
        res = res ? res : lms7002m_streaming_down(&d->base, RFIC_LMS7_RX);
    } else {
        res = res ? res : lms7002m_set_lmlrx_mode(&d->base, XSDR_LMLRX_NORMAL);
    }

    if (rx_badness || (!mmcm_rx_only_path && (tx_badness || tx_iqerrs))) {
        bool severe = (rx_badness > 100) || (!mmcm_rx_only_path && (tx_badness > 100 || tx_iqerrs > 10));
        USDR_LOG("XDEV", severe ? USDR_LOG_ERROR : USDR_LOG_WARNING, "LML Calibration failed: RX_BADNESS=%"PRId64" TX_BADNESS=%"PRId64" TX_IQERRS=%d\n",
                 rx_badness, mmcm_rx_only_path ? 0 : tx_badness, mmcm_rx_only_path ? 0 : tx_iqerrs);

        if (res == 0 && severe)
            res = -ERANGE;
    }
    return res;
}

int xsdr_set_samplerate_ex(xsdr_dev_t *d,
                           unsigned rxrate, unsigned txrate,
                           unsigned adcclk, unsigned dacclk,
                           unsigned flags)
{
    lldev_t dev = d->base.lmsstate.dev;
    subdev_t subdev = d->base.lmsstate.subdev;
    // unsigned sisosdrflag;
    int res;

    if (!(((d->hwid) & 0xff) & PHY_CFG_VALID_MSK)) {
        USDR_LOG("XDEV", USDR_LOG_ERROR, "Incompatible firmware, please update to 20250501 at least!\n");
        return -ENOTSUP;
    }

    res = _xsdr_checkpwr(d);
    if (res)
        return res;

    if (rxrate == 0 && txrate == 0) {
        rxrate = 1e6;
    }

    unsigned m_flags = flags | (((d->siso_sdr_active_rx && d->hwchans_rx == 1) || d->dpump) ? XSDR_LML_SISO_DDR_RX : 0)
                       | (((d->siso_sdr_active_tx && d->hwchans_tx == 1) || d->dpump) ? XSDR_LML_SISO_DDR_TX : 0);

    res = lms7002m_samplerate(&d->base, rxrate, txrate, adcclk, dacclk, m_flags, d->rx_port_is_1);
    if (res)
        return res;

    d->s_rxrate = rxrate;
    d->s_txrate = txrate;
    d->s_adcclk = adcclk;
    d->s_dacclk = dacclk;
    d->s_flags = m_flags;
    d->cfg_srate_siso_rx = (m_flags & XSDR_LML_SISO_DDR_RX) ? 1 : 0;
    d->cfg_srate_siso_tx = (m_flags & XSDR_LML_SISO_DDR_TX) ? 1 : 0;

    if (d->afe_active == false) {
        // Need AFE for reference cloking
        lms7002m_afe_enable(&d->base.lmsstate, true, true, true, true); // TODO: Check if rx & tx, a & b is required!

        // wait for clock to stabilize
        usleep(10000);

        // We need RxTSP & TxTSP configured for proper LML - TSP alignment before LFSR training
        d->afe_active = true;
    }

    if (rxrate) {
#if 0
        if (d->mmcm_rx) {
            res = res ? res : xsdr_configure_lml_mmcm_rx(d);
        }
#endif
#if 0
        sisosdrflag = d->dpump ? 16 : d->base.lml_mode.rxsisoddr ? 8 : 0;
        res = res ? res : lowlevel_reg_wr32(dev, subdev, REG_CFG_PHY_0, 0x80000007 | sisosdrflag);
        usleep(100);
        res = res ? res : lowlevel_reg_wr32(dev, subdev, REG_CFG_PHY_0, 0x80000000 | sisosdrflag);
#endif
#if 0
        if (d->mmcm_rx) {
            // Configure PHY (reset)
            // TODO phase search

            for (unsigned h = 0; h < 16; h++) {
                res = res ? res : lowlevel_reg_wr32(dev, subdev, REG_CFG_PHY_0, 0x80000007 | sisosdrflag);
                usleep(100);
                res = res ? res : lowlevel_reg_wr32(dev, subdev, REG_CFG_PHY_0, 0x80000000 | sisosdrflag);


                USDR_LOG("XDEV", USDR_LOG_INFO, "PHASE=%d\n", h);
                res = res ? res : lowlevel_reg_wr32(dev, subdev, REG_CFG_PHY_0, 0x00000000);
                unsigned tmp; //, tmp2;
                for (unsigned k = 0; k < 100; k++) {
                    res = res ? res : lowlevel_reg_rd32(dev, subdev, REG_CFG_PHY_0, &tmp);
                }

                res = res ? res : mmcm_set_digdelay_raw(d->base.lmsstate.dev, d->base.lmsstate.subdev, DRP_MMCM_PORT_RX, CLKOUT_PORT_0, h);
            }
        }
#endif
        if (!d->ssdr_pro) {
        // Switch to clock meas
        res = res ? res : lowlevel_reg_wr32(dev, subdev, REG_CFG_PHY_0, 0x02000000);
        }
    }

    if (txrate || d->ssdr_pro) {
        res = res ? res : _xsdr_calibrate_lml(d);
    }

    return res;
}


int xsdr_clk_debug_info(xsdr_dev_t *d)
{
    lldev_t dev = d->base.lmsstate.dev;
    // subdev_t subdev = d->base.lmsstate.subdev;
    unsigned crx, ctx, caux;
    int res = 0;

    uint32_t dump[13];

    res = res ? res : dev_gpi_get32(dev, IGPI_MEAS_RXCLK, &crx);
    res = res ? res : dev_gpi_get32(dev, IGPI_MEAS_TXCLK, &ctx);
    res = res ? res : dev_gpi_get32(dev, IGPI_CLK1PPS, &caux);
    if (res)
        return res;

    if (!d->ssdr_pro) {
        for (unsigned h = 0; h < 13; h++) {
            unsigned p = ((h & 0x3) << 2) | (h >> 2);
            res = res ? res : xsdr_phy_rx_reg(d, false, PHY_REG_RXBANK_LFSRCHK, p, 0);
            res = res ? res : xsdr_phy_rx_reg(d, false, PHY_REG_RXBANK_LFSRCHK, p, 0);
            res = res ? res : xsdr_phy_rx_reg(d, false, PHY_REG_RXBANK_LFSRCHK, p, 0);
            res = res ? res : xsdr_phy_rx_reg(d, false, PHY_REG_RXBANK_LFSRCHK, p, 0);

            res = res ? res : lowlevel_reg_rd32(dev, 0, REG_CFG_PHY_0, &dump[h]);
        }
    } else {
        memset(dump, 0, sizeof(dump));
    }


    USDR_LOG("XDEV", USDR_LOG_WARNING, "PHY - RX %08x (%d) / TX %08x (%d) / AUX %08x (%d) %d/%d/%d/%d  %d/%d/%d/%d  %d/%d/%d/%d -- %d \n",
             crx, crx & 0xfffffff,
             ctx, ctx & 0xfffffff,
             caux, caux & 0xfffffff,
             dump[0], dump[1], dump[2], dump[3],  dump[4], dump[5], dump[6], dump[7],   dump[8], dump[9], dump[10], dump[11], dump[12]
             );
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

    static const lms7002m_lml_map_t diqarray_tx[] = {
         // MIMO modes
         // {{ LML_BQ, LML_BI, LML_AQ, LML_AI }},
         // {{ LML_BI, LML_BQ, LML_AI, LML_AQ }},
         // {{ LML_AQ, LML_AI, LML_BQ, LML_BI }},
         // {{ LML_AI, LML_AQ, LML_BI, LML_BQ }},
        {{ LML_AQ, LML_AI, LML_BQ, LML_BI }},
        {{ LML_AI, LML_AQ, LML_BI, LML_BQ }},
        {{ LML_BQ, LML_BI, LML_AQ, LML_AI }},
        {{ LML_BI, LML_BQ, LML_AI, LML_AQ }},

         // SISO modes
         {{ LML_AQ, LML_AI, LML_AQ, LML_AI }},
         {{ LML_AI, LML_AQ, LML_AI, LML_AQ }},
         {{ LML_BQ, LML_BI, LML_BQ, LML_BI }},
         {{ LML_BI, LML_BQ, LML_BI, LML_BQ }},

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
    const lms7002m_lml_map_t *diqarray = (rx) ? diqarray_rx : diqarray_tx;;
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
    int res = lms7002m_streaming_up(&d->base, dir, (lms7002m_mac_mode_t)rx_chs, rx_flags, (lms7002m_mac_mode_t)tx_chs, tx_flags);
    if (res)
        return res;

    d->afe_active = true;
    return 0;
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
    if (d->ssdr && freq > 3.0e9) {
        float bwef = d->lms8st_bwef_1000 / 1000.0;
        int res = 0;
        d->lms7_lob = 2.01e9;

        res = res ? res : dev_gpo_set(d->base.lmsstate.dev, IGPO_LMS8_CTRL, 0x81);

#if 0
        res = res ? res : lms8001b_hlmix_loss_set(&d->lms8, 2, 0);
        res = res ? res : lms8001b_hlmix_loss_set(&d->lms8, 3, 0);
#else
        res = res ? res : lms8001a_ch_lna_pa_set(&d->lms8, 0, 0, 0);
        res = res ? res : lms8001a_ch_lna_pa_set(&d->lms8, 1, 0, 0);
        res = res ? res : lms8001a_ch_lna_pa_set(&d->lms8, 2, 0, 0);
        res = res ? res : lms8001a_ch_lna_pa_set(&d->lms8, 3, 0, 0);
#endif
        res = res ? res : lms8001_core_enable(&d->lms8, 1);
        res = res ? res : lms8001_ch_enable(&d->lms8, 0xf);

        //res = res ? res : lms8001_tune(&d->lms8, d->base.fref, freq - d->lms7_lob);
        res = res ? res : lms8001_smart_tune(&d->lms8, 0, freq - d->lms7_lob, d->base.fref,
                                             d->lms8st_loopbw, d->lms8st_phasemargin, bwef, d->lms8st_flock_n);

        res = res ? res : dev_gpo_set(d->base.lmsstate.dev, IGPO_LMS8_CTRL, 0x80);
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
#if 0
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
#endif
    return -ENOTSUP;
}


int xsdr_ctor(lldev_t dev, xsdr_dev_t *d)
{
    memset(d, 0, sizeof(xsdr_dev_t));
    d->base.lmsstate.dev = dev;

    d->base.on_ant_port_sw = &_xsdr_antenna_port_switch;
    d->base.on_get_lml_portcfg = &lms7nfe_get_lml_portcfg;

    d->lms8_rx_f_switchover = 3.5e9;
    d->lms8_tx_f_switchover = 3.5e9;

    d->lms8st_loopbw = 300000;
    d->lms8st_phasemargin = 50;
    d->lms8st_bwef_1000 = 2000;
    d->lms8st_flock_n = 100;
    d->lms8st_iq_gen = 0;
    d->lms8st_int_mod = 0;
    d->lms8st_enabled = 1;

    return 0;
}

int _xsdr_init_revx(xsdr_dev_t *d, unsigned hwid)
{
    lldev_t dev = d->base.lmsstate.dev;
    unsigned subdev = 0;
    int res = 0;
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

    if (hwid == SSDR_DEV || hwid == SSDRPRO_DEV) {
        // QPC8019Q   0: RFC1 -- HF; 1: RFC2 -- LF

        d->base.cfg_auto_rx[0].stop_freq = 3000e6;
        d->base.cfg_auto_tx[0].stop_freq = 3000e6;

        d->base.cfg_auto_rx[0].sw = 1;
        d->base.cfg_auto_rx[0].swlb = 0;

        d->base.cfg_auto_rx[1].sw = 0;
        d->base.cfg_auto_rx[1].swlb = 1;
    }

    res = res ? res : dev_gpo_set(dev, IGPO_LMS_PWR, 0x80);
    res = res ? res : dev_gpo_set(dev, IGPO_LMS_PWR, 0x00);
    if (res)
        return res;

    uint16_t rev = 0xffff;
    res = lp8758_get_rev(dev, subdev, I2C_BUS_LP8758_FPGA, &rev);
    if (res)
        return res;

    bool good_pmic = (hwid == SSDRPRO_DEV) ? (rev == 0xe302) :  (rev == 0xe001);
    if (good_pmic) {
        d->pmic_ch145_valid = true;
    }

    USDR_LOG("XDEV", (good_pmic) ? USDR_LOG_INFO : USDR_LOG_ERROR, "PMIC_RFIC ver %04x (%d)\n",
             rev, d->pmic_ch145_valid);

    if (hwid == SSDRPRO_DEV) {
        res = lp8758_vout_set(dev, subdev, I2C_BUS_LP8758_FPGA, 3, 2040);
    } if (hwid == SSDR_DEV) {
        res = lp8758_vout_set(dev, subdev, I2C_BUS_LP8758_FPGA, 1, 2040);
    } else {
        // TODO check if we need this rail
        res = lp8758_vout_set(dev, subdev, I2C_BUS_LP8758_FPGA, 1, 1480);
    }

    if (hwid == SSDRPRO_DEV) {
        // TODO adjust VCORE to 0.88V
        // res = res ? res : lp8758_vout_set(dev, subdev, I2C_BUS_LP8758_FPGA, 0, 880);
        res = res ? res : lp8758_vout_set(dev, subdev, I2C_BUS_LP8758_FPGA, 1, 1280);
        res = res ? res : lp8758_vout_set(dev, subdev, I2C_BUS_LP8758_FPGA, 2, 1850);
    } else {
        // LMS Vcore boost to 1.25V
        res = res ? res : lp8758_vout_set(dev, subdev, I2C_BUS_LP8758_FPGA, 2, 1280);
        res = res ? res : lp8758_vout_set(dev, subdev, I2C_BUS_LP8758_FPGA, 3, 1850);
    }

    res = res ? res : lp8758_vout_ctrl(dev, subdev, I2C_BUS_LP8758_FPGA, 0, 1, 1); //1v0 | 0v9 -- less affected
    res = res ? res : lp8758_vout_ctrl(dev, subdev, I2C_BUS_LP8758_FPGA, 1, 1, 1); //2v5 | 1v2 -- less affected
    res = res ? res : lp8758_vout_ctrl(dev, subdev, I2C_BUS_LP8758_FPGA, 2, 1, 1); //1v2 | 1v8
    res = res ? res : lp8758_vout_ctrl(dev, subdev, I2C_BUS_LP8758_FPGA, 3, 1, 1); //1v8 | 2v7

    // Wait for power to settle
    for (unsigned i = 0; !res && !pg && (i < 100); i++) {
        usleep(1000);
        res = res ? res : lp8758_check_pg(dev, subdev, I2C_BUS_LP8758_FPGA, 0xf, &pg);
    }

    if (!pg) {
        USDR_LOG("XDEV", USDR_LOG_INFO, "Couldn't set PMIC voltages!\n");
        return -EIO;
    }

    d->lms8_alive = false;
    // Enable internal clocking by default
    res = res ? res : dev_gpo_set(d->base.lmsstate.dev, IGPO_CLK_CFG, 1);
    if (hwid == SSDR_DEV || hwid == SSDRPRO_DEV) {
        uint32_t chipver = ~0;
        unsigned lms8_step = LMS8_MPW2024;
        if (getenv("LMS8_MPW2015")) {
            lms8_step = LMS8_MPW2015;
        }

        // Check LMS8 presence
        res = res ? res : dev_gpo_set(dev, IGPO_LMS8_CTRL, 0x0);
        res = res ? res : dev_gpo_set(dev, IGPO_LDOLMS_EN, 1); // Enable LDOs
        res = res ? res : dev_gpo_set(dev, IGPO_LMS_PWR, 9);   // LMS
        usleep(100000);

        res = res ? res : lowlevel_spi_tr32(dev, d->base.lmsstate.subdev, 0, 0x002F0000, &chipver);
        USDR_LOG("XDEV", USDR_LOG_INFO, "LMS7002 version %08x\n", chipver);

        for (unsigned j = 0; j < 5; j++) {
            res = res ? res : dev_gpo_set(dev, IGPO_LMS8_CTRL, 0x81);
            usleep(100000);

            res = res ? res : lowlevel_spi_tr32(dev, d->base.lmsstate.subdev, 0, 0x800000ff, &chipver);
            res = res ? res : lowlevel_spi_tr32(dev, d->base.lmsstate.subdev, 0, 0x000f0000, &chipver);
            USDR_LOG("XDEV", USDR_LOG_INFO, "LMS8001 version %08x\n", chipver);

            res = res ? res : lms8001_create(dev, d->base.lmsstate.subdev, 0, lms8_step, &d->lms8);

            res = res ? res : dev_gpo_set(dev, IGPO_LMS8_CTRL, 0x80);
            //res = res ? res : dev_gpo_set(dev, IGPO_LMS8_CTRL, 0x00);
            // res = res ? res : dev_gpo_set(dev, IGPO_LDOLMS_EN, 0); // Enable LDOs
            // res = res ? res : dev_gpo_set(dev, IGPO_LMS_PWR, 0);

            if (chipver != 0x00004040) {
                usleep(100000);
            } else {
                d->lms8_alive = true;
                break;
            }
        }

        if (!getenv("USDR_BARE_DEV") && (!d->lms8_alive)) {
            USDR_LOG("XDEV", USDR_LOG_ERROR, "LMS8001 not detected, check the board!\n");
            return -EFAULT;
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


    // Reset
    res = res ? res : dev_gpo_set(dev, IGPO_LMS_PWR, 0x80);

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

    res = res ? res : lp8758_vout_set(dev, subdev, I2C_BUS_LP8758_FPGA, 1, 3000);

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

    USDR_LOG("XDEV", (on && !d->pmic_ch145_valid) ? USDR_LOG_ERROR : USDR_LOG_INFO,
             "RFIC PWR:%d CH145:%d\n", on, d->pmic_ch145_valid);
    if (on && !d->pmic_ch145_valid) {
        // 1V45 is cricial for Rev0 XSDR and can be ignored in Rev2
        int id = -1;
        res = xsdr_gettemp_id(d, &id);
        if (res == 0) {
            USDR_LOG("XDEV", USDR_LOG_WARNING, "TEMP ID: %04x\n", id);
        }

        return -EIO;
    }

    res = dev_gpo_set(dev, IGPO_LDOLMS_EN, on ? 1 : 0); // Enable LDOs
    if (res)
        return res;

    if (d->ssdr) {
        // Haevy load on 1.8VA
        usleep(100000);
    }
    usleep(1000);
    return 0;
}

static
int _xsdr_pwren_revo(xsdr_dev_t *d, bool on)
{
    return 0;
}

int xsdr_set_lms125vdd(xsdr_dev_t *d, unsigned vdd_mv)
{
    if (d->new_rev && !d->ssdr) {
        return lp8758_vout_set(d->base.lmsstate.dev, d->base.lmsstate.subdev, I2C_BUS_LP8758_FPGA,
                               d->ssdr_pro ? 1 : 2, vdd_mv);
    }

    return -EINVAL;
}

int xsdr_set_vio(xsdr_dev_t *d, unsigned vio_mv)
{
    if (!d->new_rev) {
        if (vio_mv > 3300)
            vio_mv = 3300;
        else if (vio_mv < 1600)
            vio_mv = 1600;

        USDR_LOG("XDEV", USDR_LOG_WARNING, "VIO set to %d mV\n", vio_mv);
        return lp8758_vout_set(d->base.lmsstate.dev, d->base.lmsstate.subdev, I2C_BUS_LP8758_FPGA, 1, vio_mv);
    }

    if (vio_mv > 2100)
        vio_mv = 2100;
    else if (vio_mv < 1600)
        vio_mv = 1600;

    USDR_LOG("XDEV", USDR_LOG_WARNING, "VIO set to %d mV\n", vio_mv);
    return lp8758_vout_set(d->base.lmsstate.dev, d->base.lmsstate.subdev, I2C_BUS_LP8758_FPGA,
                           d->ssdr_pro ? 3 : 1, vio_mv);
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
                          (d->new_rev) ? 0 : 0x01B10D15, 1, &d->base.lmsstate);
    if (res)
        return res;

    d->pwr_en = on;
    return res;
}

int xsdr_usbclk(xsdr_dev_t *d, bool uclk)
{
    // Override for testing purposes
    return dev_gpo_set(d->base.lmsstate.dev, IGPO_USB_CLK_EN, uclk ? 1 : 0);
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

    const uint8_t phycfg_id = hwid & 0xff;
    const bool rx_port_is_1 = ((phycfg_id & PHY_CFG_LML2_IS_RX) != PHY_CFG_LML2_IS_RX);
    const bool tx_mmcm = ((phycfg_id & PHY_CFG_TX_MMCM) == PHY_CFG_TX_MMCM);
    const bool rx_mmcm = ((phycfg_id & PHY_CFG_RX_MMCM) == PHY_CFG_RX_MMCM);

    d->hwid = hwid;
    d->hwchans_rx = 2; // Defaults to MIMO;
    d->hwchans_tx = 2; // Defaults to MIMO;
    d->siso_sdr_active_rx = false;
    d->siso_sdr_active_tx = false;
    d->rx_port_is_1 = rx_port_is_1;
    d->mmcm_rx = rx_mmcm;
    d->mmcm_tx = tx_mmcm;
    d->cfg_srate_siso_rx = 0;
    d->cfg_srate_siso_tx = 0;
    d->dpump = false;
    d->ssdr_pro = false;

    res = lms7002m_init(&d->base, dev, 0, XSDR_INT_REFCLK);
    if (res) {
        return res;
    }

    switch (hwcfg_devid) {
    case XSDR_DEV: d->new_rev = true; d->ssdr = false; break;
    case XTRX_DEV: d->new_rev = false; d->ssdr = false; break;
    case SSDR_DEV: d->new_rev = true; d->ssdr = true; break;
    case SSDRPRO_DEV: d->new_rev = true; d->ssdr = true; d->ssdr_pro = true; break;
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

    res = (res) ? res : _xsdr_mmcm_pd(d);

    if (d->ssdr_pro) {
        res = res ? res : lp8758_vout_set(dev, d->base.lmsstate.subdev, I2C_BUS_LP8758_FPGA, 3, 900);
        res = res ? res : lp8758_vout_ctrl(dev, d->base.lmsstate.subdev, I2C_BUS_LP8758_FPGA, 3, 0, 1);
    } if (d->ssdr) {
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

    // TODO: Properly set mask for A/B channels
    d->base.rx_run[0] = rxen;
    d->base.rx_run[1] = rxen;
    d->base.tx_run[0] = txen;
    d->base.tx_run[1] = txen;

    res = xsdr_rfic_streaming_up(d,
                                 (rxen ? RFIC_LMS7_RX : 0) | (txen ? RFIC_LMS7_TX : 0),
                                 LMS7_CH_AB, 0,
                                 LMS7_CH_AB, 0);


    res = res ? res : _xsdr_calibrate_lml(d);
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
    //xsdr_dev_t *d = (xsdr_dev_t *)param;
    //d->rxdsp_freq_offset = freqoffset;
    //return sfe_rf4_nco_freq(d->base.lmsstate.dev, 0, CSR_RFE4_BASE, freqoffset);
    return -EINVAL;
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
        //res = sfe_rf4_nco_enable(d->base.lmsstate.dev, 0, CSR_RFE4_BASE, func ? true : false, idx);
        res = -EINVAL;
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

int xsdr_gettemp_id(xsdr_dev_t *d, int* id)
{
    if (d->new_rev) {
        return tmp114_devid_get(d->base.lmsstate.dev, 0, I2C_BUS_TMP_114, id);
    } else {
        return tmp108_temp_get(d->base.lmsstate.dev, 0, I2C_BUS_TMP_108, id);
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



