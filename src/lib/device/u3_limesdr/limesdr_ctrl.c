// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <string.h>

#include "limesdr_ctrl.h"
#include <usdr_logging.h>

#define LMS7_BUS_ADDR 0

#define ABS(x) (((x) > 0) ? (x) : -(x))

// TODO: Rename these magic constants
enum limesdr_fpga_registers {
    // Direct clocking
    LIMESDR_REG_0003 = 0x0003,
    LIMESDR_REG_0005 = 0x0005,

    // Streaming
    LIMESDR_REG_0007 = 0x0007,
    LIMESDR_REG_0008 = 0x0008,
    LIMESDR_REG_0009 = 0x0009,
    LIMESDR_REG_000A = 0x000A,

    LIMESDR_REG_0017 = 0x0017,

    // PLL
    LIMESDR_REG_BUSY = 0x0021, // former busyAddr
    LIMESDR_REG_0023 = 0x0023,
    LIMESDR_REG_0024 = 0x0024,
    LIMESDR_REG_0025 = 0x0025,

    // Frequency detector
    LIMESDR_REG_0061 = 0x0061,
    LIMESDR_REG_0063 = 0x0063,
    LIMESDR_REG_0065 = 0x0065,

    LIMESDR_REG_0072 = 0x0072,
    LIMESDR_REG_0073 = 0x0073,

    LIMESDR_REG_FFFF = 0xFFFF,
};

enum limesdr_fpga_reg_0x000a {
    RX_EN = BIT(0), //controls both receiver and transmitter
    TX_EN = BIT(1), //used for wfm playback from fpga
    STREAM_LOAD = BIT(2),
};

enum limesdr_fpga_reg_0x0009 {
    SMPL_NR_CLR = BIT(0), // rising edge clears
    TXPCT_LOSS_CLR = BIT(1), // 0 - normal operation, 1-clear
};

enum {
    PLLCFG_START = 0x1,
    PHCFG_START = 0x2,
    PLLRST_START = 0x4,
    PHCFG_UPDN = 1 << 13,
    PHCFG_MODE = 1 << 14,
};


static
const lms7002m_lml_map_t _limesdr_lml_portcfg(bool UNUSED rx, unsigned UNUSED chs, unsigned UNUSED flags, bool UNUSED no_siso_map)
{
    // During SISO DDR mode only 0 and 1 make sense
    static const lms7002m_lml_map_t diqarray[] = {
        {{ LML_AI, LML_AQ, LML_BI, LML_BQ }}, // Normal
        {{ LML_AQ, LML_AI, LML_BQ, LML_BI }}, // Swap IQ
    };

    return diqarray[0];
}


static int limesdr_detect_refclk(limesdr_dev_t *d, unsigned fx3Clk, unsigned *fref);
static int limesdr_set_direct_clocking(limesdr_dev_t *d, int clockIndex);

static
int limesdr_write_reg(limesdr_dev_t* d, uint16_t addr, uint16_t value)
{
    return lowlevel_reg_wr16(d->base.lmsstate.dev, d->base.lmsstate.subdev, addr, value);
}

static
int limesdr_read_reg(limesdr_dev_t* d, uint16_t addr)
{
    uint16_t val;
    int res = lowlevel_reg_rd16(d->base.lmsstate.dev, d->base.lmsstate.subdev, addr, &val);
    if (res) {
        return res;
    }
    return val;
}


static int _limesdr_antenna_port_switch(lms7002_dev_t* dev, int direction, unsigned sw)
{
    limesdr_dev_t* d = container_of(dev, limesdr_dev_t, base);
    int reg17 = limesdr_read_reg(d, LIMESDR_REG_0017);
    if (reg17 < 0)
        return reg17;

    uint16_t wr_reg17;
    if (direction == RFIC_LMS7_RX) {
        wr_reg17 = (reg17 & ~(3 << 8)) | (sw << 8);
    } else {
        wr_reg17 = (reg17 & ~(3 << 12)) | (sw << 12);
    }

    USDR_LOG("LIME", USDR_LOG_ERROR, "RF PATH %04x\n", wr_reg17);
    return limesdr_write_reg(d, LIMESDR_REG_0017, wr_reg17);
}


int limesdr_ctor(lldev_t dev, limesdr_dev_t *d)
{
    memset(d, 0, sizeof(limesdr_dev_t));
    d->base.lmsstate.dev = dev;
    d->base.on_ant_port_sw = &_limesdr_antenna_port_switch;
    d->base.on_get_lml_portcfg = &_limesdr_lml_portcfg;

    return 0;
}

int limesdr_init(limesdr_dev_t *d)
{
    int res;
    // Autodetect ref frequency
    lldev_t dev = d->base.lmsstate.dev;
    unsigned refclock;
    uint32_t ver;

    d->base.cfg_auto_rx[0].stop_freq = 1700e6;
    d->base.cfg_auto_rx[0].band = RFE_LNAW;
    d->base.cfg_auto_rx[0].sw = 2;
    d->base.cfg_auto_rx[0].swlb = 1;
    strncpy(d->base.cfg_auto_rx[0].name0, "LNAW", sizeof(d->base.cfg_auto_rx[0].name0));
    strncpy(d->base.cfg_auto_rx[0].name1, "W", sizeof(d->base.cfg_auto_rx[0].name1));

    d->base.cfg_auto_rx[1].stop_freq = 4000e6;
    d->base.cfg_auto_rx[1].band = RFE_LNAH;
    d->base.cfg_auto_rx[1].sw = 1;
    d->base.cfg_auto_rx[1].swlb = 2;
    strncpy(d->base.cfg_auto_rx[1].name0, "LNAH", sizeof(d->base.cfg_auto_rx[1].name0));
    strncpy(d->base.cfg_auto_rx[1].name1, "H", sizeof(d->base.cfg_auto_rx[1].name1));

    d->base.cfg_auto_rx[2].stop_freq = 4100e6;
    d->base.cfg_auto_rx[2].band = RFE_LNAL;
    d->base.cfg_auto_rx[2].sw = 0;
    d->base.cfg_auto_rx[2].swlb = 0;
    strncpy(d->base.cfg_auto_rx[2].name0, "LNAL", sizeof(d->base.cfg_auto_rx[2].name0));
    strncpy(d->base.cfg_auto_rx[2].name1, "L", sizeof(d->base.cfg_auto_rx[2].name1));


    d->base.cfg_auto_tx[0].stop_freq = 2000e6;
    d->base.cfg_auto_tx[0].band = 2;
    d->base.cfg_auto_tx[0].sw = 2;
    d->base.cfg_auto_tx[0].swlb = 1;
    strncpy(d->base.cfg_auto_tx[0].name0, "W", sizeof(d->base.cfg_auto_tx[0].name0));
    strncpy(d->base.cfg_auto_tx[0].name1, "B2", sizeof(d->base.cfg_auto_tx[0].name1));

    d->base.cfg_auto_tx[1].stop_freq = 4000e6;
    d->base.cfg_auto_tx[1].band = 1;
    d->base.cfg_auto_tx[1].sw = 1;
    d->base.cfg_auto_tx[1].swlb = 2;
    strncpy(d->base.cfg_auto_tx[1].name0, "H", sizeof(d->base.cfg_auto_tx[1].name0));
    strncpy(d->base.cfg_auto_tx[1].name1, "B1", sizeof(d->base.cfg_auto_tx[1].name1));

    res = limesdr_detect_refclk(d, 100e6, &refclock);
    if (res)
        return res;

    // LMS7 RESET
    res = lowlevel_get_ops(d->base.lmsstate.dev)->ls_op(d->base.lmsstate.dev,
                                                        d->base.lmsstate.subdev,
                                                        USDR_LSOP_CUSTOM_CMD, 0,
                                                        0, NULL, 0, NULL);
    if (res)
        return res;

    res = lms7002m_init(&d->base, dev, 0, refclock);
    if (res)
        return res;

    res = lowlevel_spi_tr32(dev, d->base.lmsstate.subdev, LMS7_BUS_ADDR, 0x002f0000, &ver);
    if (res)
        return res;

    if (ver != 0x3841) {
        USDR_LOG("LIME", USDR_LOG_ERROR, "LMS7 REV %04x\n", ver);
        return -EIO;
    }

    res = lms7002m_create(d->base.lmsstate.dev, d->base.lmsstate.subdev, LMS7_BUS_ADDR, 0, 0,
                          &d->base.lmsstate);
    if (res)
        return res;

    res = limesdr_stop_streaming(d);
    if (res)
        return res;

    return 0;
}

int limesdr_dtor(limesdr_dev_t *d)
{
    // TODO
    return 0;
}



int limesdr_start_streaming(limesdr_dev_t *d)
{
    int interface_ctrl_000A = limesdr_read_reg(d, LIMESDR_REG_000A);
    if (interface_ctrl_000A < 0)
        return interface_ctrl_000A;
    uint32_t value = RX_EN;

    USDR_LOG("LIME", USDR_LOG_ERROR, "Limesdr set start streaming %04x\n", interface_ctrl_000A | value);
    return limesdr_write_reg(d, LIMESDR_REG_000A, interface_ctrl_000A | value);
}

int limesdr_stop_streaming(limesdr_dev_t *d)
{
    int interface_ctrl_000A = limesdr_read_reg(d, LIMESDR_REG_000A);
    if (interface_ctrl_000A < 0)
        return interface_ctrl_000A;
    uint32_t value = ~(RX_EN | TX_EN);
    return limesdr_write_reg(d, LIMESDR_REG_000A, interface_ctrl_000A & value);
}

int limesdr_reset_timestamp(limesdr_dev_t *d)
{
    int res = 0;
#ifndef NDEBUG
    int interface_ctrl_000A = limesdr_read_reg(d, LIMESDR_REG_000A);
    if (interface_ctrl_000A < 0)
        return 0;

    if ((interface_ctrl_000A & RX_EN)) {
        USDR_LOG("LIME", USDR_LOG_ERROR, "Streaming must be stopped to reset timestamp\n");
        return -EPERM;
    }

#endif // NDEBUG
    //reset hardware timestamp to 0
    int interface_ctrl_0009 = limesdr_read_reg(d, LIMESDR_REG_0009);
    if (interface_ctrl_0009 < 0)
        return 0;
    uint32_t value = (TXPCT_LOSS_CLR | SMPL_NR_CLR);
    res = res ? res : limesdr_write_reg(d, LIMESDR_REG_0009, interface_ctrl_0009 & ~(value));
    res = res ? res : limesdr_write_reg(d, LIMESDR_REG_0009, interface_ctrl_0009 | value);
    res = res ? res : limesdr_write_reg(d, LIMESDR_REG_0009, interface_ctrl_0009 & ~value);
    return res;
}


int limesdr_set_direct_clocking(limesdr_dev_t *d, int clockIndex)
{
    int drct_clk_ctrl_0005 = limesdr_read_reg(d, LIMESDR_REG_0005);
    if (drct_clk_ctrl_0005 < 0)
        return drct_clk_ctrl_0005;

    USDR_LOG("LIME", USDR_LOG_INFO, "Limesdr set direct clocking for %d\n", clockIndex);
    return limesdr_write_reg(d, LIMESDR_REG_0005, drct_clk_ctrl_0005 | (1 << clockIndex));
}


int limesdr_detect_refclk(limesdr_dev_t *d, unsigned fx3Clk, unsigned *fref)
{
    const unsigned fx3Cnt = 16777210;
    const unsigned clkTbl[] = { 10e6, 30.72e6, 38.4e6, 40e6, 52e6 };
    uint32_t est;
    int res = 0;
    unsigned j;

    res = res ? res : limesdr_write_reg(d, LIMESDR_REG_0061, 0);
    res = res ? res : limesdr_write_reg(d, LIMESDR_REG_0063, 0);
    res = res ? res : limesdr_write_reg(d, LIMESDR_REG_0061, 4);
    if (res)
        return res;

    for (j = 0; j < 10; j++) {
        int completed = limesdr_read_reg(d, LIMESDR_REG_0065);
        if (completed < 0)
            return completed;
        if (completed & 0x4)
            break;

        usleep(50000);
    }
    if (j == 100) {
        return -EIO;
    }

    res = lowlevel_reg_rd32(d->base.lmsstate.dev, d->base.lmsstate.subdev, LIMESDR_REG_0072, &est);
    if (res)
        return res;

    unsigned count = (uint64_t)est * fx3Clk / fx3Cnt; // Estimate ref clock based on FX3 Clock
    unsigned i = 0;
    unsigned delta = 100e6;

    while (i < sizeof(clkTbl) / sizeof(*clkTbl)) {
        if (delta < ABS(count - clkTbl[i]))
            break;
        else
            delta = ABS(count - clkTbl[i++]);
    }
    USDR_LOG("LIME", USDR_LOG_INFO, "Estimated reference clock %1.4f => %1.2f MHz\n",
             (double)count / 1e6, (double)clkTbl[i - 1] / 1e6);

    if (delta > 1e6)
        return -EFAULT;

    *fref = clkTbl[i - 1];
    return 0;
}


int limesdr_disable_stream(limesdr_dev_t *d)
{
    const unsigned chipId = 0;
    int res = 0;

    res = res ? res : limesdr_write_reg(d, LIMESDR_REG_FFFF, 1 << chipId);
    res = res ? res : limesdr_stop_streaming(d);

    return res;
}

int limesdr_setup_stream(limesdr_dev_t *d, bool iq12bit, bool sisosdr, bool trxpulse)
{
    const unsigned chipId = 0;
    int res = 0;
    uint16_t reg09;

    res = res ? res : limesdr_write_reg(d, LIMESDR_REG_FFFF, 1 << chipId);

    // TODO: AlignRX

    res = res ? res : limesdr_stop_streaming(d);
    res = res ? res : limesdr_reset_timestamp(d);

    res = res ? res : lms7002m_dc_corr(&d->base.lmsstate, P_TXA_I, 127);
    res = res ? res : lms7002m_dc_corr(&d->base.lmsstate, P_TXA_Q, 127);

    res = res ? res : lms7002m_dc_corr_en(&d->base.lmsstate,
                                          d->base.rx_run[0],
                                          d->base.rx_run[1],
                                          d->base.tx_run[0],
                                          d->base.tx_run[1]);

    // TODO: ResetStreamBuffers

    uint16_t mode;
    if (sisosdr) {
        mode = 0x0040;
    } else if (trxpulse) {
        mode = 0x0180;
    } else {
        mode = 0x0100;
    }

    if (iq12bit) {
        mode |= 2;
    }

    res = res ? res : limesdr_write_reg(d, LIMESDR_REG_0008, mode);
    res = res ? res : limesdr_write_reg(d, LIMESDR_REG_0007, 0x1); // Only channel 0 is enabled

    res = res ? res : limesdr_read_reg(d, LIMESDR_REG_0009);
    if (res < 0) {
        return res;
    }
    reg09 = res;
    res = limesdr_start_streaming(d);

    res = res ? res : limesdr_write_reg(d, LIMESDR_REG_0009, reg09 | (5 << 1));
    res = res ? res : limesdr_write_reg(d, LIMESDR_REG_0009, reg09 & ~(5 << 1));

    //if (!align)  lms->ResetLogicregisters();

    USDR_LOG("LIME", USDR_LOG_ERROR, "limesdr_setup_stream\n");
    return res;
}



int limesdr_set_samplerate(limesdr_dev_t *d, unsigned rx_rate, unsigned tx_rate,
                           unsigned adc_rate, unsigned dac_rate)
{
    int res;
    // LML2 - RX
    // LML1 - TX
    res = lms7002m_samplerate(&d->base, rx_rate, tx_rate, adc_rate, dac_rate,
                              XSDR_SR_MAXCONVRATE | XSDR_LML_SISO_DDR_RX | XSDR_LML_SISO_DDR_TX, false);

    // TODO set direct mode or do PLL with phase search
    res = res ? res : limesdr_set_direct_clocking(d, 0);
    res = res ? res : limesdr_set_direct_clocking(d, 1);

    return res;
}


int limesdr_prepare_streaming(limesdr_dev_t *d)
{
    int res = 0;
    if (d->base.cgen_clk == 0) {
        const unsigned default_rate = 1000000;
        res = limesdr_set_samplerate(d, default_rate, default_rate, 0, 0);
    }

    d->base.rx_run[0] = true;
    d->base.rx_run[1] = false;
    d->base.tx_run[0] = true;
    d->base.tx_run[1] = false;

    res = lms7002m_streaming_up(&d->base,
                                 RFIC_LMS7_RX | RFIC_LMS7_TX,
                                 LMS7_CH_A, 0,
                                 LMS7_CH_A, 0);

    USDR_LOG("LIME", USDR_LOG_ERROR, "limesdr_prepare_streaming\n");
    return res;
}
