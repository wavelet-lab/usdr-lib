// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <usdr_port.h>
#include <usdr_lowlevel.h>
#include <usdr_logging.h>

#include "../device.h"
#include "../device_vfs.h"
#include "../device_names.h"
#include "../device_cores.h"
#include "../device_ids.h"

#include "../ipblks/streams/sfe_rx_4.h"
#include "../ipblks/streams/stream_sfetrx4_dma32.h"
#include "../ipblks/fgearbox.h"

#include "../generic_usdr/generic_regs.h"

#include "../hw/tps6381x/tps6381x.h"
#include "../hw/lmk05318/lmk05318.h"
#include "../hw/lp875484/lp875484.h"
#include "../hw/afe79xx/afe79xx.h"

#include "dsdr_hiper.h"

// Board revisions:
//  1 - DSDR
//  2 - Hiper
//

enum dsdr_type {
    DSDR_KCU116_EVM = 0xce,
    DSDR_M2_R0 = 0xc2,
    DSDR_PCIE_HIPER_R0 = 0xcf,
};

enum dsdr_jesdv {
    DSDR_JESD204B_810_245 = 1,
    DSDR_JESD204C_6664_245 = 7,
    DSDR_JESD204C_6664_491 = 3,
};

// I2C buses
// I2C3      () -- TCA6424AR ( 0 & 1), TMP114
// REF/I2C5  () -- TCA6424AR ( 0 ), DAC80501MD, LG77LIC, TMP114
//

// SPI buses
// LMS8001 + 6sens  --
// REF_SDIO         -- ADF4002B


// We need digital lock detect
// ADF4002/IC1 (pin 15) -> GPIO33_3???



enum {
    SRF4_FIFOBSZ = 0x10000, // 64kB
};

enum i2c_bus1 {
    I2C_ADDR_PMIC_0P9 = 0x60, //LP875484
};

enum i2c_bus2 {
    // I2C_ADDR_TPS63811 = 0x75, //TPS63811
    I2C_ADDR_LMK = 0x65, //LMK05318B
};

enum spi_bus {
    SPI_BUS_HIPER_FE = 4,
};

enum i2c_idx {
    // I2C_AFE_PMIC = 1,
    // I2C_TPS63811 = 2,
    // I2C_LMK = 3,

    I2C_AFE_PMIC   = MAKE_LSOP_I2C_ADDR(0, 0, I2C_ADDR_PMIC_0P9),
    I2C_TPS63811   = MAKE_LSOP_I2C_ADDR(0, 1, I2C_DEV_DCDCBOOST),
    I2C_LMK        = MAKE_LSOP_I2C_ADDR(0, 1, I2C_ADDR_LMK),
};

// LMK ports
// -------------------------
// port 0: N/C
// port 1: LMK_AFEREF                                 |   AFECLK 491.52
// port 2: LMK_SYSREF                                 |   SYSREF   3.84
// port 3: N/C
// port 4: FPGA_GT_ETHREF    => BANK224 / REF1        |
// port 5: FPGA_GT_AFEREF    => BANK226 / REF1        |   GTHCLK 122.88 / 245.76
// port 6: FPGA_SYSREF       => BANK65  / T24_U24     |   SYSREF   3.84
// port 7: FPGA_1PPS         => BANK65  / T25_U25     |   SYSCLK 122.88 / 245.76

enum lmk_ports {
    LMK_PORT_AFEREF = 1,
    LMK_PORT_SYSREF = 2,

    LMK_FPGA_GT_ETHREF = 4,
    LMK_FPGA_GT_AFEREF = 5,
    LMK_FPGA_SYSREF = 6,
    LMK_FPGA_1PPS = 7,
};

// Power dependencies
// VIOSYS for I2C_ADDR_PMIC_0P9 depends on PWR_EN_2P05
enum {
    IGPI_JESD_SYSREF_RAC  = 36,
    IGPI_JESD_TX_SYSN_CNT = 37,
    IGPI_JESD_RX_LMFC_RD  = 38,
    IGPO_JESD_AUX         = 39,
};

enum {
    IGPO_TIAFE_MASTER_RESET_N = 13,
    IGPO_TIAFE_RX_SYNC_RESET = 14,
    IGPO_TIAFE_RX_LANE_ENABLED = 15,
    IGPO_TIAFE_RX_LANE_POLARITY = 16,
    IGPO_TIAFE_RX_LANE_MAP_0 = 17,
    IGPO_TIAFE_RX_LANE_MAP_1 = 18,
    IGPO_TIAFE_RX_LANE_MAP_2 = 19,
    IGPO_TIAFE_RX_LANE_MAP_3 = 20,
    IGPO_TIAFE_RX_BUFFER_RELDLY_0 = 21,
    IGPO_TIAFE_RX_BUFFER_RELDLY_1 = 22,
    IGPO_TIAFE_RX_CLR_SYSREF_REALIGN = 23,
    IGPO_TIAFE_TX_SYNC_RESET = 24,
    IGPO_TIAFE_TX_LANE_ENABLED = 25,
    IGPO_TIAFE_TX_LANE_POLARITY = 26,
    IGPO_TIAFE_TX_LANE_MAP_0 = 27,
    IGPO_TIAFE_TX_LANE_MAP_1 = 28,
    IGPO_TIAFE_TX_LANE_MAP_2 = 29,
    IGPO_TIAFE_TX_LANE_MAP_3 = 30,
    IGPO_TIAFE_TX_CLR_SYSREF_REALIGN = 31,

    IGPO_DSPCHAIN_PRG = 32,
    IGPO_DSPCHAIN_RST = 33,

    IGPO_RX_MAP = 34,

    IGPO_DSPCHAIN_TX_PRG = 35,
    IGPO_DSPCHAIN_TX_RST = 36,

    IGPO_TX_MAP = 37,
};

static
const usdr_dev_param_constant_t s_params_m2_dsdr_rev000[] = {
    { DNLL_SPI_COUNT, 5 },
    { DNLL_I2C_COUNT, 2 }, // 2
    { DNLL_SRX_COUNT, 1 },
    { DNLL_STX_COUNT, 1 },
    { DNLL_RFE_COUNT, 1 },
    { DNLL_TFE_COUNT, 0 },
    { DNLL_IDX_REGSP_COUNT, 1 },
    { DNLL_IRQ_COUNT, 9 },

    // low level buses
    { "/ll/irq/0/core", USDR_MAKE_COREID(USDR_CS_AUX, USDR_AC_PIC32_PCI) },
    { "/ll/irq/0/base", M2PCI_REG_INT },
    { "/ll/spi/0/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_SIMPLE) },
    { "/ll/spi/0/base", M2PCI_REG_SPI0 },
    { "/ll/spi/0/irq",  M2PCI_INT_SPI_0 },
    { "/ll/spi/1/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_SIMPLE) },
    { "/ll/spi/1/base", M2PCI_REG_SPI1 },
    { "/ll/spi/1/irq",  M2PCI_INT_SPI_1 },
    { "/ll/spi/2/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_SIMPLE) },
    { "/ll/spi/2/base", M2PCI_REG_SPI2 },
    { "/ll/spi/2/irq",  M2PCI_INT_SPI_2 },
    { "/ll/spi/3/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_SIMPLE) },
    { "/ll/spi/3/base", M2PCI_REG_SPI3 },
    { "/ll/spi/3/irq",  M2PCI_INT_SPI_3 },
    { "/ll/spi/4/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_CFG_CS8) },
    { "/ll/spi/4/base", REG_SPI_EXT_DATA },
    { "/ll/spi/4/irq",  M2PCI_INT_SPI_EXT },

    { "/ll/qspi/0/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_QSPIA24_R0) },
    { "/ll/qspi/0/base", M2PCI_REG_QSPI_FLASH },
    { "/ll/qspi/0/irq",  -1 },
    { "/ll/qspi_flash/base", M2PCI_REG_QSPI_FLASH },

    { "/ll/i2c/0/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_DI2C_SIMPLE) },
    { "/ll/i2c/0/base", M2PCI_REG_I2C },
    { "/ll/i2c/0/irq",  M2PCI_INT_I2C_0 },
    { "/ll/i2c/1/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_DI2C_SIMPLE) },
    { "/ll/i2c/1/base", REG_SPI_I2C2 },
    { "/ll/i2c/1/irq",  M2PCI_INT_I2C_1 },

    { "/ll/gpio/0/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_GPIO15_SIMPLE) },
    { "/ll/gpio/0/base", M2PCI_REG_GPIO_S },
    { "/ll/gpio/0/irq",  -1 },
    { "/ll/uart/0/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_UART_SIMPLE) },
    { "/ll/uart/0/base", REG_UART_TRX },
    { "/ll/uart/0/irq",  -1 },

    // Indexed area map
    { "/ll/idx_regsp/0/base", M2PCI_REG_WR_BADDR },
    { "/ll/idx_regsp/0/virt_base", VIRT_CFG_SFX_BASE },

    // data stream cores
    { "/ll/srx/0/core",    USDR_MAKE_COREID(USDR_CS_STREAM, USDR_SC_RXDMA_BRSTN) },
    { "/ll/srx/0/base",    M2PCI_REG_WR_RXDMA_CONFIRM},
    { "/ll/srx/0/cfg_base",VIRT_CFG_SFX_BASE },
    { "/ll/srx/0/irq",     M2PCI_INT_RX},
    { "/ll/srx/0/dmacap",  0x855 },
    { "/ll/srx/0/rfe",     (uintptr_t)"/ll/rfe/0" },
//    { "/ll/srx/0/chmsk",   0x3 },
    { "/ll/rfe/0/fifobsz", SRF4_FIFOBSZ },
    { "/ll/rfe/0/core",    USDR_MAKE_COREID(USDR_CS_FE, USDR_FC_BRSTN) },
    { "/ll/rfe/0/base",    CSR_RFE4_BASE },

    { "/ll/stx/0/core",    USDR_MAKE_COREID(USDR_CS_STREAM, USDR_SC_TXDMA_OLD) },
    { "/ll/stx/0/base",    M2PCI_REG_WR_TXDMA_CNF_L},
    { "/ll/stx/0/cfg_base",VIRT_CFG_SFX_BASE + 512 },
    { "/ll/stx/0/irq",     M2PCI_INT_TX},
    { "/ll/stx/0/dmacap",  0x555 },

    { "/ll/sdr/0/rfic/0", (uintptr_t)"afe79xx" },
    { "/ll/sdr/max_hw_rx_chans",  4 },
    { "/ll/sdr/max_hw_tx_chans",  4 },

    { "/ll/sdr/max_sw_rx_chans",  4 },
    { "/ll/sdr/max_sw_tx_chans",  4 },
};

static int dev_m2_dsdr_rate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_dsdr_gain_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_dsdr_senstemp_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dev_m2_dsdr_debug_all_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dev_m2_dsdr_pwren_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_dsdr_debug_rxtime_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dev_m2_dsdr_stat(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dev_m2_dsdr_debug_lldev_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dev_m2_dsdr_debug_clk_info(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_dsdr_debug_clk_info_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *value);

static int dev_m2_dsdr_sdr_rx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_dsdr_sdr_tx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_dsdr_afe_health_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dev_m2_dsdr_dummy(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return 0;
}

static int _debug_lmk05318_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int _debug_lmk05318_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);


static
const usdr_dev_param_func_t s_fparams_m2_dsdr_rev000[] = {
    { "/dm/rate/master",        { dev_m2_dsdr_rate_set, NULL }},
    { "/dm/sdr/0/rx/gain",      { dev_m2_dsdr_gain_set, NULL }},
    { "/dm/sdr/0/rx/freqency",  { dev_m2_dsdr_sdr_rx_freq_set, NULL }},
    { "/dm/sdr/0/rx/bandwidth", { dev_m2_dsdr_dummy, NULL }},
    { "/dm/sdr/0/rx/path",      { dev_m2_dsdr_dummy, NULL }},

    { "/dm/sdr/0/tx/gain",      { dev_m2_dsdr_dummy, NULL }},
    { "/dm/sdr/0/tx/freqency",  { dev_m2_dsdr_sdr_tx_freq_set, NULL }},
    { "/dm/sdr/0/tx/bandwidth", { dev_m2_dsdr_dummy, NULL }},
    { "/dm/sdr/0/tx/path",      { dev_m2_dsdr_dummy, NULL }},

    { "/dm/sdr/0/afe_health",   { dev_m2_dsdr_dummy, dev_m2_dsdr_afe_health_get }},

    { "/dm/sensor/temp",  { NULL, dev_m2_dsdr_senstemp_get }},
    { "/dm/debug/all",    { NULL, dev_m2_dsdr_debug_all_get }},
    { "/dm/debug/rxtime", { NULL, dev_m2_dsdr_debug_rxtime_get }},
    { "/dm/power/en",     { dev_m2_dsdr_pwren_set, NULL }},
    { "/dm/stat",         { NULL, dev_m2_dsdr_stat }},
    { "/debug/lldev",     { NULL, dev_m2_dsdr_debug_lldev_get}},
    { "/dm/sdr/refclk/path", { dev_m2_dsdr_dummy, NULL}},
    { "/debug/clk_info",  { dev_m2_dsdr_debug_clk_info, dev_m2_dsdr_debug_clk_info_get }},

    { "/debug/hw/lmk05318/0/reg", { _debug_lmk05318_reg_set, _debug_lmk05318_reg_get }},

};



struct dev_m2_dsdr {
    device_t base;

    lmk05318_state_t lmk;

    subdev_t subdev;

    unsigned type;
    unsigned jesdv;

    stream_handle_t* rx;
    stream_handle_t* tx;

    afe79xx_state_t st;
    dsdr_hiper_fe_t hiper;

    uint32_t debug_lmk05318_last;

    const char* afecongiguration;
    uint32_t max_rate;

    uint32_t adc_rate;
    unsigned rxbb_rate;
    unsigned rxbb_decim;

    uint32_t dac_rate;
    unsigned txbb_rate;
    unsigned txbb_inter;
};
typedef struct dev_m2_dsdr dev_m2_dsdr_t;



enum dev_gpo {
    IGPO_BANK_LEDS        = 0,
    IGPO_PWR_LMK          = 1,
    IGPO_PWR_AFE          = 2,
    IGPO_AFE_RST          = 3,
};

enum dev_gpi {
    IGPI_PGOOD           = 16,
};

static int dev_gpo_set(lldev_t dev, unsigned bank, unsigned data)
{
    return lowlevel_reg_wr32(dev, 0, 0, ((bank & 0x7f) << 24) | (data & 0xff));
}

static int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, 0, 16 + (bank / 4), data);
}


int dev_m2_dsdr_sdr_rx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    if (!d->st.libcapi79xx_upd_nco)
        return 0;

    for (unsigned i = 0; i < 4; i++) {
        d->st.libcapi79xx_upd_nco(&d->st.capi, NCO_RX, i, value / 1000, 0, 0);
    }

    return 0;
}

int dev_m2_dsdr_sdr_tx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    if (!d->st.libcapi79xx_upd_nco)
        return 0;

    for (unsigned i = 0; i < 4; i++) {
        d->st.libcapi79xx_upd_nco(&d->st.capi, NCO_TX, i, value / 1000, 0, 0);
    }

    return 0;
}

int dev_m2_dsdr_gain_set(pdevice_t ud, pusdr_vfs_obj_t UNUSED obj, uint64_t value)
{
    return 0;
}

int dev_m2_dsdr_rate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    int res = 0;

    unsigned tx_inters[] = { 1, 2, 0, 4, 0, 0, 8, 16, 32, 64, 0 };
    unsigned rx_decims[] = { 1, 2, 3, 4, 5, 6, 8, 16, 32, 64, 128 };
    unsigned i = 0;
    unsigned ii;

    for (ii = 0; ii < SIZEOF_ARRAY(rx_decims); ii++) {
        if (value * rx_decims[ii] < d->max_rate)
            i = ii;
        else
            break;
    }

    d->rxbb_rate = d->adc_rate / rx_decims[i];
    d->rxbb_decim = rx_decims[i];

    d->txbb_rate = tx_inters[i] == 0 ? 0 : d->dac_rate / tx_inters[i];
    d->txbb_inter = tx_inters[i];

    USDR_LOG("DSDR", USDR_LOG_ERROR, "Set rate: RX %.3f Mhz => %.3f (Decim: %d) -- TX %.3f Mhz => %.3f (Inter: %d)\n",
             value / 1.0e6, d->rxbb_rate / 1.0e6, d->rxbb_decim,
             value / 1.0e6, d->txbb_rate / 1.0e6, d->txbb_inter);

    res = (res) ? res : fgearbox_load_fir(d->base.dev, IGPO_DSPCHAIN_PRG, (fgearbox_firs_t)d->rxbb_decim);
    if (res) {
        USDR_LOG("LSDR", USDR_LOG_ERROR, "Unable to initialize decimation FIR gearbox, error = %d!\n", res);
        return res;
    }

    if (d->txbb_inter > 0) {
        d->txbb_rate = d->dac_rate / d->txbb_inter;

        res = (res) ? res : fgearbox_load_fir_i(d->base.dev, IGPO_DSPCHAIN_TX_PRG, (fgearbox_firs_t)d->txbb_inter);
        if (res) {
            USDR_LOG("LSDR", USDR_LOG_ERROR, "Unable to initialize interpolation FIR gearbox, error = %d!\n", res);
            return res;
        }
    }

#if 0
    for (int i = 0; i < 5; i++) {
        uint32_t clk;
        res = res ? res : dev_gpi_get32(d->base.dev, 20, &clk);

        USDR_LOG("DSDR", USDR_LOG_ERROR, "Clk %d: %d\n", clk >> 28, clk & 0xfffffff);
        usleep(0.5 * 1e6);
    }
#endif

    return 0;
}

int dev_m2_dsdr_afe_health_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_dsdr *o = (struct dev_m2_dsdr *)ud;
    if (o->st.libcapi79xx_check_health == 0) {
        USDR_LOG("DSDR", USDR_LOG_ERROR, "AFE CAPI isn't available!\n");
        return -ENOENT;
    }

    int rok = 0;
    char staus_buffer[16384];
    staus_buffer[0] = 0;

    int res = o->st.libcapi79xx_check_health(&o->st.capi, &rok, SIZEOF_ARRAY(staus_buffer), staus_buffer);
    if (res)
        return res;

    *ovalue = rok;
    if (staus_buffer[0]) {
        USDR_LOG("DSDR", USDR_LOG_WARNING, "AFE health report:\n%s\n", staus_buffer);
    }
    return 0;
}


int _debug_lmk05318_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_dsdr *o = (struct dev_m2_dsdr *)ud;
    *ovalue = o->debug_lmk05318_last;
    return 0;
}


int _debug_lmk05318_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_dsdr *o = (struct dev_m2_dsdr *)ud;
    int res;
    unsigned addr = (value >> 8) & 0x7fff;
    unsigned data = value & 0xff;
    uint8_t d;

    o->debug_lmk05318_last = ~0u;

    if (value & 0x800000) {
        res = lmk05318_reg_wr(&o->lmk, addr, data);

        USDR_LOG("XDEV", USDR_LOG_WARNING, "LMK05318 WR REG %04x => %04x\n",
                 (unsigned)addr, data);
    } else {
        d = 0xff;
        res = lmk05318_reg_rd(&o->lmk, addr, &d);
        o->debug_lmk05318_last = d;

        USDR_LOG("XDEV", USDR_LOG_WARNING, "LMK05318 RD REG %04x <= %04x\n",
                 (unsigned)addr,
                 o->debug_lmk05318_last);
    }

    return res;
}

int dev_m2_dsdr_stat(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    *ovalue = 0;
    return 0;
}

int dev_m2_dsdr_debug_clk_info(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return 0;
}

int dev_m2_dsdr_debug_clk_info_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    int res = 0;
    uint32_t clk;

    res = res ? res : dev_gpi_get32(d->base.dev, 20, &clk);
    *value = clk & 0xfffffff;
    return res;
}


int dev_m2_dsdr_debug_lldev_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    lldev_t dev = d->base.dev;
    *ovalue = (intptr_t)dev;
    return 0;
}

int dev_m2_dsdr_senstemp_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue)
{
    // struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    // lldev_t dev = d->base.dev;
    // int temp, res;
    // res = tmp108_temp_get(dev, 0, I2C_BUS_TMP108, &temp);
    // if (res)
    //     return res;

    // *ovalue = (int64_t)temp;
    return 0;
}

int dev_m2_dsdr_debug_all_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    *ovalue = 0;
    return 0;
}

int dev_m2_dsdr_pwren_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return 0;
}

static int dev_m2_dsdr_debug_rxtime_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    *ovalue = 0;
    return 0;
}


static
void usdr_device_m2_dsdr_destroy(pdevice_t udev)
{
    usdr_device_base_destroy(udev);
}

static int usdr_jesd204b_bringup_pre(struct dev_m2_dsdr *dd)
{
    lldev_t dev = dd->base.dev;
    int res = 0;
    uint32_t d;

    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_RX_SYNC_RESET, 1);
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_TX_SYNC_RESET, 1);
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_MASTER_RESET_N, 0);


    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_RX_LANE_MAP_0, 0x10);
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_RX_LANE_MAP_1, 0x32);

    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_TX_LANE_MAP_0, 0x10);
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_TX_LANE_MAP_1, 0x32);

    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_RX_LANE_POLARITY, 0x0);
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_TX_LANE_POLARITY, 0x0);

    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_RX_LANE_ENABLED, 0xf);
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_TX_LANE_ENABLED, 0xf);

    usleep(1);

    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_MASTER_RESET_N, 1);

    for (unsigned k = 0; k < 100; k++) {
        usleep(10000);

        res = dev_gpi_get32(dev, IGPI_JESD_SYSREF_RAC, &d);
        USDR_LOG("DSDR", USDR_LOG_ERROR, "STAT = %08x\n", d);
        if (d & 0x08000000)
            break;
    }


    // TODO wait for PLL to lock..
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_TX_SYNC_RESET, 0);

    usleep(10000);

    res = res ? res :dev_gpi_get32(dev, IGPI_JESD_SYSREF_RAC, &d);
    USDR_LOG("DSDR", USDR_LOG_ERROR, "STAT = %08x\n", d);
    return res;
}

static int usdr_jesd204b_bringup_post(struct dev_m2_dsdr *dd)
{
    lldev_t dev = dd->base.dev;
    int res = 0;
    uint32_t d = 0;

    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_RX_SYNC_RESET, 0);

    usleep(10000);

    res = res ? res : dev_gpi_get32(dev, IGPI_JESD_SYSREF_RAC, &d);
    USDR_LOG("DSDR", USDR_LOG_ERROR, "STAT = %08x\n", d);
    return res;

}

static
int usdr_device_m2_dsdr_initialize(pdevice_t udev, unsigned pcount, const char** devparam, const char** devval)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)udev;
    lldev_t dev = d->base.dev;
    int res = 0;
    uint32_t hwid, usr2, pg, los, devid, jesdv;

    d->subdev = 0;

    res = res ? res : dev_gpi_get32(dev, IGPI_USR_ACCESS2, &usr2);
    res = res ? res : dev_gpi_get32(dev, IGPI_HWID, &hwid);
    if (res) {
        return res;
    }

    devid = (hwid >> 16) & 0xff;
    jesdv = (hwid >> 8) & 0xff;
    switch (devid) {
    case DSDR_KCU116_EVM:
    case DSDR_M2_R0:
    case DSDR_PCIE_HIPER_R0:
        d->type = devid;
        break;

    case 0xff:
        d->type = DSDR_PCIE_HIPER_R0;
        break;

    default:
        USDR_LOG("XDEV", USDR_LOG_ERROR, "Unsupported HWID = %08x, skipping initialization!\n", hwid);
        return -EIO;
    }

    switch (jesdv) {
    case DSDR_JESD204B_810_245:
        d->max_rate = 260e6;
        d->dac_rate = d->adc_rate = 245760000;
        d->afecongiguration = "/home/serg/Downloads/Afe79xxPg1_02.txt";
        break;

    case DSDR_JESD204C_6664_245:
        d->max_rate = 260e6;
        d->dac_rate = d->adc_rate = 245760000;
        d->afecongiguration =  "/home/serg/Downloads/Afe79xxPg1_6664_245.txt";
        break;

    case DSDR_JESD204C_6664_491:
        d->max_rate = 520e6;
        d->dac_rate = d->adc_rate = 491520000;
        d->afecongiguration =  "/home/serg/Downloads/Afe79xxPg1_6664_491.txt";
        break;

    default:
        USDR_LOG("XDEV", USDR_LOG_ERROR, "Unsupported JESD type %x (HWID = %08x), skipping initialization!\n", jesdv, hwid);
        return -EIO;
    }

    d->jesdv = jesdv;
    USDR_LOG("XDEV", USDR_LOG_WARNING, "AFE type JESD204%c\n", (jesdv == DSDR_JESD204B_810_245) ? 'B' : 'C');

    if (d->type == DSDR_KCU116_EVM) {
        USDR_LOG("XDEV", USDR_LOG_ERROR, "Skipping AFE initialization! SR=%.2f\n", d->adc_rate / 1e6);
        res = res ? res : afe79xx_create_dummy(&d->st);

        for (int i = 0; i < 10; i++) {
            uint32_t clk;
            res = res ? res : dev_gpi_get32(d->base.dev, 20, &clk);

            USDR_LOG("DSDR", USDR_LOG_ERROR, "Clk %d: %d\n", clk >> 28, clk & 0xfffffff);
            usleep(0.5 * 1e6);
        }

        res = usdr_jesd204b_bringup_pre(d);

        USDR_LOG("XDEV", USDR_LOG_ERROR, "Waiting for AFE... (press eneter when external confuguration is done)\n");
        getchar();
        USDR_LOG("XDEV", USDR_LOG_ERROR, "Resetting JESD\n");

        res = usdr_jesd204b_bringup_post(d);
        return res;
    }

    res = res ? res : dev_gpo_set(dev, IGPO_BANK_LEDS, 1);

    if (getenv("USDR_BARE_DEV")) {
        USDR_LOG("XDEV", USDR_LOG_WARNING, "USDR_BARE_DEV is set, skipping initialization!\n");
        return res;
    }


    res = res ? res : dev_gpo_set(dev, IGPO_PWR_LMK, 0xf);

    for (unsigned j = 0; j < 10; j++) {
        usleep(10000);
        res = res ? res : dev_gpi_get32(dev, 16, &pg);
        if (res || (pg & 1))
            break;
    }

    if (d->type == DSDR_M2_R0) {
        for (unsigned j = 0; j < 20; j++) {
            usleep(10000);
            res = res ? res : tps6381x_init(dev, d->subdev, I2C_TPS63811, true, true, 3450);
            if (res == 0)
                break;
        }
    }
    usleep(40000);


    for (unsigned j = 0; j < 25; j++) {
        usleep(40000);
        res = res ? res : lmk05318_create(dev, d->subdev, I2C_LMK,
                                          (d->type == DSDR_PCIE_HIPER_R0) ? 2 : 1 /* TODO FIXME!!! */, &d->lmk);
        if (res == 0)
            break;
    }
    // Update deviders for 245/491MSPS rate
    if (d->jesdv == DSDR_JESD204C_6664_491) {
        // GT should be 245.76
        // FPGA SYSCLK should be 245.76

        res = res ? res : lmk05318_set_out_div(&d->lmk, LMK_FPGA_GT_AFEREF, 4);
        res = res ? res : lmk05318_set_out_div(&d->lmk, LMK_FPGA_1PPS, 4);
    }

    usleep(1000);

    res = res ? res : lmk05318_check_lock(&d->lmk, &los);

    for (int i = 0; i < 5; i++) {
        uint32_t clk = 0;
        res = res ? res : dev_gpi_get32(d->base.dev, 20, &clk);

        USDR_LOG("DSDR", USDR_LOG_ERROR, "Clk %d: %d\n", clk >> 28, clk & 0xfffffff);
        usleep(0.5 * 1e6);
    }

    res = res ? res : lmk05318_check_lock(&d->lmk, &los);



    // res = res ? res : lmk05318_set_out_mux(&d->lmk, LMK_FPGA_SYSREF, false, LVDS);

    USDR_LOG("DSDR", USDR_LOG_ERROR, "Configuration: OK [%08x, %08x] res=%d   PG=%08x\n", usr2, hwid, res, pg);

    // return 0;
    uint32_t pgdat = 0;
    res = dev_gpi_get32(dev, IGPI_PGOOD, &pgdat);
    if (!(pgdat & (1 << 0))) {
        USDR_LOG("DSDR", USDR_LOG_ERROR, "2V05 isn't good, giving up!\n");
        return -EIO;
    }

    // Initialize AFEPWR
    res = res ? res : dev_gpo_set(dev, IGPO_PWR_AFE, 0x1); // Enable VIOSYS, hold RESET
    usleep(10000);
    //res = res ? res : dev_gpo_set(dev, IGPO_PWR_AFE, 0x3); // Enable VIOSYS, hold RESET
    res = res ? res : lp875484_init(dev, d->subdev, I2C_AFE_PMIC);
    res = res ? res : lp875484_set_vout(dev, d->subdev, I2C_AFE_PMIC, 900);
    res = res ? res : dev_gpo_set(dev, IGPO_PWR_AFE, 0x3); // Enable VIOSYS, release RESET
    if (res)
        return res;

    bool afe_pg = false;
    for (unsigned j = 0; j < 100; j++) {
        res = lp875484_is_pg(dev, d->subdev, I2C_AFE_PMIC, &afe_pg);
        if (res)
            return res;

        if (afe_pg)
            break;

        usleep(1000);
    }
    if (!afe_pg) {
        USDR_LOG("DSDR", USDR_LOG_ERROR, "DCDC 0.9V isn't good, giving up!\n");
        return -EIO;
    }

    res = res ? res : dev_gpo_set(dev, IGPO_PWR_AFE, 0x7); // Enable DCDC 1.2V;
    // We don't have PG_1v2 routed in this rev
    // We don't have EN_1v8 routed in this rev

    if (d->type == DSDR_PCIE_HIPER_R0) {
        usleep(25000);
        res = res ? res : dev_gpo_set(dev, IGPO_PWR_AFE, 0xf);
    }

    // Check PG_1v8
    for (unsigned j = 0; j < 100; j++) {
        res = dev_gpi_get32(dev, IGPI_PGOOD, &pgdat);
        if (pgdat & (1 << 6))
            break;

        usleep(1000);
    }
    if (!(pgdat & (1 << 6))) {
        USDR_LOG("DSDR", USDR_LOG_ERROR, "DCDC 1.8V isn't good, giving up!\n");
        return -EIO;
    }

    res = res ? res : dev_gpo_set(dev, IGPO_PWR_AFE, 0x1f);
    if (res)
        return res;

    // Test AFE chip
    USDR_LOG("DSDR", USDR_LOG_ERROR, "AFE is powered up!\n");

    usleep(100000);

    res = res ? res : dev_gpo_set(dev, IGPO_AFE_RST, 0x0);
    res = res ? res : dev_gpo_set(dev, IGPO_AFE_RST, 0x1);

    usleep(100000);


    res = res ? res : afe79xx_create(dev, d->subdev, 0, &d->st);
    if (res == 0) {
        res = res ? res : usdr_jesd204b_bringup_pre(d);


        // sleep(1);
        usleep(10000);

        USDR_LOG("DSDR", USDR_LOG_ERROR, "Initializing AFE...\n");

        res = res ? res : afe79xx_init(&d->st, d->afecongiguration);

        res = res ? res : usdr_jesd204b_bringup_post(d);
    }

    if (d->type == DSDR_PCIE_HIPER_R0) {
        //res = res ? res :
                  dsdr_hiper_fe_create(dev, SPI_BUS_HIPER_FE, &d->hiper);
    }
    return 0;
}

static
int usdr_device_m2_dsdr_create_stream(device_t* dev, const char* sid, const char* dformat,
                                              uint64_t channels, unsigned pktsyms,
                                              unsigned flags, stream_handle_t** out_handle)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)dev;
    int res = 0;
    unsigned hwchs;

    if (strstr(sid, "rx") != NULL) {
        if (d->rx) {
            return -EBUSY;
        }

        // Channels remap
        uint64_t remap_msk;
        uint8_t  remap_cfg;

        switch (channels) {
        case 1: remap_msk = 1; remap_cfg = 0; break; // 0001 A
        case 2: remap_msk = 1; remap_cfg = 1; break; // 0010 B
        case 4: remap_msk = 1; remap_cfg = 2; break; // 0100 C
        case 8: remap_msk = 1; remap_cfg = 3; break; // 1000 D

        case 3: remap_msk = 3; remap_cfg = 4 * 1 + 0; break; // 0011 B+A
        case 5: remap_msk = 3; remap_cfg = 4 * 2 + 0; break; // 0101 C+A
        case 6: remap_msk = 3; remap_cfg = 4 * 2 + 1; break; // 0110 C+B
        case 9: remap_msk = 3; remap_cfg = 4 * 3 + 0; break; // 1001 D+A
        case 10: remap_msk = 3; remap_cfg = 4 * 3 + 1; break; // 1010 D+B
        case 12: remap_msk = 3; remap_cfg = 4 * 3 + 2; break; // 1100 D+C

        default:
            USDR_LOG("UDEV", USDR_LOG_ERROR, "Unsupported rx channel mask %08llx!\n", (long long)channels);
            return -EINVAL;
        }

        USDR_LOG("UDEV", USDR_LOG_INFO, "DSDR channels %08x remmaped to %08x with %08x mux\n",
                 (unsigned)channels, (unsigned)remap_msk, remap_cfg);
        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_RX_MAP, remap_cfg);

        uint64_t v;
        for (unsigned ch = 0; ch < 4; ch++) {
            d->st.libcapi79xx_get_nco(&d->st.capi, NCO_RX, ch, &v, 0, 0);

            USDR_LOG("UDEV", USDR_LOG_INFO, "RX NCO[%d] = %lld\n", ch, (long long)v);
        }


        struct sfetrx4_config rxcfg;
        res = parse_sfetrx4(dformat, remap_msk, pktsyms, &rxcfg);
        if (res) {
            USDR_LOG("UDEV", USDR_LOG_ERROR, "Unable to parse RX stream configuration!\n");
            return res;
        }

        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_DSPCHAIN_RST, 0x1);
        usleep(1000);
        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_DSPCHAIN_RST, 0x0);

        res = (res) ? res : create_sfetrx4_stream(dev, CORE_SFERX_DMA32_R0, dformat, remap_msk, pktsyms,
                                    flags, M2PCI_REG_WR_RXDMA_CONFIRM, VIRT_CFG_SFX_BASE,
                                    SRF4_FIFOBSZ, CSR_RFE4_BASE, &d->rx, &hwchs);
        if (res) {
            return res;
        }
        *out_handle = d->rx;
    } else if (strstr(sid, "tx") != NULL) {
        if (d->tx) {
            return -EBUSY;
        }

        uint8_t remap_cfg = 0;
        uint64_t remap_msk = 1;

        switch (channels) {
        case 3:
        case 6:
        case 12:
            remap_cfg = 0x44;
            remap_msk = 3;
            break;
        }

        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_TX_MAP, remap_cfg);

        struct sfetrx4_config txcfg;
        res = (res) ? res : parse_sfetrx4(dformat, remap_msk, pktsyms, &txcfg);
        if (res) {
            USDR_LOG("UDEV", USDR_LOG_ERROR, "Unable to parse TX stream configuration!\n");
            return res;
        }

        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_DSPCHAIN_TX_RST, 0x1);
        usleep(1000);
        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_DSPCHAIN_TX_RST, 0x0);


        res = (res) ? res :create_sfetrx4_stream(dev, CORE_SFETX_DMA32_R0, dformat, remap_msk, pktsyms,
                                    flags | DMS_DONT_CHECK_FWID, M2PCI_REG_WR_TXDMA_CNF_L, VIRT_CFG_SFX_BASE + 512,
                                    0, 0, &d->tx, &hwchs);
        if (res) {
            return res;
        }
        *out_handle = d->tx;
    }

    return res;
}

static
int usdr_device_m2_dsdr_unregister_stream(device_t* dev, stream_handle_t* stream)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)dev;
    if (stream == d->rx) {
        d->rx = NULL;
    }
    return -EINVAL;
}

static
int usdr_device_m2_dsdr_create(lldev_t dev, device_id_t devid)
{
    int res;

    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)malloc(sizeof(struct dev_m2_dsdr));
    res = usdr_device_base_create(&d->base, dev);
    if (res) {
        goto failed_free;
    }

    res = vfs_add_const_i64_vec(&d->base.rootfs,
                                s_params_m2_dsdr_rev000,
                                SIZEOF_ARRAY(s_params_m2_dsdr_rev000));
    if (res)
        goto failed_tree_creation;

    res = usdr_vfs_obj_param_init_array(&d->base,
                                        s_fparams_m2_dsdr_rev000,
                                        SIZEOF_ARRAY(s_fparams_m2_dsdr_rev000));
    if (res)
        goto failed_tree_creation;

    d->base.initialize = &usdr_device_m2_dsdr_initialize;
    d->base.destroy = &usdr_device_m2_dsdr_destroy;
    d->base.create_stream = &usdr_device_m2_dsdr_create_stream;
    d->base.unregister_stream = &usdr_device_m2_dsdr_unregister_stream;
    d->base.timer_op = &sfetrx4_stream_sync;

    d->rx = NULL;
    d->tx = NULL;

    d->type = DSDR_PCIE_HIPER_R0;
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
    usdr_device_m2_dsdr_create,
};

int usdr_device_register_m2_dsdr()
{
    return usdr_device_register(M2_DSDR_DEVICE_ID_C, &s_ops);
}
