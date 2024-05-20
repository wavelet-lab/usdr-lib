// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "usdr_ctrl.h"
#include <usdr_logging.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#include "../cal/cal_lo_iqimb.h"
#include "../ipblks/streams/sfe_rx_4.h"

#include "../hw/lp8758/lp8758.h"
#include "../hw/tmp114/tmp114.h"
#include "../hw/si5332/si5332.h"
#include "../hw/tps6381x/tps6381x.h"
#include "../ipblks/xlnx_bitstream.h"

#include "../generic_usdr/generic_regs.h"

// Clock configuration
//          rev4/rev3      rev2         rev1
//  port0 - LMS_PLLCLK | RXCLK       | RXCLK
//  port1 - RXCLK      | LMS_PLLCLK  | LMS_PLLCLK
//  port2 - TXCLK      | TXCLK       | TXCLK
//  port3 - MIXER_LO   | MIXER_LO    | MIXER_LO
//  port4 - MGT        | MGT         |  N/A
//  port5 - FPGA_REF   | USB_CLK     |  N/A
//
// RXFE for all revs
//  LNA | range     | balun                | switch pos
//  1   | 0.3 - 2.8 | B0322J5050AHF        | RF1 - 0
//  2   | 1.5 - 3.8 | 3600BL14M050         | RF2 - 1
//  3   | 0.3 - 3.8 | external M.2 port / local mixer
// TXFE for all revs
//  PA  | range     | balun                | switch pos
//  1   |           | B0322J5050AHF        | RF2 - 1
//  2   |           | 3600BL14M050         | RF1 - 0
//
// I2C bus:
//  LP8758
//  SI5332A
//  TPS63811
//  TMP114


enum BUSIDX_m2_lm6_1_rev000 {
    I2C_BUS_LP8758 = 0,
    I2C_BUS_SI5332A = 1,
    I2C_BUS_TPS63811 = 2,
    I2C_BUS_TEMP = 2,

    SPI_LMS6 = 0,
};

// RX chain
// LNA_GAIN -> mixer -> VGA1_GAIN -> lpf -> VGA2_GAIN[1,2] -> adc

// RX gain
enum rxvgas {
    GAIN_TX_VGA2_MIN = 0,
    GAIN_TX_VGA2_MAX = 25,

    GAIN_TX_VGA1_MIN = -35,
    GAIN_TX_VGA1_MAX = -4,

    GAIN_RX_VGA2_MIN = 0,
    GAIN_RX_VGA2_MAX = 60,

    GAIN_RX_VGA1_MIN = 5,
    GAIN_RX_VGA1_MAX = 31,

    GAIN_RX_LNA_MAX = 6,
    GAIN_RX_LNA_MID = 3,
    GAIN_RX_LNA_MIN = 0,
};

enum pllfreq {
    PLL_MIN = 23,
    PLL_MAX = 41,
};

enum {
    USDR_ENABLE_EXT_MIXER = 2,
};

enum sigtype {
    USDR_TX_LO_CHANGED,
    USDR_RX_LO_CHANGED,
    USDR_TX_LNA_CHANGED,
    USDR_RX_LNA_CHANGED,
};

static int8_t _lms6002d_rxvga1_db_to_int(double db)
{
    if (db < 5)
        db = 5;
    else if (db > 30)
        db = 30.17;

    return (int8_t)(127.5 - 127 / pow(10, (db - 5) / 20));
}

#if 0   //unused, DO NOT DELETE
static int get_antenna_cfg_by_name(const char* name, const freq_auto_band_map_t* maps, unsigned max)
{
    for (unsigned i = 0; i < max; i++) {
        if (strcasecmp(name, maps[i].name0) == 0)
            return i;
        if (strcasecmp(name, maps[i].name1) == 0)
            return i;
    }

    return -1;
}
#endif

static int get_antenna_cfg_by_freq(unsigned freq, const freq_auto_band_map_t* maps, unsigned max)
{
    unsigned i;
    for (i = 0; i < max - 1; i++) {
        if (freq < maps[i].stop_freq)
            return i;
    }

    return i;
}

#if 0   //unused, DO NOT DELETE
static int get_antenna_cfg_by_band(unsigned band, const freq_auto_band_map_t* maps, unsigned max)
{
    for (unsigned i = 0; i < max; i++) {
        if (maps[i].band == band)
            return i;
    }

    return -1;
}
#endif

static int dev_gpo_set(lldev_t dev, unsigned bank, unsigned data)
{
    return lowlevel_reg_wr32(dev, 0, 0, ((bank & 0x7f) << 24) | (data & 0xff));
}

static int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, 0, 16 + (bank / 4), data);
}

static int _usdr_set_lna_rx(usdr_dev_t *d, unsigned cfg_idx)
{
    const freq_auto_band_map_t* cfg = &d->cfg_auto_rx[cfg_idx];
    unsigned band = cfg->band;
    unsigned txlbband = 0;
    int res;

    d->rx_cfg_path = cfg_idx;
    /*
    if (d->rx_lna_lb_active) {
        switch (band) {
        case RFE_LNAL: band = RFE_LBL; txlbband = 2; break;
        case RFE_LNAW: band = RFE_LBW; txlbband = 1; break;
        case RFE_LNAH: band = RFE_LBH; txlbband = 1; break;
        }
    }
    */

    USDR_LOG("UDEV", USDR_LOG_INFO, "%s: Set RX band to %d (%s/%s) %s [TXLB:%d => ATEEN=%d,%d]\n",
             lowlevel_get_devname(d->base.dev), band,
             cfg->name0, cfg->name1,
             /* d->rx_lna_lb_active ? "loopback enabled" : */ "",
             txlbband, 0, 0 /* d->trf_lb_atten, d->trf_lb_loss */ );

    res = lms6002d_set_rx_path(&d->lms, ((d->rx_rfic_lna = band)));
    //if (txlbband) {
    //    res = (res) ? res : lms7_trf_set_path(&d->lmsstate, ((d->tx_rfic_band = txlbband)));
    //}
    if (res)
        return res;

    return usdr_set_rx_port_switch(d, /*d->rx_lna_lb_active ? cfg->swlb :*/ cfg->sw);
}


static int _usdr_set_lna_tx(usdr_dev_t *d, unsigned cfg_idx)
{
    const freq_auto_band_map_t* cfg = &d->cfg_auto_tx[cfg_idx];
    unsigned band = cfg->band;
    int res;
    d->tx_cfg_path = cfg_idx;

    USDR_LOG("XDEV", USDR_LOG_INFO, "%s: Set TX band to %d (%s/%s)\n",
             lowlevel_get_devname(d->base.dev), band, cfg->name0, cfg->name1);

//    if (d->rx_lna_lb_active) {
//        res = lms7_rfe_set_path(&d->lmsstate, band == 1 ? RFE_LBW : RFE_LBL,
//                                d->rx_run[0], d->rx_run[1]);
//    }

    res = lms6002d_set_tx_path(&d->lms, ((d->tx_rfic_band = band)));
    if (res)
        return res;

    return usdr_set_tx_port_switch(d, /* d->rx_lna_lb_active ? cfg->swlb :*/ cfg->sw);
}

static int _usdr_pwr_state(usdr_dev_t *d, bool tx, bool enable)
{
    int res = 0;

    if ((tx) && (d->tx_pwren != enable)) {
        res = lms6002d_trf_enable(&d->lms, enable);
        d->tx_pwren = enable;
        USDR_LOG("UDEV", USDR_LOG_INFO, "TX POWER %d\n", d->tx_pwren);
    } else if ((!tx) && (d->rx_pwren != enable)) {
        res = lms6002d_rfe_enable(&d->lms, enable);
        d->rx_pwren = enable;
        USDR_LOG("UDEV", USDR_LOG_INFO, "RX POWER %d\n", d->rx_pwren);
    }

    return res;
}

int _usdr_update_path_rx(usdr_dev_t *d, const char* path)
{
    int res = 0;
#if 0
    int cfgidx;

    if (p == USDR_RX_AUTO) {
        cfgidx = get_antenna_cfg_by_freq(d->rx_lo, d->cfg_auto_rx, USDR_MAX_RX_BANDS);
        USDR_LOG("UDEV", USDR_LOG_INFO, "%s: Auto RX band selection: %s\n",
                 lowlevel_get_devname(d->base.dev), d->cfg_auto_rx[cfgidx].name0);

        res = (res) ? res : _usdr_set_lna_rx(d, cfgidx);
    } else {
        cfgidx = get_antenna_cfg_by_band(
    }
#endif
    return res;
}


static int _usdr_signal_event(usdr_dev_t *d, enum sigtype t)
{
    int res = 0;
    int cfgidx;

    switch (t) {
    case USDR_RX_LO_CHANGED:
        _usdr_pwr_state(d, false, true);
    case USDR_RX_LNA_CHANGED:
        if (d->rx_rfic_path == USDR_RX_AUTO) {
            cfgidx = get_antenna_cfg_by_freq(d->rx_lo, d->cfg_auto_rx, USDR_MAX_RX_BANDS);
            USDR_LOG("UDEV", USDR_LOG_INFO, "%s: Auto RX band selection: %s\n",
                        lowlevel_get_devname(d->base.dev), d->cfg_auto_rx[cfgidx].name0);

            res = (res) ? res : _usdr_set_lna_rx(d, cfgidx);
        }
        return res;
    case USDR_TX_LO_CHANGED:
        _usdr_pwr_state(d, true, true);
    case USDR_TX_LNA_CHANGED:
        if (d->tx_rfic_path == USDR_TX_AUTO) {
            cfgidx = get_antenna_cfg_by_freq(d->tx_lo, d->cfg_auto_tx, USDR_MAX_TX_BANDS);
            USDR_LOG("UDEV", USDR_LOG_INFO, "%s: Auto TX band selection: %s\n",
                        lowlevel_get_devname(d->base.dev), d->cfg_auto_tx[cfgidx].name0);

            res = (res) ? res : _usdr_set_lna_tx(d, cfgidx);
        }
        return res;
    }

    return 0;
}


#if 0
int usdr_rfic_fe_set_freq(struct usdr_dev *d,
                          unsigned type,
                          double freq,
                          double *actualfreq)
{
    int res = 0;
    if (!(type & (RFIC_LMS6_TX | RFIC_LMS6_RX)))
        return -EINVAL;

    if (type & RFIC_LMS6_TX) {
        res = (res) ? res : lms6002d_tune_pll(&d->lms, true, freq);
    }
    if (type & RFIC_LMS6_RX) {
        res = (res) ? res : lms6002d_tune_pll(&d->lms, false, freq);
    }
    return res;
}
#endif

int usdr_set_samplerate_ex(struct usdr_dev *d,
                           unsigned rxrate, unsigned txrate,
                           unsigned adcclk, unsigned dacclk,
                           unsigned flags)
{
    lldev_t dev = d->base.dev;
    unsigned rate = (rxrate == 0) ? txrate : rxrate;
    unsigned freq = rate << 1; //Link speed x2 sample rate
    struct si5332_layout_info nfo = { d->fref, freq };
    int res;

    res = si5332_set_layout(dev, 0, I2C_BUS_SI5332A, &nfo, d->hw_board_rev == USDR_REV_3 ? false : true, &d->si_vco_freq);
    if (res)
        return res;

    // TODO: Add ability to alter it
    d->mixer_lo = d->si_vco_freq / 8;

    // FIME: Remove DEBUG
    // check clocks
    for (unsigned i = 0; i < 10; i++) {
        unsigned v = 0, q = 0, t = 0;
        usleep(10200);
        dev_gpi_get32(dev, IGPI_USBC, &q);
        dev_gpi_get32(dev, IGPI_RXCLK, &v);
        dev_gpi_get32(dev, IGPI_TXCLK, &t);

        USDR_LOG("UDEV", USDR_LOG_INFO, "M2_LM6_1: MCLK=%x UCLK=%x TCLK=%x -- %d / %d / %d", v, q, t,
                 v & 0xfffffff, q & 0xfffffff, t & 0xfffffff);
    }

    return 0;
}


int usdr_rfic_streaming_up(struct usdr_dev *d, unsigned dir)
{
    int res = 0;

    if (dir & RFIC_LMS6_TX) {
        d->tx_run = true;
        res = (res) ? res : _usdr_pwr_state(d, true, true);
    }

    if (dir & RFIC_LMS6_RX) {
        d->rx_run = true;
        res = (res) ? res : _usdr_pwr_state(d, false, true);
    }

    // TODO enable clock
    //return lms6002d_rf_enable(&d->lms, d->tx_run, d->rx_run);
    return res;
}

int usdr_rfic_streaming_down(struct usdr_dev *d, unsigned dir)
{
    int res = 0;

    if (dir & RFIC_LMS6_TX) {
        d->tx_run = false;
        res = (res) ? res : _usdr_pwr_state(d, true, false);
    }

    if (dir & RFIC_LMS6_RX) {
        d->rx_run = false;
        res = (res) ? res : _usdr_pwr_state(d, false, false);
    }

    // TODO disable clock
    //return lms6002d_rf_enable(&d->lms, d->tx_run, d->rx_run);
    return res;
}


int usdr_ctor(lldev_t dev, subdev_t sub, struct usdr_dev *d)
{
    memset(d, 0, sizeof(struct usdr_dev));
    d->base.dev = dev;
    d->subdev = sub;

    d->rx_cfg_path = 255;
    d->tx_cfg_path = 255;

    return 0;
}

int usdr_i2c_addr_ext_set(struct usdr_dev *d, uint8_t addr)
{
    return lowlevel_reg_wr32(d->base.dev, d->subdev, M2PCI_REG_STAT_CTRL,
                             MAKE_I2C_LUT(0x80 | addr, I2C_DEV_TMP114NB, I2C_DEV_CLKGEN, I2C_DEV_PMIC_FPGA));
}

int usdr_init(struct usdr_dev *d, int ext_clk, unsigned ext_fref)
{
    lldev_t dev = d->base.dev;
    int res;
    uint16_t rev = 0xffff;
    uint32_t did;
    uint32_t uaccess;

    if (ext_clk && ext_fref) {
        if ((ext_fref < 23e6) || (ext_fref > 41e6)) {
            USDR_LOG("XDEV", USDR_LOG_WARNING, "Optimal LMS6002D reference clock is in 23..41 Mhz\n");
        }
    }

    // Set default internal ref
    d->refclkpath = 0;
    d->fref = (ext_clk && ext_fref) ? ext_fref : 26000000;

    // Antenna band switch configuration
    d->cfg_auto_rx[0].stop_freq = 230e6;
    d->cfg_auto_rx[0].band = RXPATH_LNA3;
    d->cfg_auto_rx[0].sw = 1;
    d->cfg_auto_rx[0].swlb = 1;
    strncpy(d->cfg_auto_rx[0].name0, "LNAL", sizeof(d->cfg_auto_rx[0].name0));
    strncpy(d->cfg_auto_rx[0].name1, "EXT", sizeof(d->cfg_auto_rx[0].name0));

    d->cfg_auto_rx[1].stop_freq = 2800e6;
    d->cfg_auto_rx[1].band = RXPATH_LNA1;
    d->cfg_auto_rx[1].sw = 0;
    d->cfg_auto_rx[1].swlb = 0;
    strncpy(d->cfg_auto_rx[1].name0, "LNAW", sizeof(d->cfg_auto_rx[1].name0));
    strncpy(d->cfg_auto_rx[1].name1, "W", sizeof(d->cfg_auto_rx[1].name0));

    d->cfg_auto_rx[2].stop_freq = 4000e6;
    d->cfg_auto_rx[2].band = RXPATH_LNA2;
    d->cfg_auto_rx[2].sw = 1;
    d->cfg_auto_rx[2].swlb = 1;
    strncpy(d->cfg_auto_rx[2].name0, "LNAH", sizeof(d->cfg_auto_rx[2].name0));
    strncpy(d->cfg_auto_rx[2].name1, "H", sizeof(d->cfg_auto_rx[2].name0));

    // TX configuration
    d->cfg_auto_tx[0].stop_freq = 2800e6;
    d->cfg_auto_tx[0].band = TXPATH_PA1;
    d->cfg_auto_tx[0].sw = 1;
    d->cfg_auto_tx[0].swlb = 1;
    strncpy(d->cfg_auto_tx[0].name0, "W", sizeof(d->cfg_auto_tx[0].name0));
    strncpy(d->cfg_auto_tx[0].name1, "B1", sizeof(d->cfg_auto_tx[0].name0));

    d->cfg_auto_tx[1].stop_freq = 4000e6;
    d->cfg_auto_tx[1].band = TXPATH_PA2;
    d->cfg_auto_tx[1].sw = 0;
    d->cfg_auto_tx[1].swlb = 0;
    strncpy(d->cfg_auto_tx[1].name0, "H", sizeof(d->cfg_auto_tx[0].name0));
    strncpy(d->cfg_auto_tx[1].name1, "B2", sizeof(d->cfg_auto_tx[0].name0));

    // Get HWID
    res = dev_gpi_get32(dev, IGPI_USR_ACCESS2, &uaccess);
    if (res)
        return res;

    res = dev_gpi_get32(dev, IGPI_HWID, &d->hwid);
    if (res)
        return res;

    d->hw_board_hasmixer = false;
    d->hw_board_rev = (d->hwid >> 8) & 0xff;
    USDR_LOG("XDEV", USDR_LOG_WARNING, "HWID %08x USDR Board rev.%d Device `%s` FirmwareID %08x (%lld)\n",
             d->hwid, d->hw_board_rev, lowlevel_get_devname(dev), uaccess, (long long)get_xilinx_rev_h(uaccess));

    switch (d->hw_board_rev) {
    case USDR_REV_1:
    case USDR_REV_2:
    case USDR_REV_3:
        d->hw_board_hasmixer = true;
        break;
    default:
        USDR_LOG("XDEV", USDR_LOG_ERROR, "This hardware board revision isn't supported by host support libraries; please update the software\n");
        if (getenv("USDR_BARE_DEV")) {
            d->hw_board_rev = USDR_REV_UNKNOWN;
        } else {
            return -ENOTSUP;
        }
    }

    if (d->hw_board_hasmixer) {
        d->cfg_auto_rx[0].sw |= USDR_ENABLE_EXT_MIXER;
    }

    // Reset LMS
    res = dev_gpo_set(dev, IGPO_LMS_RST, 0);
    if (res)
        return res;

    if (getenv("USDR_BARE_DEV")) {
        dev_gpo_set(dev, IGPO_LED, 1);
        USDR_LOG("UDEV", USDR_LOG_WARNING, "USDR_BARE_DEV is set, skiping initalization!\n");
        return 0;
    }

    res = usdr_i2c_addr_ext_set(d, 0);

    if (res)
        return res;

    if (d->hw_board_rev == USDR_REV_3) {
        int devid = ~0u;

        // Check TMP114
        res = tmp114_devid_get(dev, d->subdev, I2C_BUS_TEMP, &devid);
        if (res)
            return res;

        if (devid != 0x1114) {
            USDR_LOG("UDEV", USDR_LOG_ERROR, "TMP114 DEVID=%x\n", devid);
        }
    }

    res = lp8758_get_rev(dev, d->subdev, I2C_BUS_LP8758, &rev);
    if (res)
        return res;

    if (rev != 0xe001) {
        USDR_LOG("UDEV", USDR_LOG_ERROR, "PMIC ID is incorrect: %04x\n", rev);
        return -EFAULT;
    }

    res = res ? res : lp8758_ss(dev, d->subdev, I2C_BUS_LP8758, 0);

    // Enable SS, tshutoff = 125C
    // res = lp8758_reg_set(dev, 0, I2C_BUS_LP8758, 0x17, 0x07);
    //if (res)
    //   return res;

    // Set Vgpio to 1.8V (but for REV1 set to 3.3 to feed LMS
    //res = lp8758_vout_set(dev, d->subdev, I2C_BUS_LP8758, 1,
    //                      d->hw_board_rev == USDR_REV_1 ? 3360 : 1800);

    res = res ? res : lp8758_vout_set(dev, d->subdev, I2C_BUS_LP8758, 1, 1800);
    // Increase 1.8V rail to get more samplerate
    // res = lp8758_vout_set(dev, d->subdev, I2C_BUS_LP8758, 3, 2100);
    // if (res)
    //     return res;

    res = res ? res : lp8758_vout_ctrl(dev, d->subdev, I2C_BUS_LP8758, 0, 1, 1); //1v0 -- less affected
    res = res ? res : lp8758_vout_ctrl(dev, d->subdev, I2C_BUS_LP8758, 1, 1, 1); //2v5 -- less affected
    res = res ? res : lp8758_vout_ctrl(dev, d->subdev, I2C_BUS_LP8758, 2, 1, 1); //1v2
    res = res ? res : lp8758_vout_ctrl(dev, d->subdev, I2C_BUS_LP8758, 3, 1, 1); //1v8

    if (res)
        return res;

    // Turn on force PWM
    res = lowlevel_reg_wr32(d->base.dev, d->subdev, M2PCI_REG_STAT_CTRL,
                            MAKE_I2C_LUT(0x80, I2C_DEV_DCDCBOOST, I2C_DEV_CLKGEN, I2C_DEV_PMIC_FPGA));
    if (res)
        return res;

    res = tps6381x_init(dev, d->subdev, I2C_BUS_TPS63811, true, true, 3450);
    if (res) {
        return res;
    }

    res = usdr_i2c_addr_ext_set(d, 0);
    if (res)
        return res;

    usleep(10000);

    // Turn on clock buffers
    res = si5332_init(dev, 0, I2C_BUS_SI5332A, 1,
                      ext_clk == -1 ? (d->hw_board_rev == USDR_REV_3 ? false : true) : ext_clk,
                      d->hw_board_rev == USDR_REV_3 ? true : false);

    if (d->hw_board_rev == USDR_REV_3) {
        res = dev_gpo_set(dev, IGPO_ENABLE_OSC, ext_clk == 1 ? 0 : 1);
        usleep(1000);
    }

    // Wait for clock to settle
    usleep(10000);

    res = res ? res : dev_gpo_set(dev, IGPO_BOOSTER, 1);
    res = res ? res : dev_gpo_set(dev, IGPO_LED, 1);
    res = res ? res : dev_gpo_set(dev, IGPO_LMS_RST, 1);
    if (res)
        goto fail;

    // Read rfic id
    res = lowlevel_spi_tr32(dev, d->subdev, SPI_LMS6, 0x0400, &did);
    if (res)
        goto fail;

    USDR_LOG("UDEV", USDR_LOG_INFO, "M2_LM6_1: RFIC %04x\n", did);
    usleep(1000);

    res = lms6002d_create(dev, 0, SPI_LMS6, &d->lms);
    if (res)
        goto fail;

    // TODO: Apply si div
    d->lms.fref = d->fref;

    res = res ? res : lms6002d_trf_enable(&d->lms, false);
    res = res ? res : lms6002d_rfe_enable(&d->lms, false);

    res = res ? res : dev_gpo_set(dev, IGPO_RXMIX_EN, 0);

    res = res ? res : usdr_set_rx_port_switch(d, d->cfg_auto_rx[1].sw);
    res = res ? res : usdr_set_tx_port_switch(d, d->cfg_auto_tx[0].sw);
    res = res ? res : lms6002d_set_rx_path(&d->lms, d->cfg_auto_rx[1].band);
    res = res ? res : lms6002d_set_tx_path(&d->lms, d->cfg_auto_tx[0].band);

    res = res ? res : dev_gpo_set(dev, IGPO_DCCORR, 1);
    return res;

fail:
    dev_gpo_set(dev, IGPO_LED, 0);
    dev_gpo_set(dev, IGPO_LMS_RST, 0);
    dev_gpo_set(dev, IGPO_BOOSTER, 0);
    return res;
}

int usdr_dtor(struct usdr_dev *d)
{
    lldev_t dev = d->base.dev;

    dev_gpo_set(dev, IGPO_LMS_RST, 0);
    dev_gpo_set(dev, IGPO_LED, 0);
    dev_gpo_set(dev, IGPO_BOOSTER, 0);

    USDR_LOG("UDEV", USDR_LOG_INFO, "turnoff\n");
    return 0;
}

// We need to enable RX RF to get forward clocking
#if 0
int usdr_pwren(struct usdr_dev *d, bool on)
{
    // TODO RX / TX selection
    lms6002d_rf_enable(&d->lms, false, on);
    // TX off
    lms6002d_rf_enable(&d->lms, true, on /* false */);
    return 0;
}
#endif


static
int _usdr_lms6002_dc_calib(struct usdr_dev *d)
{
    int res;

    res = lms6002d_cal_lpf(&d->lms);
    if (res)
        return res;

    for (unsigned i = 0; i < 16; i++) {
        res = lms6002d_cal_lpf_bandwidth(&d->lms, i);
        if (res)
            return res;
    }

    res = lms6002d_cal_txrxlpfdc(&d->lms, true);
    if (res)
        return res;

    res = lms6002d_set_rx_extterm(&d->lms, true);
    if (res)
        return res;

    //TODO Set path to 1

    res = lms6002d_cal_txrxlpfdc(&d->lms, false);
    if (res)
        return res;
    res = lms6002d_cal_vga2(&d->lms);
    if (res)
        return res;

    res = lms6002d_set_rx_extterm(&d->lms, false);
    if (res)
        return res;

    return 0;
}

int usdr_rfic_fe_set_freq(struct usdr_dev *d,
                          bool dir_tx,
                          double freq,
                          double *actualfreq)
{
    if (actualfreq) *actualfreq = freq;
   // txrx_pll_tune(&d->lms, dir_tx ? 0x10 : 0x20, d->fref, freq);
   // return 0;

    if (dir_tx) {
        d->tx_lo = freq;
    } else {
        d->rx_lo = freq;
    }

    // Apply automatic switch, turn on pwr for RX or TX
    _usdr_signal_event(d, dir_tx ? USDR_TX_LO_CHANGED : USDR_RX_LO_CHANGED);

    if (d->mexir_en && !dir_tx) {
        // upconverter mixer
        freq += d->mixer_lo;
    }
    d->rfic_rx_lo = freq;

    USDR_LOG("XDEV", USDR_LOG_INFO, "%s: FE_FREQ orig=%u RFIC=%u\n",
             lowlevel_get_devname(d->base.dev), d->rx_lo, d->rfic_rx_lo);

    return lms6002d_tune_pll(&d->lms, dir_tx, freq);
}


int usdr_rfic_bb_set_badwidth(struct usdr_dev *d,
                              bool dir_tx,
                              unsigned bw,
                              unsigned* actualbw)
{
    if (actualbw) *actualbw = bw;
    return lms6002d_set_bandwidth(&d->lms, dir_tx, bw);
}

static
int _usdr_gain_clamp(int gain, int gain_min, int gain_max)
{
    if (gain < gain_min) gain = gain_min;
    else if (gain > gain_max) gain = gain_max;
    return gain;
}

int usdr_rfic_set_gain(struct usdr_dev *d,
                       unsigned gain_type,
                       int gain,
                       int *actualgain)
{
    int res;
    int ngain;
    int8_t val;

    switch (gain_type) {
    case GAIN_RX_LNA:
        if (gain >= GAIN_RX_LNA_MAX) {
            ngain = GAIN_RX_LNA_MAX;
            val = RXLNAGAIN_MAX;
        } else if (gain >=  GAIN_RX_LNA_MID) {
            ngain = GAIN_RX_LNA_MID;
            val = RXLNAGAIN_MID;
        } else {
            ngain = GAIN_RX_LNA_MIN;
            val = RXLNAGAIN_BYPASS;
        }
        res = lms6002d_set_rxlna_gain(&d->lms, val);
        break;

    case GAIN_RX_VGA1:
        ngain = _usdr_gain_clamp(gain, GAIN_RX_VGA1_MIN, GAIN_RX_VGA1_MAX);
        val = _lms6002d_rxvga1_db_to_int(ngain);
        res = lms6002d_set_rxvga1_gain(&d->lms, val);
        break;

    case GAIN_RX_VGA2:
        ngain = _usdr_gain_clamp(gain, GAIN_RX_VGA2_MIN, GAIN_RX_VGA2_MAX);
        res = lms6002d_set_rxvga2_gain(&d->lms, ngain/3);
        break;

    case GAIN_TX_VGA1:
        ngain = _usdr_gain_clamp(gain, GAIN_TX_VGA1_MIN, GAIN_TX_VGA1_MAX);
        res = lms6002d_set_txvga1_gain(&d->lms, ngain - GAIN_TX_VGA1_MIN);
        break;

    case GAIN_TX_VGA2:
        ngain = _usdr_gain_clamp(gain, GAIN_TX_VGA2_MIN, GAIN_TX_VGA2_MAX);
        res = lms6002d_set_txvga2_gain(&d->lms, ngain);
        break;

    default:
        return -EINVAL;
    }

    if (actualgain) *actualgain = ngain;
    return res;
}


int usdr_rfic_fe_set_rxlna(struct usdr_dev *d,
                           const char *lna)
{
    // return -EINVAL;
    USDR_LOG("UDEV", USDR_LOG_ERROR, "usdr_rfic_fe_set_rxlna not implimented!!!\n");
    return 0;
}


int usdr_set_rx_port_switch(struct usdr_dev *d, unsigned path)
{
    USDR_LOG("UDEV", USDR_LOG_INFO, "RXSW:%d EXT_MIXER_EN:%d\n", path & 1, path >> 1);
    int res = 0;
    res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_RXSW, path & 1);
    res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_RXMIX_EN, path >> 1);

    d->mexir_en = (path >> 1) ? true : false;
    return res;
}

int usdr_set_tx_port_switch(struct usdr_dev *d, unsigned path)
{
    USDR_LOG("UDEV", USDR_LOG_INFO, "TXSW:%d\n", path);
    return dev_gpo_set(d->base.dev, IGPO_TXSW, path);
}

int usdr_calib_dc(struct usdr_dev *d, bool rx)
{
    int res;

   // res = lms6002d_cal_txrxlpfdc(&d->lms, false);
   // res = lms6002d_cal_vga2(&d->lms);

    res = _usdr_lms6002_dc_calib(d);

    USDR_LOG("UDEV", USDR_LOG_INFO, "DC - Calibration done\n");
    return res;
}


int usdr_gettemp(struct usdr_dev *d, int* temp256)
{
    return tmp114_temp_get(d->base.dev, 0, I2C_BUS_TEMP, temp256);
}
