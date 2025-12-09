// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <usdr_port.h>
#include <usdr_lowlevel.h>
#include <usdr_logging.h>

#include "../device.h"
#include "../device_ids.h"
#include "../device_vfs.h"
#include "../device_names.h"
#include "../device_cores.h"
#include "../device_fe.h"

#include "sync_const.h"
#include "../hw/lmk05318/lmk05318.h"
#include "../hw/lmx2820/lmx2820.h"
#include "../hw/lmx1214/lmx1214.h"
#include "../hw/lmx1204/lmx1204.h"
#include "../hw/lmk1d1208i/lmk1d1208i.h"

// [0] 24bit 20Mhz AD5662  InRef::DAC_REF
// [1] 24bit 20Mhz AD5662  ClockGen::GEN_DC
// [2] 24bit 40Mhz LMX2820 LMXGen0
// [3] 24bit 40Mhz LMX2820 LMXGen1
// [4] 24bit 2Mhz  LMX1204 ClockDistrib::DI0
// [5] 24bit 2Mhz  LMX1214 LoDistrib::DI1
// [6] 24bit 20Mhz AD5662  WR DAC

enum i2c_addrs {
    I2C_ADDR_LMK05318B = 0x64,
    I2C_ADDR_LP87524 = 0x60,
};

enum BUSIDX_I2C {
    I2C_BUS_LMK05318B = MAKE_LSOP_I2C_ADDR(0, 0, I2C_ADDR_LMK05318B),

    I2C_BUS_LMK1D1208I_LCK = MAKE_LSOP_I2C_ADDR(0, 1, 0x68),
    I2C_BUS_LMK1D1208I_LRF = MAKE_LSOP_I2C_ADDR(0, 1, 0x69),

    I2C_BUS_LP87524 = MAKE_LSOP_I2C_ADDR(1, 0, I2C_ADDR_LP87524),

    SPI_INREF_DAC = 0,
    SPI_OCXO_DAC = 1,
    SPI_LMX2820_0 = 2,
    SPI_LMX2820_1 = 3,
    SPI_LMX1204 = 4,
    SPI_LMX1214 = 5,
    SPI_WR_DAC = 6,
    SPI_EXTERNAL = 7, //SPI to external frontend
};

static
const usdr_dev_param_constant_t s_params_pe_sync_rev000[] = {
    { DNLL_SPI_COUNT, 8 },
    { DNLL_I2C_COUNT, 2 },
    { DNLL_SRX_COUNT, 0 },
    { DNLL_STX_COUNT, 0 },
    { DNLL_RFE_COUNT, 0 },
    { DNLL_TFE_COUNT, 0 },
    { DNLL_IDX_REGSP_COUNT, 0 },
    { DNLL_IRQ_COUNT, 10 },

    // low level buses
    { "/ll/irq/0/core", USDR_MAKE_COREID(USDR_CS_AUX, USDR_AC_PIC32_PCI) },
    { "/ll/irq/0/base", REG_INTS },

    { "/ll/spi/0/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_SIMPLE) },
    { "/ll/spi/0/base", REG_SPI0 },
    { "/ll/spi/0/irq",  INT_SPI_0 },
    { "/ll/spi/1/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_SIMPLE) },
    { "/ll/spi/1/base", REG_SPI1 },
    { "/ll/spi/1/irq",  INT_SPI_1 },
    { "/ll/spi/2/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_SIMPLE) },
    { "/ll/spi/2/base", REG_SPI2 },
    { "/ll/spi/2/irq",  INT_SPI_2 },
    { "/ll/spi/3/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_SIMPLE) },
    { "/ll/spi/3/base", REG_SPI3 },
    { "/ll/spi/3/irq",  INT_SPI_3 },
    { "/ll/spi/4/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_SIMPLE) },
    { "/ll/spi/4/base", REG_SPI4 },
    { "/ll/spi/4/irq",  INT_SPI_4 },
    { "/ll/spi/5/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_SIMPLE) },
    { "/ll/spi/5/base", REG_SPI5 },
    { "/ll/spi/5/irq",  INT_SPI_5 },
    { "/ll/spi/6/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_SIMPLE) },
    { "/ll/spi/6/base", REG_SPI6 },
    { "/ll/spi/6/irq",  INT_SPI_6 },
    { "/ll/spi/7/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_CFG_CS8) },
    { "/ll/spi/7/base", REG_SPI_EXT_DATA },
    { "/ll/spi/7/irq",  INT_SPI_EXT },

    { "/ll/i2c/0/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_DI2C_SIMPLE) },
    { "/ll/i2c/0/base", REG_I2C0_DATA },
    { "/ll/i2c/0/irq",  INT_I2C_0 },
    { "/ll/i2c/1/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_DI2C_SIMPLE) },
    { "/ll/i2c/1/base", REG_I2C1_DATA },
    { "/ll/i2c/1/irq",  INT_I2C_1 },

    { "/ll/gpio/0/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_GPIO15_SIMPLE) },
    { "/ll/gpio/0/base", REG_GPIO_CTRL },
    { "/ll/gpio/0/irq",  -1 },

    { "/ll/qspi_flash/base", REG_WR_FLASHSPI_CMD },

    { "/ll/sdr/0/rfic/0", (uintptr_t)"none" },
    { "/ll/device/name",  (uintptr_t)"none"},

    { "/ll/sdr/max_hw_rx_chans",  0 },
    { "/ll/sdr/max_hw_tx_chans",  0 },
    { "/ll/sdr/max_sw_rx_chans",  0 },
    { "/ll/sdr/max_sw_tx_chans",  0 },
};

static int dev_pe_sync_rate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static
const usdr_dev_param_func_t s_fparams_pe_sync_rev000[] = {
    { "/dm/rate/master",        { dev_pe_sync_rate_set, NULL }},
    { "/dm/sdr/channels",       { NULL, NULL }},
};

struct dev_pe_sync {
    device_t base;

    lmk05318_state_t gen;
    lmx2820_state_t lmx0, lmx1;
    lmx1214_state_t lodistr;
    lmx1204_state_t cldistr;
    lmk1d1208i_state_t lmk1d0, lmk1d1;
};

enum dev_gpi {
    IGPI_STAT = 16,
};

enum dev_gpo {
    IGPO_IN_CTRL      = 0,
    IGPO_SY0_CTRL     = 1,
    IGPO_SY1_CTRL     = 2,
    IGPO_GEN_CTRL     = 3,
    IGPO_DISTRIB_CTRL = 4,
    IGPO_SYNC_CTRL    = 5,
    IGPO_LED_CTRL     = 6,
    IGPO_USB2_CFG     = 7,
};

static int dev_gpo_set(lldev_t dev, unsigned bank, unsigned data)
{
    return lowlevel_reg_wr32(dev, 0, 0, ((bank & 0x7f) << 24) | (data & 0xff));
}

static int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, 0, 16 + (bank / 4), data);
}


int dev_pe_sync_rate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return -EINVAL;
}

static void usdr_device_pe_sync_destroy(pdevice_t udev)
{
    struct dev_pe_sync *d = (struct dev_pe_sync *)udev;
    lldev_t dev = d->base.dev;

    dev_gpo_set(dev, IGPO_DISTRIB_CTRL, 0);
    dev_gpo_set(dev, IGPO_SY0_CTRL, 0);
    dev_gpo_set(dev, IGPO_SY1_CTRL, 0);

    usdr_device_base_destroy(udev);
    USDR_LOG("SYNC", USDR_LOG_WARNING, "PESync destroyed");
}

static int i2c_reg_rd8(lldev_t dev, unsigned lsaddr, uint8_t reg, uint8_t* val)
{
    uint8_t addr[1] = { reg };
    return lowlevel_ls_op(dev, 0, USDR_LSOP_I2C_DEV, lsaddr, 1, val, 1, addr);
}

static int usdr_device_pe_sync_initialize(pdevice_t udev, unsigned pcount, const char** devparam, const char** devval)
{
    struct dev_pe_sync *d = (struct dev_pe_sync *)udev;
    lldev_t dev = d->base.dev;
    int res = 0;
    uint32_t v = 0, s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    uint8_t r = 0, r4 = 0, r5 = 0;

    if (getenv("USDR_BARE_DEV")) {
        USDR_LOG("SYNC", USDR_LOG_WARNING, "USDR_BARE_DEV is set, skipping initialization!\n");
        return 0;
    }

    // gpo_in_ctrl[0] --  0 - Disable input 1PPS / 10Mhz buffer and REF ADC for 1PPS
    // gpo_in_ctrl[1] --  0 - external SMA, 1 - feedback from LCK_FB
    // gpo_in_ctrl[2] --  0 - external SMA, 1 - 1PPS from GPS
    // gpo_in_ctrl[3] --  En GPS LDO
    res = res ? res : dev_gpo_set(dev, IGPO_IN_CTRL, 0b1101); // Enable GPS

    // gpo_sy_ctrl*[0] -- LMX2820 LDO Pwr EN
    // gpo_sy_ctrl*[1] -- LMX2820 CE pin
    // gpo_sy_ctrl*[2] -- LMX2820 MUTE pin
    res = res ? res : dev_gpo_set(dev, IGPO_SY0_CTRL, 3); // Enable LMX2820 0
    res = res ? res : dev_gpo_set(dev, IGPO_SY1_CTRL, 3); // Enable LMX2820 1

    // gpo_gen_ctrl[0] -- En LDO for LMK05318B
    // gpo_gen_ctrl[1] -- PDN for LMK05318B
    // gpo_gen_ctrl[2] -- En LDO for OCXO and OCXO DAC
    // gpo_gen_ctrl[3] -- En distribution buffer REFCLK
    // gpo_gen_ctrl[4] -- En distribution buffer 1PPS
    // gpo_gen_ctrl[5] -- clk_gpio[0]
    // gpo_gen_ctrl[6] -- clk_gpio[1]
    // gpo_gen_ctrl[7] -- clk_gpio[2]
    res = res ? res : dev_gpo_set(dev, IGPO_GEN_CTRL, (1 << 0) | (1 << 1) | (1 << 2) | (1 << 5) | (1 << 3) | (1 << 4));

    // gpo_distrib_ctrl[0]   -- En global LDO for all distribution logic
    // gpo_distrib_ctrl[2:1] -- En LDO for LMX1204/LMX1214
    // gpo_distrib_ctrl[3]   -- 0 - buffers LMK1D1208I disable, 1 - en
    // gpo_distrib_ctrl[4]   -- En LDO FCLK4..0 CMOS buffers
    // gpo_distrib_ctrl[5]   -- 0 - internal path, 1 - external LO/REFCLK/SYSREF
    res = res ? res : dev_gpo_set(dev, IGPO_DISTRIB_CTRL, (1 << 0) | (15 << 1));

    // Wait for all LDOs to settle
    usleep(200000);

    // gpo_led_ctrl[0] -- LEDG[0]
    // gpo_led_ctrl[1] -- LEDR[0]
    // gpo_led_ctrl[2] -- LEDG[1]
    // gpo_led_ctrl[3] -- LEDR[1]
    // gpo_led_ctrl[4] -- LEDG[2]
    // gpo_led_ctrl[5] -- LEDR[2]
    // gpo_led_ctrl[6] -- LEDG[3]
    // gpo_led_ctrl[7] -- LEDR[3]
    res = res ? res : dev_gpo_set(dev, IGPO_LED_CTRL, 0xff);

    res = res ? res : dev_gpi_get32(dev, IGPI_STAT, &v);
    res = res ? res : i2c_reg_rd8(dev, I2C_BUS_LP87524, 0x01, &r);

    res = res ? res : lowlevel_spi_tr32(dev, 0, SPI_LMX2820_0, 0x9c0000, &s0);
    res = res ? res : lowlevel_spi_tr32(dev, 0, SPI_LMX2820_1, 0x9c0000, &s1);

    res = res ? res : lowlevel_spi_tr32(dev, 0, SPI_LMX1204, 0x176040, NULL); //Enable MUXOUT as SPI readback
    res = res ? res : lowlevel_spi_tr32(dev, 0, SPI_LMX1204, 0xA10000, &s2);

    res = res ? res : lowlevel_spi_tr32(dev, 0, SPI_LMX1214, 0x176040, NULL); //Enable MUXOUT
    res = res ? res : lowlevel_spi_tr32(dev, 0, SPI_LMX1214, 0xCF0000, &s3);

    res = res ? res : i2c_reg_rd8(dev, I2C_BUS_LMK1D1208I_LCK, 0x05, &r4);
    res = res ? res : i2c_reg_rd8(dev, I2C_BUS_LMK1D1208I_LRF, 0x05, &r5);

    USDR_LOG("SYNC", USDR_LOG_WARNING, "STAT=%08x LP87524_OTP=%02x LMS2820[0/1]=%04x/%04x LMX1204/LMX1214=%04x/%04x LMK1D1208I_LCK/LRF=%02x/%02x\n",
             v, r, s0, s1, s2, s3, r4, r5);

    // Initialize LMK05318B
    // XO: 25Mhz
    //
    // OUT0: LVDS       125.000 Mhz
    // OUT1: LVDS       125.000 Mhz
    // OUT2: LVDS       250.000 Mhz MASH_ORD 0/1/2 | 156.250 Mhz MASH_ORD 3
    // OUT3: LVDS       250.000 Mhz MASH_ORD 0/1/2 | 156.250 Mhz MASH_ORD 3
    // OUT4: LVDS OFF   156.250 Mhz  | OFF by default
    // OUT5: LVDS OFF   156.250 Mhz  | OFF by default
    // OUT6: Dual CMOS   10.000 Mhz
    // OUT7: Dual CMOS        1 Hz

    if(res)
        return res;

    lmk05318_dpll_settings_t dpll;
    memset(&dpll, 0, sizeof(dpll));
    dpll.enabled = true;
    dpll.en[LMK05318_PRIREF] = true;
    dpll.fref[LMK05318_PRIREF] = 1;
    dpll.type[LMK05318_PRIREF] = DPLL_REF_TYPE_DIFF_NOTERM;
    dpll.dc_mode[LMK05318_PRIREF] = DPLL_REF_DC_COUPLED_INT;
    dpll.buf_mode[LMK05318_PRIREF] = DPLL_REF_AC_BUF_HYST50_DC_EN;

    const uint64_t lmk_freq[8] =
    {
        125000000,
        125000000,
        150000000,
        150000000,
        156250000,
        156250000,
        10000000,
        1
    };

    lmk05318_out_config_t lmk_out[8];

    res = res ? res : lmk05318_port_request(&lmk_out[0], 0, lmk_freq[0], false, LVDS);
    res = res ? res : lmk05318_port_request(&lmk_out[1], 1, lmk_freq[1], false, LVDS);
    res = res ? res : lmk05318_port_request(&lmk_out[2], 2, lmk_freq[2], false, LVDS);
    res = res ? res : lmk05318_port_request(&lmk_out[3], 3, lmk_freq[3], false, LVDS);
    res = res ? res : lmk05318_port_request(&lmk_out[4], 4, lmk_freq[4], false, OUT_OFF);
    res = res ? res : lmk05318_port_request(&lmk_out[5], 5, lmk_freq[5], false, OUT_OFF);
    res = res ? res : lmk05318_port_request(&lmk_out[6], 6, lmk_freq[6], false, LVCMOS_P_N);
    res = res ? res : lmk05318_port_request(&lmk_out[7], 7, lmk_freq[7], false, LVCMOS_P_N);

    res = res ? res : lmk05318_create(dev, 0, I2C_BUS_LMK05318B, 25000000, XO_CMOS, false, &dpll, lmk_out, SIZEOF_ARRAY(lmk_out), &d->gen, false /*dry_run*/);
    if(res)
        return res;

    //wait for PRIREF/SECREF validation
    res = lmk05318_wait_dpll_ref_stat(&d->gen, 4*60000000); //60s - searching for satellites may take a lot of time if GPS in just turned on
    if(res)
    {
        USDR_LOG("SYNC", USDR_LOG_ERROR, "LMK03518 DPLL input reference freqs are not validated during specified timeout");
        return res;
    }

    //wait for lock
    res = lmk05318_wait_apll1_lock(&d->gen, 100000);
    res = res ? res : lmk05318_wait_apll2_lock(&d->gen, 100000);

    unsigned los_msk;
    lmk05318_check_lock(&d->gen, &los_msk, false /*silent*/); //just to log state

    if(res)
    {
        USDR_LOG("SYNC", USDR_LOG_ERROR, "LMK03518 PLLs not locked during specified timeout");
        return res;
    }

    //sync to make APLL1/APLL2 & out channels in-phase
    res = lmk05318_sync(&d->gen);
    if(res)
        return res;

    USDR_LOG("SYNC", USDR_LOG_INFO, "LMK03518 outputs synced");
    //

    usleep(2000000); //LMX2820[0] will not start without it

    //
    //LMX2820 #0 setup
    //

    const uint64_t lmx0_freq[] =
    {
        500000000*2, //1400000000,  //OUT_A
        500000000*2, //1400000000   //OUT_B
        25000000,                   //SR_OUT
    };

    res = lmx2820_create(dev, 0, SPI_LMX2820_0, &d->lmx0);

    lmx2820_sysref_chain_t* lmx0_sr = &d->lmx0.lmx2820_sysref_chain;
    lmx0_sr->enabled = false;
    lmx0_sr->master_mode = true;
    lmx0_sr->cont_pulse = true;
    lmx0_sr->srout = lmx0_freq[2];
    lmx0_sr->delay_ctrl = 0;

    res = res ? res : lmx2820_tune(&d->lmx0, lmk_freq[2], 2 /*mash order 2*/, 0 /*force_mult*/, lmx0_freq[0], lmx0_freq[1]);

    lmx2820_stats_t lmxstatus0;
    lmx2820_read_status(&d->lmx0, &lmxstatus0); //just for logging

    if(res)
    {
        USDR_LOG("SYNC", USDR_LOG_ERROR, "LMX2820[0] PLL not locked during specified timeout");
        return res;
    }

    USDR_LOG("SYNC", USDR_LOG_INFO, "LMX2820[0] outputs locked & synced");
    //

    //
    //LMX2820 #1 setup
    //

    const uint64_t lmx1_freq[] =
    {
        550000000, //1600000000,
        550000000, //1600000000
    };

    res = lmx2820_create(dev, 0, SPI_LMX2820_1, &d->lmx1);

    lmx2820_sysref_chain_t* lmx1_sr = &d->lmx1.lmx2820_sysref_chain;
    lmx1_sr->enabled = false;
    lmx1_sr->master_mode = true;
    lmx1_sr->cont_pulse = true;
    lmx1_sr->srout = 25000000;
    lmx1_sr->delay_ctrl = 0;

    res = res ? res : lmx2820_tune(&d->lmx1, lmk_freq[3], 2 /*mash order 2*/, 0 /*force_mult*/, lmx1_freq[0], lmx1_freq[1]);

    lmx2820_stats_t lmxstatus1;
    lmx2820_read_status(&d->lmx1, &lmxstatus1); //just for logging

    if(res)
    {
        USDR_LOG("SYNC", USDR_LOG_ERROR, "LMX2820[1] PLL not locked during specified timeout");
        return res;
    }

    USDR_LOG("SYNC", USDR_LOG_INFO, "LMX2820[1] outputs locked & synced");
    //

    //
    //LMX1214 setup
    //

    const uint64_t ld_clkout = lmx1_freq[0];
    bool ld_en[LMX1214_OUT_CNT] = {1,1,1,1};
    lmx1214_auxclkout_cfg_t ld_aux;
    ld_aux.enable = 1;
    ld_aux.fmt = LMX1214_FMT_LVDS;
    ld_aux.freq = ld_clkout;

    res = lmx1214_create(dev, 0, SPI_LMX1214, &d->lodistr);
    res = res ? res : lmx1214_solver(&d->lodistr, lmx1_freq[0], ld_clkout, ld_en, &ld_aux, false /*prec_mode*/, false /*dry run*/);

    float lmx1214_tempval;
    lmx1214_get_temperature(&d->lodistr, &lmx1214_tempval); //just for logging

    if(res)
    {
        USDR_LOG("SYNC", USDR_LOG_ERROR, "LMX1214 failed to initialize, res:%d", res);
        return res;
    }

    USDR_LOG("SYNC", USDR_LOG_INFO, "LMX1214 initialized");
    //

    //
    //LMX1204 setup
    //

    res = lmx1204_create(dev, 0, SPI_LMX1204, &d->cldistr);
    if(res)
    {
        USDR_LOG("SYNC", USDR_LOG_ERROR, "LMX1204 failed to initialize, res:%d", res);
        return res;
    }

    //set 1204 params
    lmx1204_state_t* lmx1204 = &d->cldistr;
    lmx1204->clkin     = lmx0_freq[1];         //LMX0 OUT_B
    lmx1204->sysrefreq = lmx0_freq[2];         //LMX0 SR_OUT
    lmx1204->clkout    = d->cldistr.clkin * 4;
    lmx1204->sysrefout = 3125000;
    lmx1204->sysref_mode = LMX1204_CONTINUOUS;
    lmx1204->logiclkout = 125000000;

    lmx1204->ch_en[LMX1204_CH0] = 1;
    lmx1204->ch_en[LMX1204_CH1] = 1;
    lmx1204->ch_en[LMX1204_CH2] = 1;
    lmx1204->ch_en[LMX1204_CH3] = 1;
    lmx1204->ch_en[LMX1204_CH_LOGIC] = 1;

    lmx1204->clkout_en[LMX1204_CH0] = 1;
    lmx1204->clkout_en[LMX1204_CH1] = 1;
    lmx1204->clkout_en[LMX1204_CH2] = 1;
    lmx1204->clkout_en[LMX1204_CH3] = 1;
    lmx1204->clkout_en[LMX1204_CH_LOGIC] = 1;

    lmx1204->sysref_en = 1;
    lmx1204->sysrefout_en[LMX1204_CH0] = 1;
    lmx1204->sysrefout_en[LMX1204_CH1] = 1;
    lmx1204->sysrefout_en[LMX1204_CH2] = 1;
    lmx1204->sysrefout_en[LMX1204_CH3] = 1;
    lmx1204->sysrefout_en[LMX1204_CH_LOGIC] = 1;

    lmx1204->logiclkout_fmt    = LMX1204_FMT_LVDS;
    lmx1204->logisysrefout_fmt = LMX1204_FMT_LVDS;

    res = lmx1204_solver(&d->cldistr, false/*prec_mode*/, false/*dry_run*/);
    if(res)
        return res;

    lmx1204_stats_t lmx1204status;
    lmx1204_read_status(&d->cldistr, &lmx1204status); //just for log

    res = lmx1204_wait_pll_lock(&d->cldistr, 1000000);

    lmx1204_read_status(&d->cldistr, &lmx1204status); //just for log

    if(res)
    {
        USDR_LOG("SYNC", USDR_LOG_ERROR, "LMX1204 not locked, err:%d [%s]",
                 res, (res == -ETIMEDOUT ? "TIMEOUT" : "ERROR"));
        return res;
    }
    USDR_LOG("SYNC", USDR_LOG_INFO, "LMX1204 locked");

    USDR_LOG("SYNC", USDR_LOG_INFO, "LMX1204 initialized");
    //

    //
    // LMK1D1208I[0] setup
    //

    lmk1d1208i_config_t lmk1d_cfg;
    lmk1d_cfg.in[0].enabled = true;
    lmk1d_cfg.in[1].enabled = false;
    lmk1d_cfg.bank[0].mute = false;
    lmk1d_cfg.bank[1].mute = false;
    lmk1d_cfg.bank[0].sel = LMK1D1208I_IN0;
    lmk1d_cfg.bank[1].sel = LMK1D1208I_IN0;
    lmk1d_cfg.out[0].amp = LMK1D1208I_STANDARD_LVDS;
    lmk1d_cfg.out[1].amp = LMK1D1208I_STANDARD_LVDS;
    lmk1d_cfg.out[2].amp = LMK1D1208I_STANDARD_LVDS;
    lmk1d_cfg.out[3].amp = LMK1D1208I_STANDARD_LVDS;
    lmk1d_cfg.out[4].amp = LMK1D1208I_STANDARD_LVDS;
    lmk1d_cfg.out[5].amp = LMK1D1208I_STANDARD_LVDS;
    lmk1d_cfg.out[6].amp = LMK1D1208I_STANDARD_LVDS;
    lmk1d_cfg.out[7].amp = LMK1D1208I_STANDARD_LVDS;
    lmk1d_cfg.out[0].enabled = true;
    lmk1d_cfg.out[1].enabled = true;
    lmk1d_cfg.out[2].enabled = true;
    lmk1d_cfg.out[3].enabled = true;
    lmk1d_cfg.out[4].enabled = true;
    lmk1d_cfg.out[5].enabled = true;
    lmk1d_cfg.out[6].enabled = true;
    lmk1d_cfg.out[7].enabled = true;

    res = lmk1d1208i_create(dev, 0, I2C_BUS_LMK1D1208I_LCK, &lmk1d_cfg, &d->lmk1d0);
    if(res)
    {
        USDR_LOG("SYNC", USDR_LOG_ERROR, "LMK1D1208I[0] failed to initialize, res:%d", res);
        return res;
    }

    USDR_LOG("SYNC", USDR_LOG_INFO, "LMK1D1208I[0] initialized");
    //

    //
    // LMK1D1208I[1] setup
    //

    lmk1d_cfg.in[0].enabled = true;
    lmk1d_cfg.in[1].enabled = false;
    lmk1d_cfg.bank[0].mute = false;
    lmk1d_cfg.bank[1].mute = false;
    lmk1d_cfg.bank[0].sel = LMK1D1208I_IN0;
    lmk1d_cfg.bank[1].sel = LMK1D1208I_IN0;
    lmk1d_cfg.out[0].amp = LMK1D1208I_STANDARD_LVDS;
    lmk1d_cfg.out[1].amp = LMK1D1208I_STANDARD_LVDS;
    lmk1d_cfg.out[2].amp = LMK1D1208I_STANDARD_LVDS;
    lmk1d_cfg.out[3].amp = LMK1D1208I_STANDARD_LVDS;
    lmk1d_cfg.out[4].amp = LMK1D1208I_STANDARD_LVDS;
    lmk1d_cfg.out[5].amp = LMK1D1208I_STANDARD_LVDS;
    lmk1d_cfg.out[6].amp = LMK1D1208I_STANDARD_LVDS;
    lmk1d_cfg.out[7].amp = LMK1D1208I_STANDARD_LVDS;
    lmk1d_cfg.out[0].enabled = true;
    lmk1d_cfg.out[1].enabled = true;
    lmk1d_cfg.out[2].enabled = true;
    lmk1d_cfg.out[3].enabled = true;
    lmk1d_cfg.out[4].enabled = true;
    lmk1d_cfg.out[5].enabled = true;
    lmk1d_cfg.out[6].enabled = true;
    lmk1d_cfg.out[7].enabled = true;

    res = lmk1d1208i_create(dev, 0, I2C_BUS_LMK1D1208I_LRF, &lmk1d_cfg, &d->lmk1d1);
    if(res)
    {
        USDR_LOG("SYNC", USDR_LOG_ERROR, "LMK1D1208I[1] failed to initialize, res:%d", res);
        return res;
    }

    USDR_LOG("SYNC", USDR_LOG_INFO, "LMK1D1208I[1] initialized");
    //

    return res;
}

static int usdr_device_pe_sync_create_stream(device_t* dev, const char* sid, const char* dformat,
                                              const usdr_channel_info_t* channels, unsigned pktsyms,
                                              unsigned flags, const char* parameters, stream_handle_t** out_handle)
{
    return -EINVAL;
}

static int usdr_device_pe_sync_unregister_stream(device_t* dev, stream_handle_t* stream)
{
    return -EINVAL;
}

static int usdr_device_pe_sync_create(lldev_t dev, device_id_t devid)
{
    int res;
    struct dev_pe_sync *d = (struct dev_pe_sync *)malloc(sizeof(struct dev_pe_sync));
    res = usdr_device_base_create(&d->base, dev);
    if (res) {
        goto failed_free;
    }

    res = vfs_add_const_i64_vec(&d->base.rootfs,
                                s_params_pe_sync_rev000,
                                SIZEOF_ARRAY(s_params_pe_sync_rev000));
    if (res)
        goto failed_tree_creation;

    res = usdr_vfs_obj_param_init_array(&d->base,
                                        s_fparams_pe_sync_rev000,
                                        SIZEOF_ARRAY(s_fparams_pe_sync_rev000));
    if (res)
        goto failed_tree_creation;

    d->base.initialize = &usdr_device_pe_sync_initialize;
    d->base.destroy = &usdr_device_pe_sync_destroy;
    d->base.create_stream = &usdr_device_pe_sync_create_stream;
    d->base.unregister_stream = &usdr_device_pe_sync_unregister_stream;
    d->base.timer_op = NULL;

    dev->pdev = &d->base;
    return 0;

failed_tree_creation:
    usdr_device_base_destroy(&d->base);
failed_free:
    free(d);
    return res;
}


static const
struct device_factory_ops s_ops = {
    usdr_device_pe_sync_create,
};


int usdr_device_register_pe_sync()
{
    return usdr_device_register(PE_SYNC_DEVICE_ID_C, &s_ops);
}
