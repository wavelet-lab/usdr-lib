// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <usdr_port.h>
#include <usdr_lowlevel.h>
#include <usdr_logging.h>

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>


#include "../device.h"
#include "../device_vfs.h"
#include "../device_names.h"
#include "../device_cores.h"
#include "../device_ids.h"
#include "../dev_param.h"

#include "../ipblks/streams/stream_sfetrx4_dma32.h"
#include "../ipblks/fgearbox.h"

#include "../generic_usdr/generic_regs.h"

#include "../hw/tps6381x/tps6381x.h"
#include "../hw/lmk05318/lmk05318.h"
#include "../hw/lp875484/lp875484.h"
#include "../hw/afe79xx/afe79xx.h"
#include "../hw/tmp114/tmp114.h"

#include "dsdr_hiper.h"

// Board revisions:
//  1 - DSDR
//  2 - Hiper
//

enum dsdr_type {
    DSDR_KCU116_EVM = 0xc0,
    DSDR_M2_R0 = 0xc2,
    DSDR_M2_R1 = 0xce,
    DSDR_PCIE_HIPER_R0 = 0xcf,
};

enum dsdr_jesdv {
    DSDR_JESD204B_810_245 = 1,
    DSDR_JESD204C_6664_245 = 7,
    DSDR_JESD204C_6664_491 = 3,
};

enum dsdr_jesd_config {
    JESD_MODE_4X_4X_491 = 0,
    JESD_MODE_8X_8X_369 = 1,
    JESD_MODE_8X_4X_491 = 8,
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
    I2C_TEMP_AFE   = MAKE_LSOP_I2C_ADDR(0, 0, I2C_DEV_TMP114NB), // Since M.2 rev1
    I2C_TEMP_FPGA  = MAKE_LSOP_I2C_ADDR(0, 1, I2C_DEV_TMP114NB), // Since M.2 rev1
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
    IGPI_JESD_AUX         = 39,

    IGPI_JESD_FPGA_ERR_0  = 40,
    IGPI_JESD_FPGA_ERR_1  = 44,
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

    //IGPO_RX_IQS = 38,

    IGPO_TX_CHEN = 39,
    IGPO_RX_CHEN = 40,

    IGPO_TX_AFETDD = 41,
    IGPO_RX_AFETDD = 42,

};

enum {
    DSDR_CHANS_LOGIC = 8,
    DSDR_CHANS_HW = 4,
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
    { DNLL_BUCKET_COUNT, 1 },
    { DNLL_GPO_COUNT, 1 },
    { DNLL_GPI_COUNT, 1 },

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
    // { "/ll/qspi_flash/master_off", },

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

    { "/ll/qspi_flash/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_QSPI_FLASH_24_RW) },
    { "/ll/qspi_flash/base", M2PCI_REG_QSPI_FLASH },
    { "/ll/qspi_flash/master_off", 0x600000 },

    { "/ll/gpi/0/core", USDR_MAKE_COREID(USDR_CS_GPI, USDR_GPI_32BIT_12) },
    { "/ll/gpi/0/base", M2PCI_REG_RD_GPI0_12 },
    { "/ll/gpo/0/core", USDR_MAKE_COREID(USDR_CS_GPO, USDR_GPO_8BIT) },
    { "/ll/gpo/0/base", M2PCI_REG_STAT_CTRL },

    // data stream cores
    { "/ll/srx/0/core",    USDR_MAKE_COREID(USDR_CS_STREAM, USDR_SC_RXDMA_BRSTN) },
    { "/ll/srx/0/base",    M2PCI_REG_WR_RXDMA_CONFIRM},
    { "/ll/srx/0/cfg_base",VIRT_CFG_SFX_BASE },
    { "/ll/srx/0/irq",     M2PCI_INT_RX},
    { "/ll/srx/0/dmacap",  0x855 },
    { "/ll/srx/0/rfe",     (uintptr_t)"/ll/rfe/0" },

    { "/ll/rfe/0/fifobsz", SRF4_FIFOBSZ },
    { "/ll/rfe/0/core",    USDR_MAKE_COREID(USDR_CS_FE, USDR_EXFC_BRSTN) },
    { "/ll/rfe/0/base",    CSR_RFE4_BASE },

    { "/ll/stx/0/core",    USDR_MAKE_COREID(USDR_CS_STREAM, USDR_SC_TXDMA_OLD) },
    { "/ll/stx/0/base",    M2PCI_REG_WR_TXDMA_CFG0},
    { "/ll/stx/0/cfg_base",VIRT_CFG_SFX_BASE + 512 },
    { "/ll/stx/0/irq",     M2PCI_INT_TX},
    { "/ll/stx/0/dmacap",  0x855 },
    { "/ll/srx/0/tfe",     (uintptr_t)"/ll/tfe/0" },
    { "/ll/tfe/0/core",    USDR_MAKE_COREID(USDR_CS_FE, USDR_TXSFE) },
    { "/ll/tfe/0/base",    CSR_TFE4_BASE },

    { "/ll/bucket/0/core", USDR_MAKE_COREID(USDR_CS_BUCKET, USDR_BUCKET_16B) },
    { "/ll/bucket/0/base", M2PCI_REG_WR_PNTFY_CFG },

    { "/ll/sync/0/core",   USDR_MAKE_COREID(USDR_CS_SYNC, USDR_SYNC_SIMPLE) },
    { "/ll/sync/0/base",   M2PCI_REG_WR_SYNC_CTRL},

    { "/ll/sdr/0/rfic/0", (uintptr_t)"afe79xx" },
    { "/ll/device/name",  (uintptr_t)"dsdr"},
};

static int dev_m2_dsdr_rate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_dsdr_rate_m_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_dsdr_rate_m_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dev_m2_dsdr_rx_enchan(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_dsdr_tx_enchan(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_dsdr_gain_tx_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_dsdr_gain_rx_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_dsdr_gain_rx_auto_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_dsdr_gain_rx_lna_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_dsdr_gain_rx_pga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

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
static int dev_m2_dsdr_sdr_rx_dsa_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_dsdr_sdr_tx_dsa_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_dsdr_afe_health_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dev_m2_dsdr_dummy(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return 0;
}

static int _debug_lmk05318_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int _debug_lmk05318_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);


static int dev_m2_dsdr_sdr_rx_remap_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_dsdr_sdr_rx_remap_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dev_m2_dsdr_sdr_tx_remap_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_dsdr_sdr_tx_remap_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

// TODO extend to arbitary number of channels
typedef uint64_t chmsk_t;
#define MAX_CHANNEL_NUMBER 64

bool chmsk_is_set(chmsk_t* msk, unsigned channel)
{
    return (*msk) & (1ull << channel);
}

bool chmsk_is_empty(chmsk_t* msk)
{
    return *msk == 0;
}

void chmsk_set_all(chmsk_t* msk) {
    *msk = ~((uint64_t)0);
}

static int device_path_to_chmsk(const char* full_path, const char* basename, const channel_map_info_t* map, const unsigned max_lchan, chmsk_t *hw_mask, chmsk_t *lg_mask);

static int dev_m2_dsdr_hrxnumchans_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dev_m2_dsdr_htxnumchans_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dev_m2_dsdr_lrxnumchans_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dev_m2_dsdr_ltxnumchans_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static
const usdr_dev_param_func_t s_fparams_m2_dsdr_rev000[] = {
    { "/ll/sdr/max_hw_rx_chans",  { NULL, dev_m2_dsdr_hrxnumchans_get } },
    { "/ll/sdr/max_hw_tx_chans",  { NULL, dev_m2_dsdr_htxnumchans_get } },

    { "/ll/sdr/max_sw_rx_chans",  { NULL, dev_m2_dsdr_lrxnumchans_get } },
    { "/ll/sdr/max_sw_tx_chans",  { NULL, dev_m2_dsdr_ltxnumchans_get } },

    { "/dm/rate/master",          { dev_m2_dsdr_rate_set, NULL }},
    { "/dm/rate/rxtxadcdac",      { dev_m2_dsdr_rate_m_set, dev_m2_dsdr_rate_m_get }},

    { "/dm/sdr/0/rx_enchan",      { dev_m2_dsdr_rx_enchan, NULL }},
    { "/dm/sdr/0/tx_enchan",      { dev_m2_dsdr_tx_enchan, NULL }},

    { "/dm/sdr/0/rx/remap",       { dev_m2_dsdr_sdr_rx_remap_set, dev_m2_dsdr_sdr_rx_remap_get }},
    { "/dm/sdr/0/tx/remap",       { dev_m2_dsdr_sdr_tx_remap_set, dev_m2_dsdr_sdr_tx_remap_get }},

    { "/dm/sdr/0/rx/gain",        { dev_m2_dsdr_gain_rx_set, NULL }},
    { "/dm/sdr/0/rx/gain/0",      { dev_m2_dsdr_gain_rx_set, NULL }},
    { "/dm/sdr/0/rx/gain/1",      { dev_m2_dsdr_gain_rx_set, NULL }},
    { "/dm/sdr/0/rx/gain/2",      { dev_m2_dsdr_gain_rx_set, NULL }},
    { "/dm/sdr/0/rx/gain/3",      { dev_m2_dsdr_gain_rx_set, NULL }},
    { "/dm/sdr/0/rx/gain/4",      { dev_m2_dsdr_gain_rx_set, NULL }},
    { "/dm/sdr/0/rx/gain/5",      { dev_m2_dsdr_gain_rx_set, NULL }},
    { "/dm/sdr/0/rx/gain/6",      { dev_m2_dsdr_gain_rx_set, NULL }},
    { "/dm/sdr/0/rx/gain/7",      { dev_m2_dsdr_gain_rx_set, NULL }},

    { "/dm/sdr/0/rx/gain/auto",   { dev_m2_dsdr_gain_rx_auto_set, NULL }},
    { "/dm/sdr/0/rx/gain/auto/0", { dev_m2_dsdr_gain_rx_auto_set, NULL }},
    { "/dm/sdr/0/rx/gain/auto/1", { dev_m2_dsdr_gain_rx_auto_set, NULL }},
    { "/dm/sdr/0/rx/gain/auto/2", { dev_m2_dsdr_gain_rx_auto_set, NULL }},
    { "/dm/sdr/0/rx/gain/auto/3", { dev_m2_dsdr_gain_rx_auto_set, NULL }},
    { "/dm/sdr/0/rx/gain/auto/4", { dev_m2_dsdr_gain_rx_auto_set, NULL }},
    { "/dm/sdr/0/rx/gain/auto/5", { dev_m2_dsdr_gain_rx_auto_set, NULL }},
    { "/dm/sdr/0/rx/gain/auto/6", { dev_m2_dsdr_gain_rx_auto_set, NULL }},
    { "/dm/sdr/0/rx/gain/auto/7", { dev_m2_dsdr_gain_rx_auto_set, NULL }},

    { "/dm/sdr/0/rx/gain/lna",    { dev_m2_dsdr_gain_rx_lna_set, NULL }},
    { "/dm/sdr/0/rx/gain/lna/0",  { dev_m2_dsdr_gain_rx_lna_set, NULL }},
    { "/dm/sdr/0/rx/gain/lna/1",  { dev_m2_dsdr_gain_rx_lna_set, NULL }},
    { "/dm/sdr/0/rx/gain/lna/2",  { dev_m2_dsdr_gain_rx_lna_set, NULL }},
    { "/dm/sdr/0/rx/gain/lna/3",  { dev_m2_dsdr_gain_rx_lna_set, NULL }},
    { "/dm/sdr/0/rx/gain/lna/4",  { dev_m2_dsdr_gain_rx_lna_set, NULL }},
    { "/dm/sdr/0/rx/gain/lna/5",  { dev_m2_dsdr_gain_rx_lna_set, NULL }},
    { "/dm/sdr/0/rx/gain/lna/6",  { dev_m2_dsdr_gain_rx_lna_set, NULL }},
    { "/dm/sdr/0/rx/gain/lna/7",  { dev_m2_dsdr_gain_rx_lna_set, NULL }},

    { "/dm/sdr/0/rx/gain/pga",    { dev_m2_dsdr_gain_rx_pga_set, NULL }},
    { "/dm/sdr/0/rx/gain/pga/0",  { dev_m2_dsdr_gain_rx_pga_set, NULL }},
    { "/dm/sdr/0/rx/gain/pga/1",  { dev_m2_dsdr_gain_rx_pga_set, NULL }},
    { "/dm/sdr/0/rx/gain/pga/2",  { dev_m2_dsdr_gain_rx_pga_set, NULL }},
    { "/dm/sdr/0/rx/gain/pga/3",  { dev_m2_dsdr_gain_rx_pga_set, NULL }},
    { "/dm/sdr/0/rx/gain/pga/4",  { dev_m2_dsdr_gain_rx_pga_set, NULL }},
    { "/dm/sdr/0/rx/gain/pga/5",  { dev_m2_dsdr_gain_rx_pga_set, NULL }},
    { "/dm/sdr/0/rx/gain/pga/6",  { dev_m2_dsdr_gain_rx_pga_set, NULL }},
    { "/dm/sdr/0/rx/gain/pga/7",  { dev_m2_dsdr_gain_rx_pga_set, NULL }},

    { "/dm/sdr/0/tx/gain",        { dev_m2_dsdr_gain_tx_set, NULL }},
    { "/dm/sdr/0/tx/gain/0",      { dev_m2_dsdr_gain_tx_set, NULL }},
    { "/dm/sdr/0/tx/gain/1",      { dev_m2_dsdr_gain_tx_set, NULL }},
    { "/dm/sdr/0/tx/gain/2",      { dev_m2_dsdr_gain_tx_set, NULL }},
    { "/dm/sdr/0/tx/gain/3",      { dev_m2_dsdr_gain_tx_set, NULL }},
    { "/dm/sdr/0/tx/gain/4",      { dev_m2_dsdr_gain_tx_set, NULL }},
    { "/dm/sdr/0/tx/gain/5",      { dev_m2_dsdr_gain_tx_set, NULL }},
    { "/dm/sdr/0/tx/gain/6",      { dev_m2_dsdr_gain_tx_set, NULL }},
    { "/dm/sdr/0/tx/gain/7",      { dev_m2_dsdr_gain_tx_set, NULL }},

    { "/dm/sdr/0/rx/freqency",    { dev_m2_dsdr_sdr_rx_freq_set, NULL }},
    { "/dm/sdr/0/rx/freqency/0",  { dev_m2_dsdr_sdr_rx_freq_set, NULL }},
    { "/dm/sdr/0/rx/freqency/1",  { dev_m2_dsdr_sdr_rx_freq_set, NULL }},
    { "/dm/sdr/0/rx/freqency/2",  { dev_m2_dsdr_sdr_rx_freq_set, NULL }},
    { "/dm/sdr/0/rx/freqency/3",  { dev_m2_dsdr_sdr_rx_freq_set, NULL }},
    { "/dm/sdr/0/rx/freqency/4",  { dev_m2_dsdr_sdr_rx_freq_set, NULL }},
    { "/dm/sdr/0/rx/freqency/5",  { dev_m2_dsdr_sdr_rx_freq_set, NULL }},
    { "/dm/sdr/0/rx/freqency/6",  { dev_m2_dsdr_sdr_rx_freq_set, NULL }},
    { "/dm/sdr/0/rx/freqency/7",  { dev_m2_dsdr_sdr_rx_freq_set, NULL }},

    { "/dm/sdr/0/rx/dsa",         { dev_m2_dsdr_sdr_rx_dsa_set, NULL }},
    { "/dm/sdr/0/rx/dsa/0",       { dev_m2_dsdr_sdr_rx_dsa_set, NULL }},
    { "/dm/sdr/0/rx/dsa/1",       { dev_m2_dsdr_sdr_rx_dsa_set, NULL }},
    { "/dm/sdr/0/rx/dsa/2",       { dev_m2_dsdr_sdr_rx_dsa_set, NULL }},
    { "/dm/sdr/0/rx/dsa/3",       { dev_m2_dsdr_sdr_rx_dsa_set, NULL }},
    { "/dm/sdr/0/rx/dsa/4",       { dev_m2_dsdr_sdr_rx_dsa_set, NULL }},
    { "/dm/sdr/0/rx/dsa/5",       { dev_m2_dsdr_sdr_rx_dsa_set, NULL }},
    { "/dm/sdr/0/rx/dsa/6",       { dev_m2_dsdr_sdr_rx_dsa_set, NULL }},
    { "/dm/sdr/0/rx/dsa/7",       { dev_m2_dsdr_sdr_rx_dsa_set, NULL }},

    { "/dm/sdr/0/tx/freqency",    { dev_m2_dsdr_sdr_tx_freq_set, NULL }},
    { "/dm/sdr/0/tx/freqency/0",  { dev_m2_dsdr_sdr_tx_freq_set, NULL }},
    { "/dm/sdr/0/tx/freqency/1",  { dev_m2_dsdr_sdr_tx_freq_set, NULL }},
    { "/dm/sdr/0/tx/freqency/2",  { dev_m2_dsdr_sdr_tx_freq_set, NULL }},
    { "/dm/sdr/0/tx/freqency/3",  { dev_m2_dsdr_sdr_tx_freq_set, NULL }},
    { "/dm/sdr/0/tx/freqency/4",  { dev_m2_dsdr_sdr_tx_freq_set, NULL }},
    { "/dm/sdr/0/tx/freqency/5",  { dev_m2_dsdr_sdr_tx_freq_set, NULL }},
    { "/dm/sdr/0/tx/freqency/6",  { dev_m2_dsdr_sdr_tx_freq_set, NULL }},
    { "/dm/sdr/0/tx/freqency/7",  { dev_m2_dsdr_sdr_tx_freq_set, NULL }},

    { "/dm/sdr/0/tx/dsa",         { dev_m2_dsdr_sdr_tx_dsa_set, NULL }},
    { "/dm/sdr/0/tx/dsa/0",       { dev_m2_dsdr_sdr_tx_dsa_set, NULL }},
    { "/dm/sdr/0/tx/dsa/1",       { dev_m2_dsdr_sdr_tx_dsa_set, NULL }},
    { "/dm/sdr/0/tx/dsa/2",       { dev_m2_dsdr_sdr_tx_dsa_set, NULL }},
    { "/dm/sdr/0/tx/dsa/3",       { dev_m2_dsdr_sdr_tx_dsa_set, NULL }},
    { "/dm/sdr/0/tx/dsa/4",       { dev_m2_dsdr_sdr_tx_dsa_set, NULL }},
    { "/dm/sdr/0/tx/dsa/5",       { dev_m2_dsdr_sdr_tx_dsa_set, NULL }},
    { "/dm/sdr/0/tx/dsa/6",       { dev_m2_dsdr_sdr_tx_dsa_set, NULL }},
    { "/dm/sdr/0/tx/dsa/7",       { dev_m2_dsdr_sdr_tx_dsa_set, NULL }},


    { "/dm/sdr/0/rx/bandwidth",   { dev_m2_dsdr_dummy, NULL }},
    { "/dm/sdr/0/rx/path",        { dev_m2_dsdr_dummy, NULL }},

    { "/dm/sdr/0/tx/gain",        { dev_m2_dsdr_dummy, NULL }},
    { "/dm/sdr/0/tx/bandwidth",   { dev_m2_dsdr_dummy, NULL }},
    { "/dm/sdr/0/tx/path",        { dev_m2_dsdr_dummy, NULL }},

    { "/dm/sdr/0/afe_health",     { dev_m2_dsdr_dummy, dev_m2_dsdr_afe_health_get }},

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

// Maximum supported simultanious logical channels (inc. multiband configurations)
#define MAX_LOGIC_CHANS      8
#define MAX_HIPER_FE_PORT    4
#define MAX_PORT_BANDS       2

// HIPER FE channel map table
static const uint8_t s_chanmap_hw_to_fe[MAX_HIPER_FE_PORT] = { 2, 3, 1, 0 };
// static const uint8_t s_chanmap_fe_to_hw[MAX_HIPER_FE_PORT] = { 3, 2, 0, 1 };

enum DSDR_STATE {
    STATE_IDLE = 0,
    STATE_AFE_INIT = 1,
};

struct channel_logic_dsp_wire {
    uint8_t dsp_type;
    uint8_t hwport;
    uint8_t band;
};
typedef struct channel_logic_dsp_wire channel_logic_dsp_wire_t;


static const channel_logic_dsp_wire_t s_dsdr_lmap_s_nco[] = {
    {  NCO_RX, 0, 0 },
    {  NCO_RX, 1, 0 },
    {  NCO_RX, 2, 0 },
    {  NCO_RX, 3, 0 },
};
static const channel_map_info_t s_dsdr_chmap_s_nco[] = {
    { "a", 0 },
    { "b", 1 },
    { "c", 2 },
    { "d", 3 },
    { NULL, CH_NULL },
};

static const channel_logic_dsp_wire_t s_dsdr_lmap_d_nco[] = {
    {  NCO_RX, 0, 0 },
    {  NCO_RX, 1, 0 },
    {  NCO_RX, 2, 0 },
    {  NCO_RX, 3, 0 },
    {  NCO_RX, 0, 1 },
    {  NCO_RX, 1, 1 },
    {  NCO_RX, 2, 1 },
    {  NCO_RX, 3, 1 },
};
static const channel_map_info_t s_dsdr_chmap_d_nco[] = {
    { "a0", 0 },
    { "b0", 1 },
    { "c0", 2 },
    { "d0", 3 },
    { "a1", 4 },
    { "b1", 5 },
    { "c1", 6 },
    { "d1", 7 },
    { NULL, CH_NULL },
};

static const channel_logic_dsp_wire_t s_dsdr_lmap_s_nco_fb[] = {
    {  NCO_RX, 0, 0 },
    {  NCO_RX, 1, 0 },
    {  NCO_RX, 2, 0 },
    {  NCO_RX, 3, 0 },
    {  NCO_FB, 4, 0 },
    {  NCO_FB, 5, 0 },
};
static const channel_map_info_t s_dsdr_chmap_s_nco_fb[] = {
    { "a", 0 },
    { "b", 1 },
    { "c", 2 },
    { "d", 3 },
    { "f0", 4 },
    { "f1", 5 },
    { NULL, CH_NULL },
};

struct dev_m2_dsdr {
    device_t base;

    lmk05318_state_t lmk;

    subdev_t subdev;

    unsigned type;
    unsigned jesdv;
    unsigned jesd_x8;

    stream_handle_t* rx;
    stream_handle_t* tx;

    afe79xx_state_t st;
    dsdr_hiper_fe_t hiper;

    uint32_t dsdr_state;
    uint32_t cfg_afe_type;
    uint32_t cfg_rx_lanemap;
    uint32_t cfg_tx_lanemap;

    uint32_t debug_lmk05318_last;

    const char* afecongiguration;
    uint32_t max_rate; // Maximum I/Q rate supported by HW

    const channel_map_info_t *rx_chmap_info;
    const channel_map_info_t *tx_chmap_info;
    const channel_logic_dsp_wire_t *rx_lmap_info;
    const channel_logic_dsp_wire_t *tx_lmap_info;

    unsigned hw_enabled_tx; // HW Enabled channels
    unsigned hw_enabled_rx; // HW Enabled channels
    unsigned logic_enabled_tx; // Logic Enabled channels
    unsigned logic_enabled_rx; // Logic Enabled channels

    unsigned hw_mask_tx; // Physically wired TX channels
    unsigned hw_mask_rx; // Physically wired RX channels

    unsigned hw_chcnt_rx;
    unsigned hw_chcnt_tx;
    unsigned logic_chcnt_rx;
    unsigned logic_chcnt_tx;

    unsigned hw_fpga_jesd_rx_en; // Physical lanes enabled bitmask 0: X0Y4, 1: X0Y5, ... 3: X0Y7
    unsigned hw_fpga_jesd_tx_en; // Physical lanes enabled bitmask 0: X0Y4, 1: X0Y5, ... 3: X0Y7

    uint8_t hw_rxch_route[MAX_LOGIC_CHANS]; // Physical channel reroute dueto absent physical channels (like in AFE7903)
    uint8_t hw_txch_route[MAX_LOGIC_CHANS]; // Physical channel reroute dueto absent physical channels (like in AFE7903)

    uint32_t adc_rate;
    unsigned rxbb_rate;
    unsigned rxbb_decim;

    uint32_t dac_rate;
    unsigned txbb_rate;
    unsigned txbb_inter;

    uint8_t tx_activated;
    uint8_t rx_activated;

    // 0xff means channel not wired
    uint8_t rx_ordinal_to_logic[MAX_LOGIC_CHANS];
    uint8_t tx_ordinal_to_logic[MAX_LOGIC_CHANS];
    //uint8_t tx_hw_to_logic[MAX_LOGIC_CHANS]; // hw -> logic, index is hw chnum

    // Configuration parameters
    opt_u64_t rx_ord_freqs[MAX_LOGIC_CHANS];
    opt_u64_t tx_ord_freqs[MAX_LOGIC_CHANS];

    // For proper FE configuration in dual-band DSP configuration indexed by hwidx, band_no
    opt_u64_t rx_bxfc[MAX_HIPER_FE_PORT][MAX_PORT_BANDS];
    opt_u64_t tx_bxfc[MAX_HIPER_FE_PORT][MAX_PORT_BANDS];
    opt_u64_t rx_raw_nco[MAX_HIPER_FE_PORT][MAX_PORT_BANDS];
    opt_u64_t tx_raw_nco[MAX_HIPER_FE_PORT][MAX_PORT_BANDS];

    channel_info_t rx_chans;
    channel_info_t tx_chans;

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

bool dev_m2_dsdr_has_hiper(dev_m2_dsdr_t* d)
{
    return d->type == DSDR_PCIE_HIPER_R0;
}


int dev_m2_dsdr_lrxnumchans_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dev_m2_dsdr_t *d = (dev_m2_dsdr_t *)ud;
    return d->logic_chcnt_rx;
}

int dev_m2_dsdr_ltxnumchans_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dev_m2_dsdr_t *d = (dev_m2_dsdr_t *)ud;
    return d->logic_chcnt_tx;
}

int dev_m2_dsdr_htxnumchans_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dev_m2_dsdr_t *d = (dev_m2_dsdr_t *)ud;
    return d->hw_chcnt_tx;
}

int dev_m2_dsdr_hrxnumchans_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dev_m2_dsdr_t *d = (dev_m2_dsdr_t *)ud;
    return d->hw_chcnt_rx;
}

int dev_m2_dsdr_rx_enchan(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_m2_dsdr_t *d = (dev_m2_dsdr_t *)ud;
    return dev_gpo_set(d->base.dev, IGPO_RX_CHEN, value);

}
int dev_m2_dsdr_tx_enchan(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_m2_dsdr_t *d = (dev_m2_dsdr_t *)ud;
    return dev_gpo_set(d->base.dev, IGPO_TX_CHEN, value);
}

static int dsdr_update_rx_remap(dev_m2_dsdr_t* d)
{
    // Mark corresponding RX chanel
    uint64_t hiper_cfg_msk = 0;
    //unsigned rx_remap  = 0;
    int res = 0;

    for (unsigned i = 0; i < MAX_LOGIC_CHANS; i++) {
        if (d->rx_ordinal_to_logic[i] != 0xff) {
            //rx_remap |= (d->rx_ordinal_to_logic[i] & 0x3) << (2 * i);

            unsigned hw_chan = d->rx_lmap_info[d->rx_ordinal_to_logic[i]].hwport;
            if (hw_chan < MAX_HIPER_FE_PORT) {
                hiper_cfg_msk |= (1u << s_chanmap_hw_to_fe[hw_chan]);
            }
        }
    }

    if (dev_m2_dsdr_has_hiper(d)) {
        res = dsdr_hiper_fe_rx_chan_en(&d->hiper, hiper_cfg_msk);
    }
    // TODO issue FE cmd
    // return res ? res : dev_gpo_set(d->base.dev, IGPO_RX_MAP, rx_remap);
    return res;
}


static int dsdr_update_tx_remap(dev_m2_dsdr_t* d)
{
    // Mark corresponding TX chanel
    uint64_t hiper_cfg_msk = 0;
    //unsigned tx_remap  = 0;
    int res = 0;

    for (unsigned i = 0; i < 4; i++) {
        //if (d->tx_hw_to_logic[i] != 0xff) {
            //tx_remap |= (d->tx_hw_to_logic[i] & 0x3) << (2 * i);

            //unsigned hw_chan = d->tx_lmap_info[d->rx_ordinal_to_logic[i]].hwport;
            //if (hw_chan < MAX_ANT_PORT) {
            //
            //}
        //    hiper_cfg_msk |= (1u << s_chanmap_hw_to_fe[i]);
        //}

        if (d->tx_ordinal_to_logic[i] != 0xff) {
            //tx_remap |= (d->rx_ordinal_to_logic[i] & 0x3) << (2 * i);

            unsigned hw_chan = d->tx_lmap_info[d->tx_ordinal_to_logic[i]].hwport;
            if (hw_chan < MAX_HIPER_FE_PORT) {
                hiper_cfg_msk |= (1u << s_chanmap_hw_to_fe[hw_chan]);
            }
        }
    }

    if (dev_m2_dsdr_has_hiper(d)) {
        res = dsdr_hiper_fe_tx_chan_en(&d->hiper, hiper_cfg_msk);
    }
    // TODO issue FE cmd
    //return res ? res : dev_gpo_set(d->base.dev, IGPO_TX_MAP, tx_remap);
    return res;
}

// Function tries to parse and match names to logical channels and iterate with ordinal index
static int dsdr_iterate_ordinal_chans(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t val, const char* basename, bool rxchans)
{
    dev_m2_dsdr_t *d = (dev_m2_dsdr_t *)ud;
    vfs_object_t ph;
    chmsk_t logic_msk;
    chmsk_t logic_enabled;
    chmsk_t ordinal_msk;
    chmsk_t ordinal_enabled;
    int res = device_path_to_chmsk(obj->full_path, basename,
                                   rxchans ? d->rx_chmap_info : d->tx_chmap_info,
                                   rxchans ? d->logic_chcnt_rx : d->logic_chcnt_tx,
                                   &logic_msk, &ordinal_msk);
    if (res == -ENAVAIL) {
        // No channel information were specified, apply settings to all HW enabled channels or cache the value
        ordinal_msk = 0;
        logic_msk = 0;
        res = 0;
    } else if (res != 0) {
        return res;
    }

    if (rxchans) {
        logic_enabled = d->rx_activated ? d->logic_enabled_rx : 0xf;
    } else {
        logic_enabled = d->tx_activated ? d->logic_enabled_tx : 0xf;
    }

    ordinal_enabled = 0;
    if (logic_msk != 0) {
        logic_enabled &= logic_msk;
    }

    for (unsigned i = 0; i < (rxchans ? d->logic_chcnt_rx : d->logic_chcnt_tx); i++) {
        if (chmsk_is_set(&logic_enabled,  rxchans ? d->rx_ordinal_to_logic[i] : d->tx_ordinal_to_logic[i])) {
            ordinal_enabled |= (1 << i);
        }
    }
    if (ordinal_msk != 0) {
        ordinal_enabled &= ordinal_msk;
    }


    ph.type = obj->type;
    ph.object = obj->object;
    ph.data = obj->data;
    ph.ops = obj->ops;
    ph.full_path[0] = 0;

    USDR_LOG("HIPR", USDR_LOG_WARNING, "Setting parameter `%s` to LOGIC: %08x ORD: %08x chans\n",
             obj->full_path, (unsigned)logic_msk, (unsigned)ordinal_enabled);

    // We rely on ordinal numbers for API simplification
    for (unsigned i = 0; i < (rxchans ? d->logic_chcnt_rx : d->logic_chcnt_tx); i++) {
        if (chmsk_is_set(&ordinal_enabled, i)) {
            ph.full_path[1] = i;
            res = res ? res : obj->ops.si64(&ph, val);
        }
    }

#if 0
    for (unsigned i = 0; i < DSDR_CHANS_HW; i++) {
        if (chmsk_is_set(&hw_msk, i)) {
            ph.full_path[1] = rxchans ? d->hw_rxch_route[i] : d->hw_txch_route[i]; // i;
            res = res ? res : obj->ops.si64(&ph, val);
        }
    }


    for (unsigned i = 0; i < DSDR_CHANS_LOGIC; i++) {
        if (chmsk_is_set(&logic_msk, i)) {

            uint8_t map = (rxchans) ? d->rx_ordinal_to_logic[i] :  d->tx_ordinal_to_logic[i];
            if (map == 0xff) {
                // Channel disabled
                continue;
            }

            ph.full_path[1] = rxchans ? d->hw_rxch_route[i] : d->hw_txch_route[i]; //i;
            res = res ? res : obj->ops.si64(&ph, val);

        }
    }
#endif
    return res;
}

static const channel_logic_dsp_wire_t *get_chmapnfo_from_ordinal(dev_m2_dsdr_t* d, bool rx, unsigned ordinal)
{
    if (ordinal >= (rx ? d->logic_chcnt_rx : d->logic_chcnt_tx)) {
        return NULL;
    }
    uint8_t logic_num = (rx ? d->rx_ordinal_to_logic[ordinal] : d->tx_ordinal_to_logic[ordinal]);
    if (logic_num == 0xff) {
        return NULL;
    }
    return rx ? &d->rx_lmap_info[logic_num] : &d->tx_lmap_info[logic_num];
}

static int dsdr_set_rx_frequency_chan(dev_m2_dsdr_t* d, uint64_t freq, unsigned ord)
{
    int res = 0;
    opt_u64_set_val(&d->rx_ord_freqs[ord], freq);
    if (!d->rx_activated) {
        return 0;
    }

    // Ordinal to logic converter
    const channel_logic_dsp_wire_t* w = get_chmapnfo_from_ordinal(d, true, ord);
    if (!w) {
        return -EINVAL;
    }
    unsigned hwidx = d->hw_rxch_route[w->hwport];

    uint64_t ncoval = freq;
    if (dev_m2_dsdr_has_hiper(d) && (hwidx < MAX_HIPER_FE_PORT)) {
        bool ch_rxiq;
        bool mod = false;
        const uint8_t iter_shared_lo_chans[2][2] = { { 0, 1 }, { 2, 3 } };
        const uint8_t iter_shared_selector[4] = { 0, 0, 1, 1 };
        uint64_t chan_mid = 0;
        unsigned cnt = 0;
        int res = 0;
        const bool shared_lo_cfg_mode = true;

        opt_u64_set_val(&d->rx_bxfc[hwidx][w->band], freq);

        for (unsigned s = 0; s < (shared_lo_cfg_mode ? 2 : 1); s++) {
            unsigned iter_hwid = shared_lo_cfg_mode ? iter_shared_lo_chans[iter_shared_selector[hwidx]][s] : hwidx;
            for (unsigned j = 0; j < MAX_PORT_BANDS; j++) {
                if (d->rx_bxfc[iter_hwid][j].set) {
                    chan_mid += d->rx_bxfc[iter_hwid][j].value;
                    cnt++;
                }
            }
        }
        chan_mid /= cnt;

        for (unsigned s = 0; s < (shared_lo_cfg_mode ? 2 : 1); s++) {
            unsigned iter_hwid = shared_lo_cfg_mode ? iter_shared_lo_chans[iter_shared_selector[hwidx]][s] : hwidx;
            res = res ? res : dsdr_hiper_fe_rx_freq_set(&d->hiper, s_chanmap_hw_to_fe[iter_hwid], chan_mid, &ncoval, &ch_rxiq);
        }

        if (res)
            return res;

        for (unsigned k = 0; k < d->logic_chcnt_rx; k++) {
            uint8_t logic_ch = d->rx_ordinal_to_logic[k];
            if (logic_ch == 0xff)
                continue;

            unsigned hw_iter = d->hw_rxch_route[d->rx_lmap_info[logic_ch].hwport];
            for (unsigned s = 0; s < (shared_lo_cfg_mode ? 2 : 1); s++) {
                unsigned iter_hwid = shared_lo_cfg_mode ? iter_shared_lo_chans[iter_shared_selector[hwidx]][s] : hwidx;

                if (iter_hwid == hw_iter) {
                    uint8_t nchan = (ch_rxiq) ? d->rx_chans.ch_map[k] | CH_SWAP_IQ_FLAG : d->rx_chans.ch_map[k] & ~CH_SWAP_IQ_FLAG;
                    if (nchan != d->rx_chans.ch_map[k]) {
                        d->rx_chans.ch_map[k] = nchan;
                        mod = true;
                    }
                }
            }
        }
        if (mod) {
            res = res ? res : d->rx->ops->option_set(d->rx, "chmap", (uintptr_t)&d->rx_chans);
        }

        for (unsigned s = 0; s < (shared_lo_cfg_mode ? 2 : 1); s++) {
            unsigned iter_hwid = shared_lo_cfg_mode ? iter_shared_lo_chans[iter_shared_selector[hwidx]][s] : hwidx;

            for (unsigned j = 0; j < MAX_PORT_BANDS; j++) {
                if (!d->rx_bxfc[iter_hwid][j].set)
                    continue;

                uint64_t freq_req = d->rx_bxfc[iter_hwid][j].value;
                int64_t nco_offset = freq_req - chan_mid;
                uint64_t freq_mod = ch_rxiq ? (ncoval - nco_offset) : (ncoval + nco_offset);

                if (d->rx_raw_nco[iter_hwid][j].set && d->rx_raw_nco[iter_hwid][j].value == freq_mod)
                    continue;

                res = res ? res : d->st.libcapi79xx_upd_nco(&d->st.capi, NCO_RX, iter_hwid, freq_mod / 1000, 0, j);
                USDR_LOG("HIPR", USDR_LOG_WARNING, "CH[%d => %c Band%d] F=%.3f RX_NCO=%.3f MID_F=%.3f\n",
                         ord, iter_hwid + 'A', j, freq_req / 1.0e6, freq_mod / 1.0e6, chan_mid / 1.0e6);

                opt_u64_set_val(&d->rx_raw_nco[iter_hwid][j], freq_mod);
            }
        }

        return res;
    }

    USDR_LOG("HIPR", USDR_LOG_WARNING, "CH[%d => %c Band%d] F=%.3f RX_NCO=%.3f\n", ord, hwidx + 'A', w->band, freq / 1.0e6, ncoval / 1.0e6);
    res = res ? res : d->st.libcapi79xx_upd_nco(&d->st.capi, NCO_RX, hwidx, ncoval / 1000, 0, w->band);
    return res;
}

static int dsdr_set_tx_frequency_chan(dev_m2_dsdr_t* d, uint64_t freq, unsigned ord)
{
    int res = 0;
    opt_u64_set_val(& d->tx_ord_freqs[ord], freq);
    if (!d->tx_activated) {
        return 0;
    }

    // Ordinal to logic converter
    const channel_logic_dsp_wire_t* w = get_chmapnfo_from_ordinal(d, false, ord);
    if (!w) {
        return -EINVAL;
    }
    unsigned hwidx = d->hw_txch_route[w->hwport];

    uint64_t ncoval = freq;
    if (dev_m2_dsdr_has_hiper(d) && (hwidx < MAX_HIPER_FE_PORT)) {
        bool ch_txiq;
        bool mod = false;
        const uint8_t iter_shared_lo_chans[2][2] = { { 0, 1 }, { 2, 3 } };
        const uint8_t iter_shared_selector[4] = { 0, 0, 1, 1 };
        uint64_t chan_mid = 0;
        unsigned cnt = 0;
        int res = 0;
        const bool shared_lo_cfg_mode = true;

        opt_u64_set_val(&d->tx_bxfc[hwidx][w->band], freq);

        for (unsigned s = 0; s < (shared_lo_cfg_mode ? 2 : 1); s++) {
            unsigned iter_hwid = shared_lo_cfg_mode ? iter_shared_lo_chans[iter_shared_selector[hwidx]][s] : hwidx;
            for (unsigned j = 0; j < MAX_PORT_BANDS; j++) {
                if (d->tx_bxfc[iter_hwid][j].set) {
                    chan_mid += d->tx_bxfc[iter_hwid][j].value;
                    cnt++;
                }
            }
        }
        chan_mid /= cnt;

        for (unsigned s = 0; s < (shared_lo_cfg_mode ? 2 : 1); s++) {
            unsigned iter_hwid = shared_lo_cfg_mode ? iter_shared_lo_chans[iter_shared_selector[hwidx]][s] : hwidx;
            res = res ? res : dsdr_hiper_fe_tx_freq_set(&d->hiper, s_chanmap_hw_to_fe[iter_hwid], chan_mid, &ncoval, &ch_txiq);
        }

        if (res)
            return res;

        for (unsigned k = 0; k < d->logic_chcnt_tx; k++) {
            uint8_t logic_ch = d->tx_ordinal_to_logic[k];
            if (logic_ch == 0xff)
                continue;

            unsigned hw_iter = d->hw_txch_route[d->tx_lmap_info[logic_ch].hwport];
            for (unsigned s = 0; s < (shared_lo_cfg_mode ? 2 : 1); s++) {
                unsigned iter_hwid = shared_lo_cfg_mode ? iter_shared_lo_chans[iter_shared_selector[hwidx]][s] : hwidx;

                if (iter_hwid == hw_iter) {
                    uint8_t nchan = (ch_txiq) ? d->tx_chans.ch_map[k] | CH_SWAP_IQ_FLAG : d->tx_chans.ch_map[k] & ~CH_SWAP_IQ_FLAG;
                    if (nchan != d->tx_chans.ch_map[k]) {
                        d->tx_chans.ch_map[k] = nchan;
                        mod = true;
                    }
                }
            }
        }
        if (mod) {
            res = res ? res : d->tx->ops->option_set(d->tx, "chmap", (uintptr_t)&d->tx_chans);
        }

        for (unsigned s = 0; s < (shared_lo_cfg_mode ? 2 : 1); s++) {
            unsigned iter_hwid = shared_lo_cfg_mode ? iter_shared_lo_chans[iter_shared_selector[hwidx]][s] : hwidx;

            for (unsigned j = 0; j < MAX_PORT_BANDS; j++) {
                if (!d->tx_bxfc[iter_hwid][j].set)
                    continue;

                uint64_t freq_req = d->tx_bxfc[iter_hwid][j].value;
                int64_t nco_offset = freq_req - chan_mid;
                uint64_t freq_mod = ch_txiq ? (ncoval - nco_offset) : (ncoval + nco_offset);

                if (d->tx_raw_nco[iter_hwid][j].set && d->tx_raw_nco[iter_hwid][j].value == freq_mod)
                    continue;

                res = res ? res : d->st.libcapi79xx_upd_nco(&d->st.capi, NCO_TX, iter_hwid, freq_mod / 1000, 0, j);
                USDR_LOG("HIPR", USDR_LOG_WARNING, "CH[%d => %c Band%d] F=%.3f TX_NCO=%.3f MID_F=%.3f\n",
                         ord, iter_hwid + 'A', j, freq_req / 1.0e6, freq_mod / 1.0e6, chan_mid / 1.0e6);

                opt_u64_set_val(&d->tx_raw_nco[iter_hwid][j], freq_mod);
            }
        }

        return res;
    }

    USDR_LOG("HIPR", USDR_LOG_WARNING, "CH[%d => %c Band%d] F=%.3f TX_NCO=%.3f\n", ord, hwidx + 'A', w->band, freq / 1.0e6, ncoval / 1.0e6);
    res = res ? res : d->st.libcapi79xx_upd_nco(&d->st.capi, NCO_TX, hwidx, ncoval / 1000, 0, w->band);
    return res;
#if 0
    uint64_t ncoval = freq;
    if (dev_m2_dsdr_has_hiper(d)) {
        bool ch_txiq;
        bool mod = false;
        unsigned fe_chan = s_chanmap_hw_to_fe[chno];
        int res = dsdr_hiper_fe_tx_freq_set(&d->hiper, fe_chan, freq, &ncoval, &ch_txiq);
        if (res)
            return res;

       //  d->txbb_swap_iq = (ch_txiq) ? d->txbb_swap_iq | (1u << chno) : d->txbb_swap_iq & (~(1u << chno));
        for (unsigned k = 0; k < DSDR_CHANS_HW; k++) {
            if ((d->tx_chans.ch_map[k] & ~CH_SWAP_IQ_FLAG) == chno) {
                uint8_t nchan = (ch_txiq) ? d->tx_chans.ch_map[k] | CH_SWAP_IQ_FLAG : d->tx_chans.ch_map[k] & ~CH_SWAP_IQ_FLAG;
                if (nchan != d->tx_chans.ch_map[k]) {
                    d->tx_chans.ch_map[k] = nchan;
                    mod = true;
                }
            }
        }
        if (mod) {
            res = res ? res : d->tx->ops->option_set(d->tx, "chmap", (uintptr_t)&d->tx_chans);
        }
    }

    USDR_LOG("HIPR", USDR_LOG_WARNING, "CH[%d] F=%.3f TX_NCO=%.3f\n", chno, freq / 1.0e6, ncoval / 1.0e6);
    return d->st.libcapi79xx_upd_nco(&d->st.capi, NCO_TX, chno, ncoval / 1000, 0, 0);
#endif
}

int dev_m2_dsdr_sdr_rx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    if (!d->st.libcapi79xx_upd_nco)
        return 0;

    if (obj->full_path[0])
        return dsdr_iterate_ordinal_chans(ud, obj, value, "/dm/sdr/0/rx/freqency", true);

    return dsdr_set_rx_frequency_chan(d, value, obj->full_path[1]);
}

int dev_m2_dsdr_sdr_rx_dsa_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    if (!d->st.libcapi79xx_set_dsa)
        return 0;

    if (obj->full_path[0])
        return dsdr_iterate_ordinal_chans(ud, obj, value, "/dm/sdr/0/rx/dsa", true);

    unsigned i = obj->full_path[1];
    const channel_logic_dsp_wire_t* w = get_chmapnfo_from_ordinal(d, true, i);
    if (!w) {
        return -EINVAL;
    }

    return d->st.libcapi79xx_set_dsa(&d->st.capi, w->dsp_type, d->hw_rxch_route[w->hwport], value);
}

int dev_m2_dsdr_sdr_tx_dsa_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    if (!d->st.libcapi79xx_set_dsa)
        return 0;

    if (obj->full_path[0])
        return dsdr_iterate_ordinal_chans(ud, obj, value, "/dm/sdr/0/tx/dsa", false);

    unsigned i = obj->full_path[1];
    const channel_logic_dsp_wire_t* w = get_chmapnfo_from_ordinal(d, false, i);
    if (!w) {
        return -EINVAL;
    }

    return d->st.libcapi79xx_set_dsa(&d->st.capi, w->dsp_type, d->hw_txch_route[w->hwport], value);
}


int dev_m2_dsdr_sdr_tx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    if (!d->st.libcapi79xx_upd_nco)
        return 0;

    if (obj->full_path[0])
        return dsdr_iterate_ordinal_chans(ud, obj, value, "/dm/sdr/0/tx/freqency", false);

    return dsdr_set_tx_frequency_chan(d, value, obj->full_path[1]);
}

int dev_m2_dsdr_gain_rx_set(pdevice_t ud, pusdr_vfs_obj_t UNUSED obj, uint64_t value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    if (!d->st.libcapi79xx_set_dsa)
        return 0;

    if (obj->full_path[0])
        return dsdr_iterate_ordinal_chans(ud, obj, value, "/dm/sdr/0/rx/gain", true);

    int res = 0;
    unsigned i = obj->full_path[1];
    unsigned dsa_attn = (value > 25) ? 0 : 50 - 2 * value;
    unsigned rem_gain = (value > 25) ? value - 25 : 0;
    const channel_logic_dsp_wire_t* w = get_chmapnfo_from_ordinal(d, true, i);
    if (!w) {
        return -EINVAL;
    }
    unsigned hwidx = d->hw_rxch_route[w->hwport];

    if (dev_m2_dsdr_has_hiper(d) && (hwidx < MAX_HIPER_FE_PORT)) {
        res = res ? res : dsdr_hiper_fe_rx_gain_set(&d->hiper, s_chanmap_hw_to_fe[hwidx], rem_gain, NULL);
    }

    res = res ? res : d->st.libcapi79xx_set_dsa(&d->st.capi, w->dsp_type, hwidx, dsa_attn);
    return res;
}

int dev_m2_dsdr_gain_tx_set(pdevice_t ud, pusdr_vfs_obj_t UNUSED obj, uint64_t value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    if (!d->st.libcapi79xx_set_dsa)
        return 0;

    if (obj->full_path[0])
        return dsdr_iterate_ordinal_chans(ud, obj, value, "/dm/sdr/0/tx/gain", false);

    unsigned i = obj->full_path[1];
    unsigned dsa_attn = (value > 29) ? 0 : 29 - value;
    const channel_logic_dsp_wire_t* w = get_chmapnfo_from_ordinal(d, false, i);
    if (!w) {
        return -EINVAL;
    }
    unsigned hwidx = d->hw_txch_route[w->hwport];
    return d->st.libcapi79xx_set_dsa(&d->st.capi, w->dsp_type, hwidx, dsa_attn);
    //res = res ? res : d->st.libcapi79xx_set_dsa(&d->st.capi, NCO_TX, i, dsa_attn);
    //return res;
}

int dev_m2_dsdr_gain_rx_auto_set(pdevice_t ud, pusdr_vfs_obj_t UNUSED obj, uint64_t value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    if (!d->st.libcapi79xx_set_dsa)
        return 0;

    if (obj->full_path[0])
        return dsdr_iterate_ordinal_chans(ud, obj, value, "/dm/sdr/0/rx/gain/auto", true);

    int res = 0;
    unsigned i = obj->full_path[1];
    unsigned dsa_attn = (value > 25) ? 0 : 50 - 2 * value;
    unsigned rem_gain = (value > 25) ? value - 25 : 0;
    const channel_logic_dsp_wire_t* w = get_chmapnfo_from_ordinal(d, true, i);
    if (!w) {
        return -EINVAL;
    }
    unsigned hwidx = d->hw_rxch_route[w->hwport];

    if (dev_m2_dsdr_has_hiper(d) && (hwidx < MAX_HIPER_FE_PORT)) {
        res = res ? res : dsdr_hiper_fe_rx_gain_set(&d->hiper, s_chanmap_hw_to_fe[hwidx], rem_gain, NULL);
    }
    res = res ? res : d->st.libcapi79xx_set_dsa(&d->st.capi, w->dsp_type, hwidx, dsa_attn);
    //res = res ? res : d->st.libcapi79xx_set_dsa(&d->st.capi, NCO_RX, i, dsa_attn);
    return res;
}

int dev_m2_dsdr_gain_rx_lna_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    if (!dev_m2_dsdr_has_hiper(d))
        return -ENOTSUP;

    if (obj->full_path[0])
        return dsdr_iterate_ordinal_chans(ud, obj, value, "/dm/sdr/0/rx/gain/lna", true);

    unsigned i = obj->full_path[1];
    const channel_logic_dsp_wire_t* w = get_chmapnfo_from_ordinal(d, true, i);
    if (!w) {
        return -EINVAL;
    }
    unsigned hwidx = d->hw_rxch_route[w->hwport];

    if (dev_m2_dsdr_has_hiper(d) && (hwidx < MAX_HIPER_FE_PORT)) {
        return dsdr_hiper_fe_rx_gain_set(&d->hiper, s_chanmap_hw_to_fe[hwidx], value, NULL);
    }

    return -EINVAL;
    //int res = dsdr_hiper_fe_rx_gain_set(&d->hiper, s_chanmap_hw_to_fe[i], value, NULL);
    //return res;
}

int dev_m2_dsdr_gain_rx_pga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    if (!d->st.libcapi79xx_set_dsa)
        return 0;

    if (obj->full_path[0])
        return dsdr_iterate_ordinal_chans(ud, obj, value, "/dm/sdr/0/rx/gain/pga", true);

    unsigned i = obj->full_path[1];
    unsigned dsa_attn = (value > 25) ? 0 : 50 - 2 * value;
    const channel_logic_dsp_wire_t* w = get_chmapnfo_from_ordinal(d, true, i);
    if (!w) {
        return -EINVAL;
    }
    unsigned hwidx = d->hw_rxch_route[w->hwport];
    return d->st.libcapi79xx_set_dsa(&d->st.capi, w->dsp_type, hwidx, dsa_attn);
    //int res = d->st.libcapi79xx_set_dsa(&d->st.capi, NCO_RX, i, 50 - 2 * value);
    //return res;
}


static int dsdr_set_rates(dev_m2_dsdr_t* d, uint32_t rx_rate, uint32_t tx_rate)
{
    int res = 0;
    unsigned tx_inters[] = { 1, 2, 0, 4, 0, 8, 16, 32, 0, 0, 0 };
    unsigned rx_decims[] = { 1, 2, 3, 4, 6, 8, 16, 32, 64, 128, 256 };
    unsigned i = 0;
    unsigned ii;
    unsigned j = 0;
    unsigned jj;

    for (ii = 0; ii < SIZEOF_ARRAY(rx_decims); ii++) {
        if (rx_rate * rx_decims[ii] < d->max_rate)
            i = ii;
        else
            break;
    }
    for (jj = 0; jj < SIZEOF_ARRAY(rx_decims); jj++) {
        if (tx_rate * rx_decims[jj] < d->max_rate)
            j = jj;
        else
            break;
    }

    if (d->tx_activated && tx_inters[i] == 0) {
        i = j;
    }

    d->rxbb_rate = d->adc_rate / rx_decims[i];
    d->rxbb_decim = rx_decims[i];

    d->txbb_rate = tx_inters[i] == 0 ? 0 : d->dac_rate / tx_inters[i];
    d->txbb_inter = tx_inters[i];


    USDR_LOG("DSDR", USDR_LOG_ERROR, "Set rate: RX %.3f Mhz => %.3f (Decim: %d) -- TX %.3f Mhz => %.3f (Inter: %d)\n",
             rx_rate / 1.0e6, d->rxbb_rate / 1.0e6, d->rxbb_decim,
             tx_rate / 1.0e6, d->txbb_rate / 1.0e6, d->txbb_inter);

    // Reset FIFO after rate change
    if (rx_rate) {
        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_DSPCHAIN_RST, 0x2);
        res = (res) ? res : fgearbox_load_fir(d->base.dev, IGPO_DSPCHAIN_PRG, (fgearbox_firs_t)d->rxbb_decim, DSP_USSERIES);
        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_DSPCHAIN_RST, 0x0);

        if (res) {
            USDR_LOG("LSDR", USDR_LOG_ERROR, "Unable to initialize decimation RX FIR gearbox, error = %d!\n", res);
            return res;
        }
    }

    if (tx_rate && (d->txbb_inter > 0)) {
        d->txbb_rate = d->dac_rate / d->txbb_inter;

        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_DSPCHAIN_TX_RST, 0x2);
        res = (res) ? res : fgearbox_load_fir_i(d->base.dev, IGPO_DSPCHAIN_TX_PRG, (fgearbox_firs_t)d->txbb_inter, DSP_USSERIES);
        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_DSPCHAIN_TX_RST, 0x0);

        if (res) {
            USDR_LOG("LSDR", USDR_LOG_ERROR, "Unable to initialize interpolation TX FIR gearbox, error = %d!\n", res);
            return res;
        }
    }

    return res;
}


int dev_m2_dsdr_rate_m_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    uint32_t *rates = (uint32_t *)(uintptr_t)value;

    uint32_t rx_rate = rates[0];
    uint32_t tx_rate = rates[1];

    //uint32_t adc_rate = rates[2];
    //uint32_t dac_rate = rates[3];

    if (rx_rate == 0 && tx_rate == 0)
        return -EINVAL;

    return dsdr_set_rates(d, rx_rate, tx_rate);
}

int dev_m2_dsdr_rate_m_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    uint32_t *rates = (uint32_t *)(uintptr_t)*ovalue;

    rates[0] = d->rxbb_rate;
    rates[1] = d->txbb_rate;
    rates[2] = d->adc_rate;
    rates[3] = d->dac_rate;

    return 0;
}

int dev_m2_dsdr_rate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    return dsdr_set_rates(d, value, value);
}

static int dsdr_check_fpga_gtrx(dev_m2_dsdr_t* o)
{
    uint32_t fpga_jesd = ~0, fpga_err_0 = ~0, fpga_err_1 = ~0;
    unsigned delay;
    int res = 0;

    for (unsigned q = 0; q < (o->jesd_x8 ? 2 : 1); q++) {
        if (o->jesd_x8) {
            res = res ? res : dev_gpo_set(o->base.dev, IGPO_TIAFE_MASTER_RESET_N, q == 0 ? 0x01 : 0x81);
        }

        res = res ? res : dev_gpi_get32(o->base.dev, IGPI_JESD_SYSREF_RAC, &fpga_jesd);
        res = res ? res : dev_gpi_get32(o->base.dev, IGPI_JESD_FPGA_ERR_0, &fpga_err_0);
        res = res ? res : dev_gpi_get32(o->base.dev, IGPI_JESD_FPGA_ERR_1, &fpga_err_1);

        delay = (fpga_jesd >> 16) & 0x3ff;
        USDR_LOG("DSDR", (fpga_err_0 != 0 || fpga_err_1 != 0) ? USDR_LOG_ERROR : USDR_LOG_INFO,
                 "FPGA JESD: SYSREF realign TX/RX = %08x Delay = %d PLL Locked %d BUFFER_OVERFLOW: %04x ERRS %04x %04x %04x %04x \n",
                 fpga_jesd & 0xff, delay, (fpga_jesd >> 26) & 3, fpga_jesd >> 28,
                 fpga_err_0 >> 16, fpga_err_0 & 0xffff, fpga_err_1 >> 16, fpga_err_1 & 0xffff);

        USDR_LOG("DSDR", USDR_LOG_INFO, "FPGA JESD lanes:                     3   2   1   0\n");
        USDR_LOG("DSDR", USDR_LOG_INFO, "Block Header errors:                %2d  %2d  %2d  %2d\n", (fpga_err_0 >> 12) & 0xf, (fpga_err_0 >> 8) & 0xf, (fpga_err_0 >> 4) & 0xf, (fpga_err_0 >> 0) & 0xf);
        USDR_LOG("DSDR", USDR_LOG_INFO, "End of Multi-Block errors:          %2d  %2d  %2d  %2d\n", (fpga_err_0 >> 28) & 0xf, (fpga_err_0 >> 24) & 0xf, (fpga_err_0 >> 20) & 0xf, (fpga_err_0 >> 16) & 0xf);
        USDR_LOG("DSDR", USDR_LOG_INFO, "End of Extended Multi-Block errors: %2d  %2d  %2d  %2d\n", (fpga_err_1 >> 12) & 0xf, (fpga_err_1 >> 8) & 0xf, (fpga_err_1 >> 4) & 0xf, (fpga_err_1 >> 0) & 0xf);
        USDR_LOG("DSDR", USDR_LOG_INFO, "CRC mismatch errors:                %2d  %2d  %2d  %2d\n", (fpga_err_1 >> 28) & 0xf, (fpga_err_1 >> 24) & 0xf, (fpga_err_1 >> 20) & 0xf, (fpga_err_1 >> 16) & 0xf);
    }
    return res;
}

int dev_m2_dsdr_afe_health_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_dsdr *o = (struct dev_m2_dsdr *)ud;
    if (o->st.libcapi79xx_check_health == 0) {
        USDR_LOG("DSDR", USDR_LOG_ERROR, "AFE CAPI isn't available!\n");
        return dsdr_check_fpga_gtrx(o);
    }

    int rok = 0;
    char staus_buffer[16384];
    staus_buffer[0] = 0;

    int res = o->st.libcapi79xx_check_health(&o->st.capi, &rok, SIZEOF_ARRAY(staus_buffer), staus_buffer);
    if (res)
        return res;

    res = dsdr_check_fpga_gtrx(o);
    if (ovalue) {
        *ovalue = rok;
        if (staus_buffer[0]) {
            USDR_LOG("DSDR", USDR_LOG_WARNING, "AFE health report:\n%s\n", staus_buffer);
        }
    }
    return res;
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

    // Stub
    if (o->type == DSDR_KCU116_EVM) {
        return 0;
    }

    if (!(value & 0x800000)) {
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
    uint32_t clk = 0;

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
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    int res = 0;

    uint64_t fe_temp = 0;
    unsigned board_temp = 0;

    if (dev_m2_dsdr_has_hiper(d)) {
        res = res ? res : dsdr_hiper_fe_get_temp_max(&d->hiper, &fe_temp);
    }

    if (d->type == DSDR_M2_R1) {
        res = res ? res : tmp114_temp_get(d->base.dev, d->subdev, I2C_TEMP_AFE, (int*)&board_temp);
    } else if (!dev_m2_dsdr_has_hiper(d)) {
        // No temp sensors
        return -EINVAL;
    } else {
        *ovalue = fe_temp;
        return res;
    }

    *ovalue = fe_temp > board_temp ? fe_temp : board_temp;
    return res;
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
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)udev;
    lldev_t dev = d->base.dev;

    dev_m2_dsdr_afe_health_get(udev, NULL, NULL);

    // FE Power OFF
    if (dev_m2_dsdr_has_hiper(d)) {
        dsdr_hiper_fe_destroy(&d->hiper);
    }

    dev_gpo_set(dev, IGPO_AFE_RST, 0x1);
    usleep(100);

    // Safe Power OFF sequence
    dev_gpo_set(dev, IGPO_PWR_AFE, 0xf);
    usleep(100);
    dev_gpo_set(dev, IGPO_PWR_AFE, 0x7);
    usleep(100);
    dev_gpo_set(dev, IGPO_PWR_AFE, 0x3);
    usleep(100);
    dev_gpo_set(dev, IGPO_PWR_AFE, 0x1);
    usleep(100);
    dev_gpo_set(dev, IGPO_PWR_AFE, 0x0);
    usleep(100);
    dev_gpo_set(dev, IGPO_PWR_LMK, 0x0);

    // Activity LED off
    dev_gpo_set(dev, IGPO_BANK_LEDS, 0);

    usdr_device_base_destroy(udev);
}

static int usdr_jesd204b_bringup_pre(struct dev_m2_dsdr *dd)
{
    lldev_t dev = dd->base.dev;
    int res = 0;
    uint32_t d = 0;
    bool pll_ready = false;

    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_RX_SYNC_RESET, 1);
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_TX_SYNC_RESET, 1);
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_MASTER_RESET_N, 0);

    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_RX_BUFFER_RELDLY_0, 0); // 0 means autodetect and adjust

    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_RX_LANE_MAP_0, dd->cfg_rx_lanemap & 0xff);
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_RX_LANE_MAP_1, (dd->cfg_rx_lanemap >> 8) & 0xff);
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_RX_LANE_MAP_2, (dd->cfg_rx_lanemap >> 16) & 0xff);
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_RX_LANE_MAP_3, (dd->cfg_rx_lanemap >> 24) & 0xff);

    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_TX_LANE_MAP_0, dd->cfg_tx_lanemap & 0xff);
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_TX_LANE_MAP_1, (dd->cfg_tx_lanemap >> 8) & 0xff);
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_TX_LANE_MAP_2, (dd->cfg_tx_lanemap >> 16) & 0xff);
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_TX_LANE_MAP_3, (dd->cfg_tx_lanemap >> 24) & 0xff);

    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_RX_LANE_POLARITY, 0x0);
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_TX_LANE_POLARITY, 0x0);

    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_RX_LANE_ENABLED, dd->hw_fpga_jesd_rx_en);
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_TX_LANE_ENABLED, dd->hw_fpga_jesd_tx_en);

    res = res ? res : usleep(1000);

    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_MASTER_RESET_N, 1);

    for (unsigned k = 0; k < 100; k++) {
        res = res ? res : usleep(10000);
        res = res ? res : dev_gpi_get32(dev, IGPI_JESD_SYSREF_RAC, &d);
        USDR_LOG("DSDR", USDR_LOG_ERROR, "STAT = %08x\n", d);
        if (d & 0x08000000) {
            pll_ready = true;
            break;
        }
    }

    if (!pll_ready) {
        USDR_LOG("DSDR", USDR_LOG_ERROR, "FPGA GTH/GTY PLLs are not locked! giving up!\n");
        return -EIO;
    }

    res = res ? res : usleep(100000); //TODO check

    // TODO wait for PLL to lock..
    res = res ? res : dev_gpo_set(dev, IGPO_TIAFE_TX_SYNC_RESET, 0);

    res = res ? res : usleep(10000);

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
    res = res ? res : usleep(10000);
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
    unsigned afeType = 0;

    d->subdev = 0;
    d->dsdr_state = STATE_IDLE;

    d->hw_mask_rx = 0xf; // RX_3 RX_2 RX_1 RX_0
    d->hw_mask_tx = 0xf; // TX_3 TX_2 TX_1 TX_0
    d->hw_fpga_jesd_rx_en = 0xf;
    d->hw_fpga_jesd_tx_en = 0xf;

    res = res ? res : dev_gpi_get32(dev, IGPI_USR_ACCESS2, &usr2);
    res = res ? res : dev_gpi_get32(dev, IGPI_HWID, &hwid);
    if (res) {
        return res;
    }

    res = res ? res : dev_gpo_set(dev, IGPO_AFE_RST, 0x0);
    res = res ? res : dev_gpo_set(dev, IGPO_PWR_AFE, 0x0);
    res = res ? res : dev_gpo_set(dev, IGPO_PWR_LMK, 0x0);
    res = res ? res : dev_gpo_set(dev, IGPO_RX_CHEN, 0x0);
    res = res ? res : dev_gpo_set(dev, IGPO_TX_CHEN, 0x0);
    res = res ? res : dev_gpo_set(dev, IGPO_TX_AFETDD, 0x0);
    res = res ? res : dev_gpo_set(dev, IGPO_RX_AFETDD, 0x0);


    // TODO check for AFE7903
    if (getenv("DSDR_AFE7903")) {
        d->hw_mask_rx = 0x5; // RX_3 RX_1
        d->hw_mask_tx = 0xA; // TX_4 TX_2
    }

    devid = (hwid >> 16) & 0xff;
    jesdv = (hwid >> 8) & 0xff;
    bool dpump_afe_clk = false;
    bool jesd_x8 = false;
    if (jesdv & 0x08) {
        dpump_afe_clk = true;
        jesdv ^= 0x08;
    }

    unsigned master_rate = 491520000;
    unsigned maxusr_rate = 520000000;
    unsigned jesd_cfg = (jesdv >> 4);
    jesdv &= 0x0f;

    switch (jesd_cfg) {
    case JESD_MODE_4X_4X_491:
        d->rx_chmap_info = s_dsdr_chmap_s_nco;
        d->tx_chmap_info = s_dsdr_chmap_s_nco;
        d->rx_lmap_info = s_dsdr_lmap_s_nco;
        d->tx_lmap_info = s_dsdr_lmap_s_nco;

        d->logic_chcnt_rx = 4;
        d->logic_chcnt_tx = 4;
        d->hw_chcnt_rx = 4;
        d->hw_chcnt_tx = 4;
        break;

    case JESD_MODE_8X_8X_369:
        d->rx_chmap_info = s_dsdr_chmap_d_nco;
        d->tx_chmap_info = s_dsdr_chmap_d_nco;
        d->rx_lmap_info = s_dsdr_lmap_d_nco;
        d->tx_lmap_info = s_dsdr_lmap_d_nco;

        d->logic_chcnt_rx = 8;
        d->logic_chcnt_tx = 4; //TODO: recompile to 8!!!
        d->hw_chcnt_rx = 4;
        d->hw_chcnt_tx = 4;

        master_rate = 368640000;
        maxusr_rate = 380000000;

        d->hw_fpga_jesd_rx_en = 0xff;
        d->hw_fpga_jesd_tx_en = 0xff;
        jesd_x8 = true;
        break;

    case JESD_MODE_8X_4X_491:
        d->rx_chmap_info = s_dsdr_chmap_s_nco_fb;
        d->tx_chmap_info = s_dsdr_chmap_s_nco;
        d->rx_lmap_info = s_dsdr_lmap_s_nco_fb;
        d->tx_lmap_info = s_dsdr_lmap_s_nco;

        d->logic_chcnt_rx = 6; // a FB channels can occupy x2 lanes, but we support only x1 at the moment
        d->logic_chcnt_tx = 4;
        d->hw_chcnt_rx = 6;
        d->hw_chcnt_tx = 4;

        d->hw_fpga_jesd_rx_en = 0x3f;
        jesd_x8 = true;
        break;

    default:
        USDR_LOG("XDEV", USDR_LOG_ERROR, "Unsupported JESD_CFG=%d!\n", jesd_cfg);
        return -EIO;
    }

    d->jesd_x8 = jesd_x8;

    switch (devid) {
    case DSDR_KCU116_EVM:
    case DSDR_M2_R0:
    case DSDR_M2_R1:
    case DSDR_PCIE_HIPER_R0:
        d->type = devid;
        break;

    default:
        USDR_LOG("XDEV", USDR_LOG_ERROR, "Unsupported HWID = %08x, skipping initialization!\n", hwid);
        return -EIO;
    }

    //
    if (getenv("DSDR_M2_R0")) {
        d->type = DSDR_M2_R0;
    }

    d->cfg_rx_lanemap = 0x76543210;
    d->cfg_tx_lanemap = 0x76543210;

    for (unsigned h = 0; h < MAX_LOGIC_CHANS; h++) {
        d->hw_rxch_route[h] = h;
        d->hw_txch_route[h] = h;
    }

    afeType = 7901;
    switch (jesdv) {
    case DSDR_JESD204B_810_245:
        d->max_rate = maxusr_rate / 2;
        d->dac_rate = d->adc_rate = master_rate / 2;
        d->afecongiguration = "Afe79xxPg1_02.txt";
        break;

    case DSDR_JESD204C_6664_245:
        d->max_rate = maxusr_rate / 2;
        d->dac_rate = d->adc_rate = master_rate / 2;
        d->afecongiguration =  "Afe79xxPg1_6664_245.txt";
        break;

    case DSDR_JESD204C_6664_491:
        d->max_rate = maxusr_rate;
        d->dac_rate = d->adc_rate = master_rate;

        if (jesd_x8 == false && master_rate == 491520000) {
            d->afecongiguration = "Afe79xxPg1_6664_491.txt";
        } else if (jesd_x8 == true && master_rate == 368640000) {
            d->afecongiguration = "Afe79xxPg1_6664_369_D.txt";
        } else if (jesd_x8 == true && master_rate == 491520000) {
            d->afecongiguration = "Afe79xxPg1_6664_491_fb3.txt";
        }

        if (d->hw_mask_rx == 0x5 && d->hw_mask_tx == 0xA) {
            // RX C/A
            // TX D/B
            // d->afecongiguration = "Afe79xxPg1_dsdr_491_7903.txt";

            afeType = 7903;

            d->hw_chcnt_rx = 2;
            d->hw_chcnt_tx = 2;

            d->cfg_rx_lanemap = 0x76543120;
            d->cfg_tx_lanemap = 0x76542031;

            d->hw_rxch_route[0] = 0;
            d->hw_rxch_route[1] = 2;
            d->hw_rxch_route[2] = 1;
            d->hw_rxch_route[3] = 3;
            d->hw_rxch_route[4] = 0;
            d->hw_rxch_route[5] = 2;
            d->hw_rxch_route[6] = 1;
            d->hw_rxch_route[7] = 3;

            d->hw_txch_route[0] = 1;
            d->hw_txch_route[1] = 3;
            d->hw_txch_route[2] = 0;
            d->hw_txch_route[3] = 2;
            d->hw_txch_route[4] = 1;
            d->hw_txch_route[5] = 3;
            d->hw_txch_route[6] = 0;
            d->hw_txch_route[7] = 2;
        }

        if (getenv("DSDR_M2_7950")) {
            if (master_rate != 491520000) {
                USDR_LOG("XDEV", USDR_LOG_ERROR, "No configuration for 7950@369MSPS!\n");
                return -EIO;
            }
            // AFE7950
            if (jesd_x8 == true) {
                d->afecongiguration = "Afe79xxPg1_6664_491_7950_fb3.txt";
            } else {
                d->afecongiguration = "Afe79xxPg1_6664_491_7950.txt";
            }
            afeType = 7950;
        }
        break;

    default:
        USDR_LOG("XDEV", USDR_LOG_ERROR, "Unsupported JESD type %x (HWID = %08x), skipping initialization!\n", jesdv, hwid);
        return -EIO;
    }

    d->cfg_afe_type = afeType;
    d->jesdv = jesdv;
    USDR_LOG("XDEV", USDR_LOG_ERROR, "Configuration: %s, Type: %d, AFE: %d, JESD204%c, CH_TX=%02x, CH_RX=%02x JESDx%d",
             d->afecongiguration, d->type, d->cfg_afe_type, (jesdv == DSDR_JESD204B_810_245) ? 'B' : 'C', d->hw_mask_tx, d->hw_mask_rx,
             jesd_x8 ? 8 : 4);

    if (getenv("SKIPAFE")) {
        d->type = DSDR_KCU116_EVM;
    }
    if (d->type == DSDR_KCU116_EVM) {
        USDR_LOG("XDEV", USDR_LOG_ERROR, "Skipping AFE initialization! SR=%.2f\n", d->adc_rate / 1e6);
        res = res ? res : afe79xx_create_dummy(&d->st);

        for (int i = 0; i < 6; i++) {
            uint32_t clk = 0;
            res = res ? res : dev_gpi_get32(d->base.dev, 20, &clk);

            USDR_LOG("DSDR", USDR_LOG_ERROR, "Clk %d: %d\n", clk >> 28, clk & 0xfffffff);
            usleep(0.5 * 1e6);
        }

        res = res ? res : usdr_jesd204b_bringup_pre(d);

        USDR_LOG("XDEV", USDR_LOG_ERROR, "Waiting for AFE... (press enter when external confuguration is done)\n");
        getchar();
        USDR_LOG("XDEV", USDR_LOG_ERROR, "Resetting JESD\n");

        res = res ? res : usdr_jesd204b_bringup_post(d);
        res = res ? res : dsdr_check_fpga_gtrx(d);
        return res;
    }

    res = res ? res : dev_gpo_set(dev, IGPO_BANK_LEDS, 1);

    if (getenv("USDR_BARE_DEV")) {
        USDR_LOG("XDEV", USDR_LOG_WARNING, "USDR_BARE_DEV is set, skipping initialization!\n");
        return res;
    }

    // Put LMK into PD but enable all LDOs to settle
    res = res ? res : dev_gpo_set(dev, IGPO_PWR_LMK, 0xbf);

    for (unsigned j = 0; j < 10; j++) {
        usleep(10000);
        res = res ? res : dev_gpi_get32(dev, IGPI_PGOOD, &pg);
        if (res || (pg & 1))
            break;
    }

    if (d->type == DSDR_M2_R0 || d->type == DSDR_M2_R1) {
        bool pg;
        for (unsigned j = 0; j < 20; j++) {
            usleep(10000);
            res = res ? res : tps6381x_init(dev, d->subdev, I2C_TPS63811, true, true, 3450);
            if (res == 0)
                break;
        }
        if (res) {
            USDR_LOG("XDEV", USDR_LOG_ERROR, "Unable to intialize tps6381x booster!\n");
            return res;
        }

        for (unsigned j = 0; j < 20; j++) {
            res = res ? res : tps6381x_check_pg(dev, d->subdev, I2C_TPS63811, &pg);
            if (!res || pg) {
                break;
            }
            usleep(20000);
        }
        if (!pg) {
            USDR_LOG("XDEV", USDR_LOG_ERROR, "No PG signal in tps6381x booster!\n");
            return -EIO;
        }
    }

    usleep(20000);
    res = res ? res : dev_gpo_set(dev, IGPO_PWR_LMK, 0xff);
    usleep(200000);

    //
    //LMK05318 init start

    //set true to enable IN_REF1 40M
    bool enable_in_ref = false;

    lmk05318_dpll_settings_t dpll;
    memset(&dpll, 0, sizeof(dpll));
    dpll.enabled = enable_in_ref;
    dpll.en[LMK05318_PRIREF] = true;
    dpll.fref[LMK05318_PRIREF] = 40000000;
    dpll.type[LMK05318_PRIREF] = DPLL_REF_TYPE_DIFF_NOTERM;
    dpll.dc_mode[LMK05318_PRIREF] = DPLL_REF_DC_COUPLED_INT;
    dpll.buf_mode[LMK05318_PRIREF] = DPLL_REF_AC_BUF_HYST50_DC_EN;

    lmk05318_out_config_t lmk_out[8];

    lmk05318_port_request(&lmk_out[0], 0,       master_rate, false, OUT_OFF);
    lmk05318_port_request(&lmk_out[1], 1,       master_rate, false, LVDS);
    lmk05318_port_request(&lmk_out[2], 2,           3840000, false, LVDS);
    lmk05318_port_request(&lmk_out[3], 3,           3840000, false, OUT_OFF);
    lmk05318_port_request(&lmk_out[4], 4,                 0, false, OUT_OFF);
    lmk05318_port_request(&lmk_out[5], 5,   d->dac_rate / 2, false, LVDS);
    lmk05318_port_request(&lmk_out[6], 6,           3840000, false, LVDS);
    lmk05318_port_request(&lmk_out[7], 7,   dpump_afe_clk ? d->dac_rate : d->dac_rate / 2, false, LVDS);

    res = lmk05318_create(dev, d->subdev, I2C_LMK, (d->type == DSDR_PCIE_HIPER_R0) ? 52000000 : 26000000, XO_CMOS,
                          false, &dpll, lmk_out, SIZEOF_ARRAY(lmk_out), &d->lmk, false /*dry_run*/);
    if(res)
        return res;

    // wait for PRIREF/SECREF validation
    res = lmk05318_wait_dpll_ref_stat(&d->lmk, 100000);
    if (res) {
        USDR_LOG("DSDR", USDR_LOG_ERROR, "LMK03518 DPLL input reference freqs are not validated during specified timeout");
        return res;
    }

    //res = res ? res : dev_gpo_set(dev, IGPO_PWR_LMK, 0x7f);

    //wait for lock
    res = lmk05318_wait_apll1_lock(&d->lmk, 200000);
    res = res ? res : lmk05318_wait_apll2_lock(&d->lmk, 200000);
    res = res ? res : lmk05318_check_lock(&d->lmk, &los, false /*silent*/); //just to log state

    if(res)
    {
        USDR_LOG("DSDR", USDR_LOG_ERROR, "LMK03518 PLLs not locked during specified timeout");
        return res;
    }

    //sync to make APLL1/APLL2 & out channels in-phase
    res = lmk05318_sync(&d->lmk);
    //usleep(1000);

    //res = res ? res : dev_gpo_set(dev, IGPO_PWR_LMK, 0x7f);
    //usleep(1000);
    //res = res ? res : dev_gpo_set(dev, IGPO_PWR_LMK, 0xff);
    //if(res)
    //    return res;

    USDR_LOG("DSDR", USDR_LOG_INFO, "LMK03518 outputs synced, LOS=%x", los);
    //LMK05318 init end
    //

    for (int i = 0; i < 5; i++) {
        uint32_t clk = 0;
        res = res ? res : dev_gpi_get32(d->base.dev, 20, &clk);

        USDR_LOG("DSDR", USDR_LOG_ERROR, "Clk %d: %d\n", clk >> 28, clk & 0xfffffff);
        res = res ? res : usleep(0.5 * 1e6);
    }

    res = res ? res : usleep(1000);
    res = res ? res : dev_gpi_get32(dev, IGPI_PGOOD, &pg);

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
    res = res ? res : usleep(10000);
    //res = res ? res : dev_gpo_set(dev, IGPO_PWR_AFE, 0x3); // Enable VIOSYS, hold RESET
    res = res ? res : lp875484_init(dev, d->subdev, I2C_AFE_PMIC);
    res = res ? res : lp875484_set_vout(dev, d->subdev, I2C_AFE_PMIC, 930); // Recomended 925mV
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

    res = res ? res : usleep(100000);

    res = res ? res : dev_gpo_set(dev, IGPO_PWR_AFE, 0x7); // Enable DCDC 1.2V;
    // We don't have PG_1v2 routed in this rev
    // We don't have EN_1v8 routed in this rev

    if (d->type == DSDR_PCIE_HIPER_R0 || d->type == DSDR_M2_R1) {
        usleep(25000);
        res = res ? res : dev_gpo_set(dev, IGPO_PWR_AFE, 0xf);
    }

    // Check PG_1v8
    for (unsigned j = 0; j < 100; j++) {
        res = dev_gpi_get32(dev, IGPI_PGOOD, &pgdat);
        if (pgdat & (1 << 6))
            break;

        res = res ? res : usleep(1000);
    }
    if (!(pgdat & (1 << 6))) {
        USDR_LOG("DSDR", USDR_LOG_ERROR, "DCDC 1.8V isn't good, giving up!\n");
        return -EIO;
    }

    res = res ? res : usleep(100000);

    res = res ? res : dev_gpo_set(dev, IGPO_PWR_AFE, 0x1f);
    if (res)
        return res;

    // Test AFE chip
    USDR_LOG("DSDR", USDR_LOG_ERROR, "AFE is powered up!\n");

    res = res ? res : usleep(300000);

    res = res ? res : dev_gpo_set(dev, IGPO_AFE_RST, 0x0);
    res = res ? res : usleep(100000);
    res = res ? res : dev_gpo_set(dev, IGPO_AFE_RST, 0x1);

    res = res ? res : usleep(100000);

    res = res ? res : dev_gpo_set(dev, IGPO_TX_AFETDD, 0x0f);
    res = res ? res : dev_gpo_set(dev, IGPO_RX_AFETDD, 0x0f);


    res = res ? res : afe79xx_create(dev, d->subdev, 0, afeType, &d->st);
    if (res == 0) {
        res = res ? res : usdr_jesd204b_bringup_pre(d);

        // TODO accurate check GTH/GTY TX is up and running
        res = res ? res : usleep(100000);

        char afeconfig_path[1024];
        char *afecfgpath = getenv("AFECFG_PATH");
        snprintf(afeconfig_path, sizeof(afeconfig_path) - 1, "%s/%s", (afecfgpath) ? afecfgpath : "", d->afecongiguration);

        USDR_LOG("DSDR", USDR_LOG_ERROR, "Initializing config `%s` for AFE...\n", afeconfig_path);

        res = res ? res : afe79xx_init(&d->st, afeconfig_path);
        res = res ? res : usdr_jesd204b_bringup_post(d);
    }

    if (d->type == DSDR_PCIE_HIPER_R0) {
        unsigned override = 0;
        unsigned* poverride = NULL;
        const char* env_over;
        if ((env_over = getenv("LMS8_MPW2024_MASK"))) {
            bool ok = (strlen(env_over) == 6);
            if (ok) {
                for (unsigned i = 0; i < 6; i++) {
                    if (env_over[i] == '0') {
                    } else if (env_over[i] == '1') {
                        override |= 1 << (5 - i);
                    } else {
                        ok = false;
                    }
                }
            }

            if (ok) {
                poverride = &override;
                USDR_LOG("DSDR", USDR_LOG_ERROR, "Applying external MPW mask for LMS8 chips: %d%d%d%d%d%d",
                         (override >> 5) & 1, (override >> 4) & 1, (override >> 3) & 1,
                         (override >> 2) & 1, (override >> 1) & 1, (override >> 0) & 1);
            } else {
                USDR_LOG("DSDR", USDR_LOG_ERROR, "Incorrect LMS8_MPW2024_MASK format!!! Should be like `LMS8_MPW_MASK=010101`\n");
            }
        }

        res = res ? res : dsdr_hiper_fe_create(dev, SPI_BUS_HIPER_FE, poverride, &d->hiper);
    }

    res = res ? res : usleep(100000);

    // check state
    res = res ? res : dev_m2_dsdr_afe_health_get(udev, NULL, NULL);
    if (res)
        return res;

    USDR_LOG("DSDR", USDR_LOG_WARNING, "Initializing AFE done!\n");
    d->dsdr_state = STATE_AFE_INIT;
    return 0;
}


int dev_m2_dsdr_sdr_rx_remap_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    unsigned bits = (d->logic_chcnt_rx == 1) ? 0 : (d->logic_chcnt_rx == 2) ? 1 : (d->logic_chcnt_rx <= 4) ? 2 : 3;
    unsigned mask = (1 << bits) - 1;
    for (unsigned i = 0; i < d->logic_chcnt_rx; i++) {
        d->rx_ordinal_to_logic[i] = (value >> (bits * i)) & mask;
    }

    return dsdr_update_rx_remap(d);
}

int dev_m2_dsdr_sdr_rx_remap_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    unsigned bits = (d->logic_chcnt_rx == 1) ? 0 : (d->logic_chcnt_rx == 2) ? 1 : (d->logic_chcnt_rx <= 4) ? 2 : 3;
    unsigned mask = (1 << bits) - 1;
    uint64_t remap = 0;
    for (unsigned i = 0; i < d->logic_chcnt_rx; i++) {
        remap |= (d->rx_ordinal_to_logic[i] & mask) << (bits * i);
    }

    *ovalue = remap;
    return 0;
}

int dev_m2_dsdr_sdr_tx_remap_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    unsigned bits = (d->logic_chcnt_tx == 1) ? 0 : (d->logic_chcnt_tx == 2) ? 1 : (d->logic_chcnt_tx <= 4) ? 2 : 3;
    unsigned mask = (1 << bits) - 1;
    for (unsigned i = 0; i < d->logic_chcnt_tx; i++) {
        d->tx_ordinal_to_logic[i] = (value >> (bits * i)) & mask;
    }
    return dsdr_update_tx_remap(d);
}

int dev_m2_dsdr_sdr_tx_remap_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    unsigned bits = (d->logic_chcnt_tx == 1) ? 0 : (d->logic_chcnt_tx == 2) ? 1 : (d->logic_chcnt_tx <= 4) ? 2 : 3;
    unsigned mask = (1 << bits) - 1;
    uint64_t remap = 0;
    for (unsigned i = 0; i < d->logic_chcnt_tx; i++) {
        remap |= (d->tx_ordinal_to_logic[i] & mask) << (bits * i);
    }

    *ovalue = remap;
    return 0;
}

static const char* dsdr_chan_name(struct dev_m2_dsdr *d, bool rx, uint8_t logic_number)
{
    const channel_map_info_t *nfo = rx ? d->rx_chmap_info : d->tx_chmap_info;
    for (unsigned p = 0; p < 16; p++) {
        if (nfo[p].name == NULL)
            break;

        if (nfo[p].hwidx == logic_number)
            return nfo[p].name;
    }

    return "-";
}

int device_path_to_chmsk(const char* full_path, const char* basename, const channel_map_info_t* map, const unsigned max_lchan, chmsk_t *hw_mask, chmsk_t *lg_mask)
{
    *hw_mask = 0;
    *lg_mask = 0;

    size_t len = strlen(basename);
    if (strncmp(full_path, basename, len)) {
        return -ENOENT;
    }
    const char* lst = full_path + len;
    if (*lst != '/') {
        chmsk_set_all(lg_mask);
        return -ENAVAIL;
    }

    lst++;

    char chanlist[64*4];
    SAFE_STRCPY(chanlist, lst);

    char* phys_names[DSDR_CHANS_LOGIC];
    unsigned phys_nums[DSDR_CHANS_LOGIC];
    usdr_channel_info_t nfo;

    nfo.phys_names = (const char**)phys_names;
    nfo.phys_nums = phys_nums;

    int res = usdr_channel_info_string_parse(chanlist, DSDR_CHANS_LOGIC, &nfo);
    if (res)
        return res;

    if (nfo.phys_names == NULL && nfo.phys_nums == NULL) {
        // No channel information were supplied
        chmsk_set_all(lg_mask);
        return 0;
    }

    channel_info_t mmaped;
    res = usdr_channel_info_map_default(&nfo, map, max_lchan, &mmaped);
    if (res)
        return res;

    chmsk_t* pmsk = (nfo.phys_names) ? hw_mask : lg_mask;
    for (unsigned i = 0; i < nfo.count; i++) {
        *pmsk |= ((chmsk_t)1) << mmaped.ch_map[i];
    }

    return 0;
}

static int parse_overriden_cahnnel_info(const char* env_string, const usdr_channel_info_t* orig, const channel_map_info_t* map, const unsigned max_lchan, channel_info_t* override)
{
    char chanlist[64*4];
    char* phys_names[DSDR_CHANS_LOGIC];
    unsigned phys_nums[DSDR_CHANS_LOGIC];
    usdr_channel_info_t nfo;
    nfo.phys_names = (const char**)phys_names;
    nfo.phys_nums = phys_nums;

    SAFE_STRCPY(chanlist, env_string);

    int res = usdr_channel_info_string_parse(chanlist, DSDR_CHANS_LOGIC, &nfo);
    if (res)
        return res;

    if (nfo.count != orig->count) {
        USDR_LOG("UDEV", USDR_LOG_ERROR, "Overriden channel count %d != requested %d count!\n", nfo.count, orig->count);
        return -EINVAL;
    }

    res = usdr_channel_info_map_default(&nfo, map, max_lchan, override);
    return res;
}

static
int usdr_device_m2_dsdr_create_stream(device_t* dev, const char* sid, const char* dformat,
                                      const usdr_channel_info_t* channels, unsigned pktsyms,
                                      unsigned flags, const char* parameters, stream_handle_t** out_handle)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)dev;
    int res = 0;
    unsigned hwchs;
    channel_info_t lchans;

    if (d->dsdr_state != STATE_AFE_INIT) {
        return -EFAULT;
    }

    bool rx_str = strstr(sid, "rx") != NULL;
    bool tx_str = strstr(sid, "tx") != NULL;
    if (!rx_str && !tx_str) {
        USDR_LOG("UDEV", USDR_LOG_ERROR, "DSDR Unrecognised stream: %s\n", sid);
        return -EINVAL;
    }

    res = usdr_channel_info_map_default(channels,
                                        rx_str ? d->rx_chmap_info : d->tx_chmap_info,
                                        rx_str ? d->logic_chcnt_rx : d->logic_chcnt_tx, &lchans);
    if (res) {
        return res;
    }
    if (channels->count > 8 || channels->count == 5 || channels->count == 7) {
        USDR_LOG("UDEV", USDR_LOG_ERROR, "DSDR %s: Unsupported channel count: %d, valid are (1, 2, 3, 4, 6, 8)\n", sid, channels->count);
        return -EINVAL;
    }

    if (rx_str) {
        if (d->rx) {
            return -EBUSY;
        }

        memset(d->rx_ordinal_to_logic, 0xff, sizeof(d->rx_ordinal_to_logic));

        const char* env_ch = getenv("DSDR_CH_RX");
        if (env_ch) {
            res = parse_overriden_cahnnel_info(env_ch, channels, d->rx_chmap_info, d->logic_chcnt_rx, &lchans);
            if (res)
                return res;
            USDR_LOG("UDEV", USDR_LOG_INFO, "DSDR RX channel mask is overriden to `%s`\n", env_ch);
        }

        memcpy(d->rx_ordinal_to_logic, lchans.ch_map, sizeof(lchans.ch_map[0]) * channels->count);

        d->hw_enabled_rx = 0;
        d->logic_enabled_rx = 0;
        for (unsigned i = 0; i < channels->count; i++) {
            unsigned logic = d->rx_ordinal_to_logic[i];
            unsigned hw = d->rx_lmap_info[logic].hwport;
            if (hw >= d->hw_chcnt_rx) {
                USDR_LOG("UDEV", USDR_LOG_ERROR, "Stream RX: Logical channel %d incorrectly mmaped to HW %d\n", i, hw);
                return -EINVAL;
            }

            d->hw_enabled_rx |= (1ull << hw);
            d->logic_enabled_rx |= (1ull << logic);
        }

        res = res ? res : dsdr_update_rx_remap(d);
        USDR_LOG("UDEV", USDR_LOG_INFO, "DSDR RX channels %d remmaped: [%s, %s, %s, %s -- %s, %s, %s, %s] mux, hw_mask %02x logic_mask %02x\n", channels->count,
                 dsdr_chan_name(d, true, 0), dsdr_chan_name(d, true, 1),
                 dsdr_chan_name(d, true, 2), dsdr_chan_name(d, true, 3),
                 dsdr_chan_name(d, true, 4), dsdr_chan_name(d, true, 5),
                 dsdr_chan_name(d, true, 6), dsdr_chan_name(d, true, 7),
                 d->hw_enabled_rx, d->logic_enabled_rx);

        uint64_t v0, v1;
        for (unsigned ch = 0; ch < 4; ch++) {
            d->st.libcapi79xx_get_nco(&d->st.capi, NCO_RX, ch, &v0, 0, 0);
            d->st.libcapi79xx_get_nco(&d->st.capi, NCO_RX, ch, &v1, 0, 1);
            USDR_LOG("UDEV", USDR_LOG_INFO, "RX NCO[%d] = %lld | NCO[%d] = %lld \n", ch, (long long)v0, ch, (long long)v1);
        }
        for (unsigned ch = 0; ch < 2; ch++) {
            d->st.libcapi79xx_get_nco(&d->st.capi, NCO_FB, ch, &v0, 0, 0);
            USDR_LOG("UDEV", USDR_LOG_INFO, "FB NCO[%d] = %lld \n", ch, (long long)v0);
        }

        struct sfetrx4_config rxcfg;
        res = res ? res : parse_sfetrx4(dformat, &lchans, pktsyms, channels->count, &rxcfg);
        if (res) {
            USDR_LOG("UDEV", USDR_LOG_ERROR, "Unable to parse RX stream configuration!\n");
            return res;
        }

        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_DSPCHAIN_RST, 0x7);
        usleep(1000);
        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_DSPCHAIN_RST, 0x0);

        res = (res) ? res : create_sfetrx4_stream(dev, (d->logic_chcnt_rx == 8 || d->logic_chcnt_rx == 6) ? CORE_EXFERX_DMA32_R0_8 : CORE_EXFERX_DMA32_R0,
                                                  dformat, channels->count, &lchans, pktsyms,
                                                  flags, M2PCI_REG_WR_RXDMA_CONFIRM, VIRT_CFG_SFX_BASE, 0,
                                                  SRF4_FIFOBSZ, CSR_RFE4_BASE, &d->rx, &hwchs);
        if (res) {
            return res;
        }

        d->rx_activated = true;
        d->rx_chans = lchans;

        // Restore cached parameters we couldn't set before activating streams
        for (unsigned i = 0; i < SIZEOF_ARRAY(d->rx_ord_freqs); i++) {
            if (d->rx_ord_freqs[i].set && (d->logic_enabled_rx & (1u << i))) {
                res = (res) ? res : dsdr_set_rx_frequency_chan(d, d->rx_ord_freqs[i].value, i);
            }
        }

        // TODO: set actual antenna mask
        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_RX_CHEN, 0xf);
        //res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_RX_CHEN, 0x0);

        *out_handle = d->rx;
    } else {
        if (d->tx) {
            return -EBUSY;
        }        
        memset(d->tx_ordinal_to_logic, 0xff, sizeof(d->tx_ordinal_to_logic));

        const char* env_ch = getenv("DSDR_CH_TX");
        if (env_ch) {
            res = parse_overriden_cahnnel_info(env_ch, channels, d->tx_chmap_info, d->logic_chcnt_tx, &lchans);
            if (res)
                return res;
            USDR_LOG("UDEV", USDR_LOG_INFO, "DSDR TX channel mask is overriden to `%s`\n", env_ch);
        }

        memcpy(d->tx_ordinal_to_logic, lchans.ch_map, sizeof(lchans.ch_map[0]) * channels->count);

        //memset(d->tx_hw_to_logic, 0xff, sizeof(d->tx_hw_to_logic));

        d->hw_enabled_tx = 0;
        d->logic_enabled_tx = 0;

        for (unsigned i = 0; i < channels->count; i++) {
            unsigned logic = d->tx_ordinal_to_logic[i];
            unsigned hw = d->tx_lmap_info[logic].hwport;
            if (hw >= d->hw_chcnt_tx) {
                USDR_LOG("UDEV", USDR_LOG_ERROR, "Stream TX: Logical channel %d incorrectly mmaped to HW %d\n", i, hw);
                return -EINVAL;
            }

            d->hw_enabled_tx |= (1ull << hw);
            d->logic_enabled_tx |= (1ull << logic);


            //d->tx_hw_to_logic[hw] = i;
        }

        // Map as single channel only
        res = res ? res : dsdr_update_tx_remap(d);
        USDR_LOG("UDEV", USDR_LOG_INFO, "DSDR TX channels %d remmaped: [%s, %s, %s, %s -- %s, %s, %s, %s] mux, hw_mask %02x logic_mask %02x\n", channels->count,
                 dsdr_chan_name(d, false, 0), dsdr_chan_name(d, false, 1),
                 dsdr_chan_name(d, false, 2), dsdr_chan_name(d, false, 3),
                 dsdr_chan_name(d, false, 4), dsdr_chan_name(d, false, 5),
                 dsdr_chan_name(d, false, 6), dsdr_chan_name(d, false, 7),
                 d->hw_enabled_tx, d->logic_enabled_tx);
        // USDR_LOG("UDEV", USDR_LOG_INFO, "DSDR TX channels %d remmaped: [A <= %c, B <= %c, C <= %c, D <= %c] mux, hw_mask %02x\n",
        //          channels->count, dsdr_chan_num(d->tx_hw_to_logic[0]), dsdr_chan_num(d->tx_hw_to_logic[1]),
        //          dsdr_chan_num(d->tx_hw_to_logic[2]), dsdr_chan_num(d->tx_hw_to_logic[3]), d->hw_enabled_tx);

        struct sfetrx4_config txcfg;
        res = (res) ? res : parse_sfetrx4(dformat, &lchans, pktsyms, channels->count, &txcfg);
        if (res) {
            USDR_LOG("UDEV", USDR_LOG_ERROR, "Unable to parse TX stream configuration!\n");
            return res;
        }

        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_DSPCHAIN_TX_RST, 0x7);
        usleep(1000);
        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_DSPCHAIN_TX_RST, 0x0);

        res = (res) ? res : create_sfetrx4_stream(dev, (d->logic_chcnt_tx == 8) ? CORE_EXFETX_DMA32_R0_8 : CORE_EXFETX_DMA32_R0,
                                                  dformat, channels->count, &lchans, pktsyms,
                                                  flags | DMS_DONT_CHECK_FWID,
                                                  M2PCI_REG_WR_TXDMA_CFG0,
                                                  M2PCI_REG_WR_SYNC_CTRL,
                                                  M2PCI_REG_RD_TXDMA_STAT,
                                                  0, CSR_TFE4_BASE, &d->tx, &hwchs);
        if (res) {
            return res;
        }

        d->tx_activated = true;
        d->tx_chans = lchans;

        // Restore cached parameters we couldn't set before activating streams
        for (unsigned i = 0; i < SIZEOF_ARRAY(d->tx_ord_freqs); i++) {
            if (d->tx_ord_freqs[i].set && (d->logic_enabled_rx & (1u << i))) {
                res = (res) ? res : dsdr_set_tx_frequency_chan(d, d->tx_ord_freqs[i].value, i);
            }
        }

        // TODO: set actual antenna mask
        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_TX_CHEN, 0xf);
        //res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_TX_CHEN, 0x0);
        *out_handle = d->tx;
    }

    return res;
}

static
int usdr_device_m2_dsdr_unregister_stream(device_t* dev, stream_handle_t* stream)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)dev;
    int res = 0;

    if (stream == d->rx) {
        d->rx = NULL;
        d->hw_enabled_tx = 0;
        d->rx_activated = false;

        if (dev_m2_dsdr_has_hiper(d)) {
            res = dsdr_hiper_fe_rx_chan_en(&d->hiper, 0);
        }

        dev_gpo_set(d->base.dev, IGPO_RX_CHEN, 0);
    } else if (stream == d->tx) {
        d->tx = NULL;
        d->hw_enabled_rx = 0;
        d->tx_activated = false;

        if (dev_m2_dsdr_has_hiper(d)) {
            res = dsdr_hiper_fe_tx_chan_en(&d->hiper, 0);
        }

        dev_gpo_set(d->base.dev, IGPO_TX_CHEN, 0);
    } else {
        return -EINVAL;
    }

    dev_m2_dsdr_afe_health_get(dev, NULL, NULL);
    return res;
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

    d->hw_enabled_tx = 0;
    d->hw_enabled_rx = 0;
    d->hw_mask_tx = 0;
    d->hw_mask_rx = 0;

    d->logic_enabled_rx = 0;
    d->logic_enabled_tx = 0;

    d->hw_fpga_jesd_rx_en = 0;
    d->hw_fpga_jesd_tx_en = 0;

    memset(d->rx_ordinal_to_logic, 0xff, sizeof(d->rx_ordinal_to_logic));
    memset(d->tx_ordinal_to_logic, 0xff, sizeof(d->tx_ordinal_to_logic));
    //memset(d->tx_hw_to_logic, 0xff, sizeof(d->tx_hw_to_logic));

    d->tx_activated = false;
    d->rx_activated = false;

    for (unsigned i = 0; i < SIZEOF_ARRAY(d->rx_ord_freqs); i++) {
        opt_u64_set_null(&d->rx_ord_freqs[i]);
        opt_u64_set_null(&d->tx_ord_freqs[i]);
    }

    for (unsigned i = 0; i < MAX_HIPER_FE_PORT; i++) {
        for (unsigned j = 0; j < MAX_PORT_BANDS; j++) {
            opt_u64_set_null(&d->rx_bxfc[i][j]);
            opt_u64_set_null(&d->tx_bxfc[i][j]);

            opt_u64_set_null(&d->rx_raw_nco[i][j]);
            opt_u64_set_null(&d->tx_raw_nco[i][j]);

        }
    }


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
