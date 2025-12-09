// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <usdr_logging.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#include "../device.h"
#include "../device_ids.h"
#include "../device_vfs.h"
#include "../device_names.h"
#include "../device_cores.h"
#include "../device_fe.h"

#include "../hw/lp8758/lp8758.h"

#include "../ipblks/streams/sfe_rx_4.h"
#include "../ipblks/streams/sfe_tx_4.h"
#include "../ipblks/streams/stream_sfetrx4_dma32.h"

#include "xsdr_ctrl.h"

#include "../device/ext_pciefe/ext_pciefe.h"
#include "../device/ext_exm2pe/board_exm2pe.h"
#include "../device/ext_fe_ch4_400_7200/ext_fe_ch4_400_7200.h"

#define USBEN 1

static inline int dev_gpo_set(lldev_t dev, unsigned bank, unsigned data)
{
    return lowlevel_reg_wr32(dev, 0, 0, ((bank & 0x7f) << 24) | (data & 0xff));
}

static int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, 0, 16 + (bank / 4), data);
}

enum {
    SRF4_FIFOBSZ = 0x10000, // 64kB
};

enum {
    I2C_BUS_FRONTEND = MAKE_LSOP_I2C_ADDR(0, 1, 0),
};

static const usdr_dev_param_constant_t s_params_m2_lm7_1_rev_generic[] = {
    { DNLL_I2C_COUNT, 1 },
    { DNLL_IRQ_COUNT, 8 },
};

// sSDR external I2C bus (3/4)
static const usdr_dev_param_constant_t s_params_m2_lm7_1_rev_advanced[] = {
    { DNLL_I2C_COUNT, 2 },
    { DNLL_IRQ_COUNT, 9 },

    { "/ll/i2c/1/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_DI2C_SIMPLE) },
    { "/ll/i2c/1/base", REG_SPI_I2C2 },
    { "/ll/i2c/1/irq",  M2PCI_INT_I2C_1 },
};

//
static
const usdr_dev_param_constant_t s_params_m2_lm7_1_rev000[] = {
    { DNLL_SPI_COUNT, 1 },
   // { DNLL_I2C_COUNT, 1 },
    { DNLL_SRX_COUNT, 1 },
    { DNLL_STX_COUNT, 1 },
    { DNLL_RFE_COUNT, 1 },
    { DNLL_TFE_COUNT, 0 },
    { DNLL_IDX_REGSP_COUNT, 1 },
  //  { DNLL_IRQ_COUNT, 8 }, //TODO fix segfault when int count < configured
    { DNLL_DRP_COUNT, 2 },
    { DNLL_BUCKET_COUNT, 1 },
    { DNLL_GPO_COUNT, 1 },
    { DNLL_GPI_COUNT, 1 },

    // low level buses
    { "/ll/irq/0/core", USDR_MAKE_COREID(USDR_CS_AUX, USDR_AC_PIC32_PCI) },
    { "/ll/irq/0/base", M2PCI_REG_INT },
    { "/ll/spi/0/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_SIMPLE) },
    { "/ll/spi/0/base", M2PCI_REG_SPI0 },
    { "/ll/spi/0/irq",  M2PCI_INT_SPI_0 },
    { "/ll/i2c/0/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_DI2C_SIMPLE) },
    { "/ll/i2c/0/base", M2PCI_REG_I2C },
    { "/ll/i2c/0/irq",  M2PCI_INT_I2C_0 },
    { "/ll/qspi_flash/base", M2PCI_REG_QSPI_FLASH },
    // Indexed area map
    { "/ll/idx_regsp/0/base", M2PCI_REG_WR_BADDR },
    { "/ll/idx_regsp/0/virt_base", VIRT_CFG_SFX_BASE },

    { "/ll/gpio/0/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_GPIO15_SIMPLE) },
    { "/ll/gpio/0/base", M2PCI_REG_GPIO_S },
    { "/ll/gpio/0/irq",  -1 },
    { "/ll/uart/0/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_UART_SIMPLE) },
    { "/ll/uart/0/base", REG_UART_TRX },
    { "/ll/uart/0/irq",  -1 },

    { "/ll/drp/0/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_DRP_PHY_V0) },
    { "/ll/drp/0/base", REG_CFG_PHY_0 },
    { "/ll/drp/1/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_DRP_PHY_V0) },
    { "/ll/drp/1/base", REG_CFG_PHY_1 },

    // data stream cores
    { "/ll/srx/0/core",    USDR_MAKE_COREID(USDR_CS_STREAM, USDR_SC_RXDMA_BRSTN) },
    { "/ll/srx/0/base",    M2PCI_REG_WR_RXDMA_CONFIRM},
    { "/ll/srx/0/cfg_base",VIRT_CFG_SFX_BASE },
    { "/ll/srx/0/irq",     M2PCI_INT_RX},
    { "/ll/srx/0/dmacap",  0x855 },
    { "/ll/srx/0/rfe",     (uintptr_t)"/ll/rfe/0" },
    { "/ll/rfe/0/fifobsz", SRF4_FIFOBSZ },  // 64kB
    { "/ll/rfe/0/core",    USDR_MAKE_COREID(USDR_CS_FE, USDR_FC_BRSTN) },
    { "/ll/rfe/0/base",    CSR_RFE4_BASE /*VIRT_CFG_SFX_BASE + 256 */},

    { "/ll/stx/0/core",    USDR_MAKE_COREID(USDR_CS_STREAM, USDR_SC_TXDMA_OLD) },
    { "/ll/stx/0/base",    M2PCI_REG_WR_TXDMA_CNF_L},
    { "/ll/stx/0/cfg_base",VIRT_CFG_SFX_BASE + 512 },
    { "/ll/stx/0/irq",     M2PCI_INT_TX},
    { "/ll/stx/0/dmacap",  0x555 },

    { "/ll/qspi_flash/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_QSPI_FLASH_24_RW) },
    { "/ll/qspi_flash/base", M2PCI_REG_QSPI_FLASH },
//    { "/ll/qspi_flash/master_off", 0x1C0000 },

    { "/ll/gpi/0/core", USDR_MAKE_COREID(USDR_CS_GPI, USDR_GPI_32BIT_12) },
    { "/ll/gpi/0/base", M2PCI_REG_RD_GPI0_12 },
    { "/ll/gpo/0/core", USDR_MAKE_COREID(USDR_CS_GPO, USDR_GPO_8BIT) },
    { "/ll/gpo/0/base", M2PCI_REG_STAT_CTRL },

    { "/ll/bucket/0/core", USDR_MAKE_COREID(USDR_CS_BUCKET, USDR_BUCKET_16B) },
    { "/ll/bucket/0/base", M2PCI_REG_WR_PNTFY_CFG },

    { "/ll/sync/0/core",   USDR_MAKE_COREID(USDR_CS_SYNC, USDR_SYNC_SIMPLE) },
    { "/ll/sync/0/base",   M2PCI_REG_WR_SYNC_CTRL},

    { "/ll/dsp/atcrbs/0/core", USDR_MAKE_COREID(USDR_CS_DSP, 0x23675e) },
    { "/ll/dsp/atcrbs/0/base", M2PCI_REG_WR_LBDSP },

    { "/ll/sdr/0/rfic/0", (uintptr_t)"lms7002m" },

    { "/ll/sdr/max_hw_rx_chans",  2 },
    { "/ll/sdr/max_hw_tx_chans",  2 },

    { "/ll/sdr/max_sw_rx_chans",  2 },
    { "/ll/sdr/max_sw_tx_chans",  2 },

    { "/ll/poll_event/in",  M2PCI_INT_RX },
    { "/ll/poll_event/out", M2PCI_INT_TX },

    // Frontend interface
    { "/ll/fe/0/gpio_busno/0", 0},
    { "/ll/fe/0/uart_busno/0", 0},

    { "/ll/fe/0/spi_busno/0", -1},
    { "/ll/fe/0/i2c_busno/0", -1},
};

static const usdr_dev_param_constant_t xsdr_params_m2_lm7_1_rev000[] = {
    { "/ll/device/name",  (uintptr_t)"xsdr"},
};

static const usdr_dev_param_constant_t ssdr_params_m2_lm7_1_rev000[] = {
    { "/ll/device/name",  (uintptr_t)"ssdr"},
};

static int dev_m2_lm7_1_rate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_rate_m_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_debug_all_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dev_m2_lm7_1_pwren_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_sdr_tdd_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_rx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_tx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_rx_gain_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_tx_gain_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_tx_gainlb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_tx_gainbb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_sdr_rx_bbfreq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_tx_bbfreq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_sdr_rx_bandwidth_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_tx_bandwidth_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_sdr_rx_gainpga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_rx_gainvga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_rx_gainlna_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_rx_gainlb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_sdr_rfic_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_rx_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_tx_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_senstemp_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue);
static int dev_m2_lm7_1_debug_lms7002m_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_debug_lms7002m_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dev_m2_lm7_1_debug_lms8001_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_debug_lms8001_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);


static int dev_m2_lm7_1_sdr_rx_dccorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_tx_dccorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_sdr_rx_dccorrmode_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_sdr_rx_phgaincorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_tx_phgaincorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_sdr_refclk_frequency_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_refclk_frequency_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dev_m2_lm7_1_sdr_refclk_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_usb_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dev_m2_lm7_1_sdr_rxdsp_swapab_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_tx_antennat_port_cfg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_rfe_throttle_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_sensor_freqpps_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* value);
static int dev_m2_lm7_1_rfe_nco_pwrdc_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* value);

static int dev_m2_lm7_1_rfe_nco_enable_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_rfe_nco_enable_frequency(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_tfe_gen_en_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_tfe_gen_const_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_tfe_gen_tone_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_tfe_nco_enable_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_tfe_nco_enable_frequency(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_calibrate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_calibrate_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* value);

static int dev_m2_lm7_1_usbclk_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_dev_atcrbs_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_dev_atcrbs_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* value);


static int dev_m2_lm7_1_dev_dac_vctcxo_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_phyrxlm_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_phy_rx_dly_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_phy_rx_lfsr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_phy_rx_lfsr_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue);

static int dev_m2_lm7_1_phy_tx_lfsr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_phy_tx_iqsel_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_lms7002rxlml_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_debug_clkinfo_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm7_1_revision_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue);
static int dev_m2_lm7_1_qspi_flash_master_off_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue);

static int dev_m2_lm7_1_sdr_tx_phase_ovr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_rx_phase_ovr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_tx_phase_ovr_iq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm7_1_sdr_tx_phase_ovr_rc_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);


static int dev_m2_lm7_1_sdr_vio_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static
const usdr_dev_param_func_t s_fparams_m2_lm7_1_rev000[] = {
    { "/dm/rate/master",        { dev_m2_lm7_1_rate_set, NULL }},
    { "/dm/rate/rxtxadcdac",    { dev_m2_lm7_1_rate_m_set, NULL }},

    { "/dm/debug/all",          { NULL, dev_m2_lm7_1_debug_all_get }},
    { "/dm/power/en",           { dev_m2_lm7_1_pwren_set, NULL }},

    { "/dm/sdr/channels",       { NULL, NULL }},
    { "/dm/sensor/temp",        { NULL, dev_m2_lm7_1_senstemp_get }},

    { "/dm/sdr/0/usbclk",         { dev_m2_lm7_1_usbclk_set,  NULL }},
    { "/dm/sdr/0/calibrate",      { dev_m2_lm7_1_calibrate_set, dev_m2_lm7_1_calibrate_get }},

    { "/dm/sdr/refclk/frequency", {dev_m2_lm7_1_sdr_refclk_frequency_set, dev_m2_lm7_1_sdr_refclk_frequency_get}},
    { "/dm/sdr/refclk/path",      {dev_m2_lm7_1_sdr_refclk_path_set, NULL}},


    { "/dm/sdr/0/vio",          { dev_m2_lm7_1_sdr_vio_set, NULL }},
    { "/dm/sdr/0/tx/phase_ovr", { dev_m2_lm7_1_sdr_tx_phase_ovr_set, NULL }},
    { "/dm/sdr/0/tx/phase_ovr_iq", { dev_m2_lm7_1_sdr_tx_phase_ovr_iq_set, NULL }},
    { "/dm/sdr/0/tx/phase_ovr_rc", { dev_m2_lm7_1_sdr_tx_phase_ovr_rc_set, NULL }},
    { "/dm/sdr/0/rx/phase_ovr", { dev_m2_lm7_1_sdr_rx_phase_ovr_set, NULL }},

    { "/dm/sdr/0/rx/dccorr",    { dev_m2_lm7_1_sdr_rx_dccorr_set, NULL }},
    { "/dm/sdr/0/tx/dccorr",    { dev_m2_lm7_1_sdr_tx_dccorr_set, NULL }},
    { "/dm/sdr/0/rx/phgaincorr",{ dev_m2_lm7_1_sdr_rx_phgaincorr_set, NULL }},
    { "/dm/sdr/0/tx/phgaincorr",{ dev_m2_lm7_1_sdr_tx_phgaincorr_set, NULL }},

    { "/dm/sdr/0/rx/freqency/bb",  { dev_m2_lm7_1_sdr_rx_bbfreq_set, NULL }},
    { "/dm/sdr/0/tx/freqency/bb",  { dev_m2_lm7_1_sdr_tx_bbfreq_set, NULL }},

    { "/dm/sdr/0/rx/freqency",  { dev_m2_lm7_1_sdr_rx_freq_set, NULL }},
    { "/dm/sdr/0/tx/freqency",  { dev_m2_lm7_1_sdr_tx_freq_set, NULL }},
    { "/dm/sdr/0/rx/gain",      { dev_m2_lm7_1_sdr_rx_gain_set, NULL }},
    { "/dm/sdr/0/tx/gain",      { dev_m2_lm7_1_sdr_tx_gain_set, NULL }},
    { "/dm/sdr/0/tx/gain/lb",   { dev_m2_lm7_1_sdr_tx_gainlb_set, NULL }},
    { "/dm/sdr/0/tx/gain/vga1", { dev_m2_lm7_1_sdr_tx_gainbb_set, NULL }},

    { "/dm/sdr/0/rx/gain/pga",  { dev_m2_lm7_1_sdr_rx_gainpga_set, NULL }},
    { "/dm/sdr/0/rx/gain/vga",  { dev_m2_lm7_1_sdr_rx_gainvga_set, NULL }},
    { "/dm/sdr/0/rx/gain/lna",  { dev_m2_lm7_1_sdr_rx_gainlna_set, NULL }},
    { "/dm/sdr/0/rx/gain/lb",   { dev_m2_lm7_1_sdr_rx_gainlb_set, NULL }},

    { "/dm/sdr/0/rx/rfic_path", { dev_m2_lm7_1_sdr_rfic_path_set, NULL }},
    { "/dm/sdr/0/tx/rfic_path", { dev_m2_lm7_1_sdr_rfic_path_set, NULL }},

    { "/dm/sdr/0/rx/path",      { dev_m2_lm7_1_sdr_rx_path_set, NULL }},
    { "/dm/sdr/0/tx/path",      { dev_m2_lm7_1_sdr_tx_path_set, NULL }},

    { "/dm/sdr/0/rx/dccorrmode",  { dev_m2_lm7_1_sdr_rx_dccorrmode_set, NULL }},

    { "/dm/sdr/0/rx/bandwidth", { dev_m2_lm7_1_sdr_rx_bandwidth_set, NULL }},
    { "/dm/sdr/0/tx/bandwidth", { dev_m2_lm7_1_sdr_tx_bandwidth_set, NULL }},

    { "/dm/sdr/0/rxdsp/swapab", { dev_m2_lm7_1_sdr_rxdsp_swapab_set, NULL }},

    { "/dm/sdr/0/tdd/freqency",          { dev_m2_lm7_1_sdr_tdd_freq_set, NULL }},
    { "/dm/sdr/0/tfe/antcfg",            { dev_m2_lm7_1_tx_antennat_port_cfg_set, NULL }},

    { "/dm/sdr/0/tfe/generator/enable",  { dev_m2_lm7_1_tfe_gen_en_set, NULL }},
    { "/dm/sdr/0/tfe/generator/const",   { dev_m2_lm7_1_tfe_gen_const_set, NULL }},
    { "/dm/sdr/0/tfe/generator/tone",    { dev_m2_lm7_1_tfe_gen_tone_set, NULL }},
    { "/dm/sdr/0/tfe/nco/enable",        { dev_m2_lm7_1_tfe_nco_enable_set, NULL }},
    { "/dm/sdr/0/tfe/nco/freqency",      { dev_m2_lm7_1_tfe_nco_enable_frequency, NULL }},

    { "/dm/sdr/0/rfe/throttle",    { dev_m2_lm7_1_rfe_throttle_set, NULL }},

    { "/dm/sdr/0/rfe/nco/enable",  { dev_m2_lm7_1_rfe_nco_enable_set, NULL }},
    { "/dm/sdr/0/rfe/nco/freqency",{ dev_m2_lm7_1_rfe_nco_enable_frequency, NULL }},
    { "/dm/sdr/0/rfe/pwrdc",       { NULL, dev_m2_lm7_1_rfe_nco_pwrdc_get }},

    // Debug interface
    { "/debug/hw/lms7002m/0/reg",  { dev_m2_lm7_1_debug_lms7002m_reg_set, dev_m2_lm7_1_debug_lms7002m_reg_get }},
    { "/debug/hw/lms8001/0/reg" ,  { dev_m2_lm7_1_debug_lms8001_reg_set, dev_m2_lm7_1_debug_lms8001_reg_get }},


    // USB debug interface
    { "/dm/usb",                   { NULL, dev_m2_lm7_1_usb_get }},

    // Get sampled amount of ticks between GPS
    { "/dm/sensor/freqpps",        { NULL, dev_m2_lm7_1_sensor_freqpps_get }},

    { "/dm/sdr/0/core/atcrbs/reg",        { dev_m2_lm7_1_dev_atcrbs_set, dev_m2_lm7_1_dev_atcrbs_get }},

    { "/dm/sdr/0/dac_vctcxo",      { dev_m2_lm7_1_dev_dac_vctcxo_set, NULL }},

    { "/dm/sdr/0/phy_rx_dly",       { dev_m2_lm7_1_phy_rx_dly_set, NULL }},
    { "/dm/sdr/0/phy_rx_lfsr",      { dev_m2_lm7_1_phy_rx_lfsr_set, dev_m2_lm7_1_phy_rx_lfsr_get }},
    { "/dm/sdr/0/phy_tx_lfsr",      { dev_m2_lm7_1_phy_tx_lfsr_set, NULL }},
    { "/dm/sdr/0/phy_tx_iqsel",     { dev_m2_lm7_1_phy_tx_iqsel_set, NULL }},

    { "/dm/sdr/0/phyrxlml",         { dev_m2_lm7_1_phyrxlm_set, NULL }},
    { "/debug/hw/lms7002m/0/rxlml", { dev_m2_lm7_1_lms7002rxlml_set, NULL }},
    { "/debug/clk_info",            { dev_m2_lm7_1_debug_clkinfo_set, NULL }},

    { "/dm/revision",               { NULL, dev_m2_lm7_1_revision_get }},

    { "/ll/qspi_flash/master_off",  { NULL, dev_m2_lm7_1_qspi_flash_master_off_get }},
};

struct dev_m2_lm7_1_gps {
    device_t base;

    lowlevel_ops_t my_ops;
    lowlevel_ops_t* p_original_ops;

    uint32_t debug_lms7002m_last;
    uint32_t debug_lms8001_last;
    // Cached device data

    struct xsdr_dev xdev;
    struct dev_fe* fe;
    bool bifurcation_en;
    bool nodecint;
    bool double_pump;

    int cal_data[8];

    stream_handle_t* rx;
    stream_handle_t* tx;
};

int dev_m2_lm7_1_debug_clkinfo_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return xsdr_clk_debug_info(&((struct dev_m2_lm7_1_gps *)ud)->xdev);
}

int dev_m2_lm7_1_dev_dac_vctcxo_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    board_ext_pciefe_t* board_fe = device_fe_to(d->fe, "pciefe");
    board_exm2pe_t* board = device_fe_to(d->fe, "exm2pe");
    ext_fe_ch4_400_7200_t* fe = device_fe_to(d->fe, "fe4ch4007200");
    if (board_fe) {
        return board_ext_pciefe_set_dac(board_fe, value);
    } else if (board) {
        return board_exm2pe_set_dac(board, value);
    } else if (fe) {
        return ext_fe_set_dac(fe, value);
    }

    return xsdr_trim_dac_vctcxo(&d->xdev, value);
}

int dev_m2_lm7_1_phyrxlm_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return xsdr_phy_tune_rx(&((struct dev_m2_lm7_1_gps *)ud)->xdev, value);
}

int dev_m2_lm7_1_phy_rx_dly_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    unsigned type = (value >> 8) & 0x1f;
    unsigned val = (value & 0x1f);

    return xsdr_config_rcvdly(&((struct dev_m2_lm7_1_gps *)ud)->xdev, type, val);
}

int dev_m2_lm7_1_phy_rx_lfsr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return xsdr_phy_en_lfsr_checker_mimo(&((struct dev_m2_lm7_1_gps *)ud)->xdev, value ? true : false);
}

int dev_m2_lm7_1_phy_tx_lfsr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return xsdr_phy_en_lfsr_generator_mimo(&((struct dev_m2_lm7_1_gps *)ud)->xdev,
                                           (value & 1) ? true : false,
                                           (value & 2) ? true : false);
}

int dev_m2_lm7_1_phy_tx_iqsel_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return xsdr_phy_tx_iqsel(&((struct dev_m2_lm7_1_gps *)ud)->xdev, value);
}


int dev_m2_lm7_1_phy_rx_lfsr_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    uint32_t v[4];
    uint64_t val = 0;
    int res = xsdr_phy_lfsr_mimo_state(&((struct dev_m2_lm7_1_gps *)ud)->xdev, LFSR_CNTR_BER, v);
    if (res)
        return res;

    for (unsigned i = 0; i < 4; i++) {
        uint64_t k = v[i];
        if (k > UINT16_MAX) {
            k = UINT16_MAX;
        }
        val |= k << (16 * i);
    }
    *ovalue = val;
    return 0;
}

int dev_m2_lm7_1_lms7002rxlml_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return lms7002m_set_lmlrx_mode(&((struct dev_m2_lm7_1_gps *)ud)->xdev.base, value);
}

int dev_m2_lm7_1_dev_atcrbs_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int res;

    res = lowlevel_reg_wr32(d->base.dev, 0, M2PCI_REG_WR_LBDSP, value);
    return res;
}

int dev_m2_lm7_1_dev_atcrbs_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int res;
    uint32_t value32 = 0;

    res = lowlevel_reg_rd32(d->base.dev, 0, M2PCI_REG_RD_LBDSP, &value32);
    *value = value32;
    return res;
}

int dev_m2_lm7_1_sdr_tx_phase_ovr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    d->xdev.tx_override_phase = value;
    return 0;
}

int dev_m2_lm7_1_sdr_tx_phase_ovr_rc_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    return xsdr_txphase_ovr(&d->xdev, value);
}


int dev_m2_lm7_1_sdr_tx_phase_ovr_iq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    d->xdev.tx_override_phase_iq = value;
    return 0;
}

int dev_m2_lm7_1_sdr_rx_phase_ovr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    d->xdev.rx_override_phase = value;
    return 0;
}

int dev_m2_lm7_1_sdr_vio_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    return xsdr_set_vio(&d->xdev, value);
}

int dev_m2_lm7_1_debug_lms7002m_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int res;
    unsigned chan = (unsigned)(value >> 32);

    res = lms7002m_mac_set(&d->xdev.base.lmsstate, chan);
    if (res)
        return res;

    d->debug_lms7002m_last = ~0u;
    res = lowlevel_spi_tr32(d->base.dev, 0, SPI_LMS7, value & 0xffffffff, &d->debug_lms7002m_last);
    USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug LMS7/%d REG %08x => %08x\n",
             lowlevel_get_devname(d->base.dev), chan, (unsigned)value,
             d->debug_lms7002m_last);
    return res;
}

int dev_m2_lm7_1_debug_lms8001_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int res = xsdr_trspi_lms8(&d->xdev, value & 0xffffffff, &d->debug_lms8001_last);
    USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug LMS8 REG %08x => %08x\n",
             lowlevel_get_devname(d->base.dev), (unsigned)value,
             d->debug_lms8001_last);
    return res;
}


int dev_m2_lm7_1_sdr_rx_dccorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int res = 0;
    unsigned chan = (unsigned)(value >> 32);
    unsigned vi = (value >> 16) & 0xffff;
    unsigned vq = (value >> 0) & 0xffff;

    if (!(chan & LMS7_CH_AB))
        return -EINVAL;

    if ((chan & LMS7_CH_A) == LMS7_CH_A) {
        res = res ? res : lms7002m_dc_corr(&d->xdev.base.lmsstate, P_RXA_I, vi);
        res = res ? res : lms7002m_dc_corr(&d->xdev.base.lmsstate, P_RXA_Q, vq);
    }
    if ((chan & LMS7_CH_B) == LMS7_CH_B) {
        res = res ? res : lms7002m_dc_corr(&d->xdev.base.lmsstate, P_RXB_I, vi);
        res = res ? res : lms7002m_dc_corr(&d->xdev.base.lmsstate, P_RXB_Q, vq);
    }

    return res;
}

int dev_m2_lm7_1_sdr_rx_dccorrmode_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int res = 0;
    unsigned chan = (unsigned)(value >> 32);
    res = (res) ? res : lms7002m_mac_set(&d->xdev.base.lmsstate, chan);
    //res = (res) ? res : lms7_rxtsp_dc_corr_off(&d->xdev.base.lmsstate);

    ///////////////////////////// FIXME: ////////////////////////////////////////////
    res = (res) ? res : lms7002m_rxtsp_dc_corr(&d->xdev.base.lmsstate, true, 0);
    return res;
}


int dev_m2_lm7_1_sdr_rx_phgaincorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int res = 0;
    unsigned chan = ((unsigned)(value >> 32)) & 0xffff;
    res = (res) ? res : lms7002m_mac_set(&d->xdev.base.lmsstate, chan);
    unsigned ig = (value & 0xffff);
    unsigned qg = ((value >> 16) & 0xffff);
    unsigned pcorr = ((value >> 48) & 0xffff);

    USDR_LOG("UDEV", USDR_LOG_NOTE, "RXGAC CH=%d I=%d Q=%d A=%d\n", chan, ig, qg, pcorr);

    res = (res) ? res : lms7002m_xxtsp_iq_gcorr(&d->xdev.base.lmsstate, LMS_RXTSP, ig, qg);
    res = (res) ? res : lms7002m_xxtsp_iq_phcorr(&d->xdev.base.lmsstate, LMS_RXTSP, pcorr);

    return res;
}

int dev_m2_lm7_1_sdr_tx_phgaincorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int res = 0;
    unsigned chan = ((unsigned)(value >> 32)) & 0xffff;
    res = (res) ? res : lms7002m_mac_set(&d->xdev.base.lmsstate, chan);
    unsigned ig = (value & 0xffff);
    unsigned qg = ((value >> 16) & 0xffff);
    unsigned pcorr = ((value >> 48) & 0xffff);

    USDR_LOG("UDEV", USDR_LOG_NOTE, "TXGAC CH=%d I=%d Q=%d A=%d\n", chan, ig, qg, pcorr);

    res = (res) ? res : lms7002m_xxtsp_iq_gcorr(&d->xdev.base.lmsstate, LMS_TXTSP, ig, qg);
    res = (res) ? res : lms7002m_xxtsp_iq_phcorr(&d->xdev.base.lmsstate, LMS_TXTSP, pcorr);

    return res;
}

int dev_m2_lm7_1_sdr_tx_dccorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int res = 0;
    unsigned chan = (unsigned)(value >> 32);
    unsigned vi = (value >> 16) & 0xffff;
    unsigned vq = (value >> 0) & 0xffff;

    if (!(chan & LMS7_CH_AB))
        return -EINVAL;

    if ((chan & LMS7_CH_A) == LMS7_CH_A) {
        res = res ? res : lms7002m_dc_corr(&d->xdev.base.lmsstate, P_TXA_I, vi);
        res = res ? res : lms7002m_dc_corr(&d->xdev.base.lmsstate, P_TXA_Q, vq);
    }
    if ((chan & LMS7_CH_B) == LMS7_CH_B) {
        res = res ? res : lms7002m_dc_corr(&d->xdev.base.lmsstate, P_TXB_I, vi);
        res = res ? res : lms7002m_dc_corr(&d->xdev.base.lmsstate, P_TXB_Q, vq);
    }

    return res;
}

int dev_m2_lm7_1_tfe_gen_en_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int res = 0;
    unsigned chan = (unsigned)(value >> 32);
    res = (res) ? res : lms7002m_mac_set(&d->xdev.base.lmsstate, chan);
    res = (res) ? res : lms7002m_xxtsp_gen(&d->xdev.base.lmsstate, LMS_TXTSP, XXTSP_NORMAL, 0, 0);
    return res;
}

int dev_m2_lm7_1_tfe_gen_const_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int res = 0;
    unsigned chan = (unsigned)(value >> 32);
    unsigned vi = (value >> 16) & 0xffff;
    unsigned vq = (value >> 0) & 0xffff;

    res = (res) ? res : lms7002m_mac_set(&d->xdev.base.lmsstate, chan);
    res = (res) ? res : lms7002m_xxtsp_gen(&d->xdev.base.lmsstate, LMS_TXTSP, XXTSP_DC, vi, vq);
    return res;
}

int dev_m2_lm7_1_tfe_gen_tone_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int res = 0;
    unsigned chan = (unsigned)(value >> 32);
    res = (res) ? res : lms7002m_mac_set(&d->xdev.base.lmsstate, chan);
    res = (res) ? res : lms7002m_xxtsp_gen(&d->xdev.base.lmsstate, LMS_TXTSP, XXTSP_DC, value & 2, value & 1);
    return res;
}

int dev_m2_lm7_1_tfe_nco_enable_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return 0;
}

int dev_m2_lm7_1_tfe_nco_enable_frequency(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int res = 0;
    unsigned chan = (unsigned)(value >> 32);
    res = (res) ? res : lms7002m_mac_set(&d->xdev.base.lmsstate, chan);
    res = (res) ? res : lms7002m_xxtsp_cmix(&d->xdev.base.lmsstate, LMS_TXTSP, value);
    return res;
}


int dev_m2_lm7_1_debug_lms7002m_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    *ovalue = d->debug_lms7002m_last;
    return 0;
}

int dev_m2_lm7_1_debug_lms8001_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    *ovalue = d->debug_lms8001_last;
    return 0;
}

int dev_m2_lm7_1_rfe_throttle_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    if (d->rx) {
        return d->rx->ops->option_set(d->rx, "throttle", value);
    }
    return -EINVAL;
}


int dev_m2_lm7_1_rfe_nco_enable_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    //struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    //return sfe_rf4_nco_enable(d->base.dev, 0, CSR_RFE4_BASE, (value & 0xff) ? true : false, value >> 32);
    return -EINVAL;
}
int dev_m2_lm7_1_rfe_nco_enable_frequency(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    //struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    //return sfe_rf4_nco_freq(d->base.dev, 0, CSR_RFE4_BASE, (int)value);
    return -EINVAL;
}
int dev_m2_lm7_1_rfe_nco_pwrdc_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int res;
    int32_t val[2];

    res = lowlevel_reg_rdndw(d->base.dev, 0, /* REG_RD_UNUSED_0 */ 7, (uint32_t*)&val[0], 2);

    USDR_LOG("UDEV", USDR_LOG_NOTE, "DCV I=%d Q=%d\n", val[0], val[1]);

    double i = val[0];
    double q = val[1];

    double pwr = i*i + q*q;
    double log = 1000 * 10 * log10(pwr);

    *value = (val[0] == 0 && val[1] == 0) ? 1000000ul : log;
    return res;
}

int dev_m2_lm7_1_calibrate_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* value)
{
     struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
     *value = (intptr_t)&d->cal_data[0];
     return 0;
}


int dev_m2_lm7_1_usbclk_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    return xsdr_usbclk(&d->xdev, value ? true : false);
}

int dev_m2_lm7_1_calibrate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int res;
    unsigned flags = value & 0xfffff;
    unsigned chan = value >> 32;

    if (flags > 65536 || chan > 1) {
        const char* v = (const char* )value;
        chan = 0; // TODO B
        flags = 0;

        if (strncmp(v, "e", 1) == 0) {
            v += 1;
            flags |= XSDR_CAL_EXT_FB;
        }

        if (strncmp(v, "a:", 2) == 0) {
            v += 2;
        } if (strncmp(v, "b:", 2) == 0) {
            v += 2;
            chan = 1;
        }

        if (strcmp(v, "txlo") == 0) {
            flags |= XSDR_CAL_TXLO;
        } else if (strcmp(v, "rxlo") == 0) {
            flags |= XSDR_CAL_RXLO;
        } else if (strcmp(v, "txiqimb") == 0) {
            flags |= XSDR_CAL_TXIQIMB;
        } else if (strcmp(v, "rxiqimb") == 0) {
            flags |= XSDR_CAL_RXIQIMB;
        } else if (strcmp(v, "tx") == 0) {
            flags |= XSDR_CAL_TXLO | XSDR_CAL_TXIQIMB;
        } else if (strcmp(v, "rx") == 0) {
            flags |= XSDR_CAL_RXLO | XSDR_CAL_RXIQIMB;
        } else if (strcmp(v, "all") == 0) {
            flags |= XSDR_CAL_TXLO | XSDR_CAL_TXIQIMB | XSDR_CAL_RXLO | XSDR_CAL_RXIQIMB;
        } else if (strcmp(v, "lo") == 0) {
            flags |= XSDR_CAL_TXLO | XSDR_CAL_RXLO;
        } else {
            return -EINVAL;
        }
    }

    res = xsdr_calibrate(&d->xdev, chan, flags, &d->cal_data[0]);
    return res;
}



enum {
    RATE_MIN = 90000,
    RATE_MAX = 195000000,
};

int dev_m2_lm7_1_senstemp_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int temp, res;

    res = xsdr_gettemp(&d->xdev, &temp);
    *ovalue = (int64_t)temp;
    return res;
}

#define MAX(x,y) (((x) > (y)) ? (x) : (y))
\

int dev_m2_lm7_1_rate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    if (value < RATE_MIN || value > RATE_MAX)
        return -ERANGE;

    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;

    //Simple SISO RX only
    return xsdr_set_samplerate_ex(&d->xdev, (unsigned)value, (unsigned)value, 0, 0,
                                  (d->bifurcation_en) ? (XSDR_LML_SISO_DDR_RX | XSDR_LML_SISO_DDR_TX) : 0 |
                                  (d->nodecint ? 0 : XSDR_SR_MAXCONVRATE) | XSDR_SR_EXTENDED_CGEN);
}

int dev_m2_lm7_1_rate_m_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    uint32_t *rates = (uint32_t *)(uintptr_t)value;

    uint32_t rx_rate = rates[0];
    uint32_t tx_rate = rates[1];

    uint32_t adc_rate = rates[2];
    uint32_t dac_rate = rates[3];

    if (rx_rate == 0 && tx_rate == 0)
        return -EINVAL;

    if ((rx_rate != 0) && (rx_rate < RATE_MIN || rx_rate > RATE_MAX))
        return -ERANGE;

    if ((tx_rate != 0) && (tx_rate < RATE_MIN || tx_rate > RATE_MAX))
        return -ERANGE;

    return xsdr_set_samplerate_ex(&d->xdev, rx_rate, tx_rate, adc_rate, dac_rate,
                                  (d->bifurcation_en) ? (XSDR_LML_SISO_DDR_RX | XSDR_LML_SISO_DDR_TX) : 0 |
                                  (d->nodecint ? 0 : XSDR_SR_MAXCONVRATE) | XSDR_SR_EXTENDED_CGEN);
}

int dev_m2_lm7_1_debug_all_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    union {
        uint32_t i32[4];
        uint64_t i64[2];
    } data;

    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int res = 0;

    for (unsigned i = 0; i < 4; i++) {
        res = lowlevel_reg_rd32(d->base.dev, 0, M2PCI_REG_RD_TXDMA_STAT + i, &data.i32[i]);
        if (res)
            return res;
    }

    *ovalue++ = data.i64[0];
    *ovalue   = data.i64[1];

    return res;
}

int dev_m2_lm7_1_sdr_tdd_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    return xsdr_rfic_fe_set_freq(&d->xdev, LMS7_CH_AB, RFIC_LMS7_TX_AND_RX_TDD, value, NULL);
}

int dev_m2_lm7_1_sdr_rx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    return xsdr_rfic_fe_set_freq(&d->xdev, LMS7_CH_AB, RFIC_LMS7_TUNE_RX_FDD, value, NULL);
}
int dev_m2_lm7_1_sdr_tx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    return xsdr_rfic_fe_set_freq(&d->xdev, LMS7_CH_AB, RFIC_LMS7_TUNE_TX_FDD, value, NULL);
}

int dev_m2_lm7_1_sdr_rx_bbfreq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    unsigned channel = value >> 32;
    int32_t freq = (int32_t)(value & 0xffffffff);

    return xsdr_rfic_bb_set_freq(&d->xdev, channel, false, freq);
}
int dev_m2_lm7_1_sdr_tx_bbfreq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    unsigned channel = value >> 32;
    int32_t freq = (int32_t)(value & 0xffffffff);

    return xsdr_rfic_bb_set_freq(&d->xdev, channel, true, freq);
}

int dev_m2_lm7_1_sdr_rx_gain_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    return xsdr_rfic_set_gain(&d->xdev, LMS7_CH_AB, RFIC_LMS7_RX_LNA_GAIN, value, NULL);
}
int dev_m2_lm7_1_sdr_tx_gain_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    return xsdr_rfic_set_gain(&d->xdev, LMS7_CH_AB, RFIC_LMS7_TX_PAD_GAIN, value, NULL);
}
int dev_m2_lm7_1_sdr_tx_gainlb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    return xsdr_rfic_set_gain(&d->xdev, LMS7_CH_AB, RFIC_LMS7_TX_LB_GAIN, value, NULL);
}
int dev_m2_lm7_1_sdr_tx_gainbb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    return xsdr_rfic_set_gain(&d->xdev, LMS7_CH_AB, RFIC_LMS7_TX_PGA_GAIN, value, NULL);
}
int dev_m2_lm7_1_sdr_rx_bandwidth_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    return xsdr_rfic_bb_set_badwidth(&d->xdev, LMS7_CH_AB, false, value, NULL);
}
int dev_m2_lm7_1_sdr_tx_bandwidth_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    return xsdr_rfic_bb_set_badwidth(&d->xdev, LMS7_CH_AB, true, value, NULL);
}
int dev_m2_lm7_1_sdr_rx_gainpga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    return xsdr_rfic_set_gain(&d->xdev, LMS7_CH_AB, RFIC_LMS7_RX_PGA_GAIN, value, NULL);
}

int dev_m2_lm7_1_sdr_rx_gainvga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    return xsdr_rfic_set_gain(&d->xdev, LMS7_CH_AB, RFIC_LMS7_RX_TIA_GAIN, value, NULL);
}

int dev_m2_lm7_1_sdr_rx_gainlna_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    return xsdr_rfic_set_gain(&d->xdev, LMS7_CH_AB, RFIC_LMS7_RX_LNA_GAIN, value, NULL);
}

int dev_m2_lm7_1_sdr_rx_gainlb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    return xsdr_rfic_set_gain(&d->xdev, LMS7_CH_AB, RFIC_LMS7_RX_LB_GAIN, value, NULL);
}

int dev_m2_lm7_1_sdr_rxdsp_swapab_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t ovalue)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    return xsdr_rfic_streaming_xflags(&d->xdev, ovalue ? RFIC_SWAP_AB : 0, 0);
}

struct param_list_idx {
    const char* name;
    unsigned param;
};
typedef struct param_list_idx param_list_idx_t;

static const param_list_idx_t s_path_list[] = {
    { "rxl", XSDR_RX_L },
    { "rxw", XSDR_RX_W },
    { "rxh", XSDR_RX_H },
    { "rxl_lb",  XSDR_RX_L_TX_B2_LB },
    { "txb2_lb", XSDR_RX_L_TX_B2_LB },
    { "rxw_lb",  XSDR_RX_W_TX_B1_LB },
    { "txb1_lb", XSDR_RX_W_TX_B1_LB },
    { "rxh_lb", XSDR_RX_H_TX_B1_LB },
    { "rx_auto", XSDR_RX_AUTO },
    { "tx_auto", XSDR_TX_AUTO },
    { "txb1", XSDR_TX_B1 },
    { "txb2", XSDR_TX_B2 },
    { "txw", XSDR_TX_W },
    { "txh", XSDR_TX_H },
    { "adc", XSDR_RX_ADC_EXT },
};

static int find_param_list(const char* param, const param_list_idx_t* lst, unsigned size)
{
    for (unsigned i = 0; i < size; i++) {
        if (strcasecmp(lst[i].name, param) == 0) {
            return i;
        }
    }
    return -1;
}

int _sdr_get_path(const char* param)
{
    int idx = find_param_list(param, s_path_list, SIZEOF_ARRAY(s_path_list));
    if (idx < 0) {
        USDR_LOG("UDEV", USDR_LOG_WARNING, "m2_lm7_1_GPS: unknown '%s' path!\n",
                 param);
        return -EINVAL;
    }
    return idx;
}

int dev_m2_lm7_1_sdr_rfic_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    if (value > 4096) {
        int idx = _sdr_get_path((const char*)value);
        if (idx < 0)
            return idx;

        value = s_path_list[idx].param;
    }

    return xsdr_rfic_fe_set_lna(&d->xdev, LMS7_CH_AB, value);
}

int dev_m2_lm7_1_sdr_rx_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    if (value > 4096) {
        int idx = _sdr_get_path((const char*)value);
        if (idx < 0)
            return idx;

        value = s_path_list[idx].param;
    }

    return xsdr_rfic_rfe_set_path(&d->xdev, value);
}

int dev_m2_lm7_1_sdr_tx_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    if (value > 4096) {
        int idx = _sdr_get_path((const char*)value);
        if (idx < 0)
            return idx;

        value = s_path_list[idx].param;
    }

    return xsdr_rfic_tfe_set_path(&d->xdev, value);
}

int dev_m2_lm7_1_sdr_refclk_frequency_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    USDR_LOG("UDEV", USDR_LOG_WARNING, "m2_lm7_1_GPS: Set fref=%d\n", (unsigned)value);
    d->xdev.base.fref = value;
    return 0;
}

int dev_m2_lm7_1_sdr_refclk_frequency_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    *ovalue = d->xdev.base.fref;
    return 0;
}

int dev_m2_lm7_1_sdr_refclk_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    if (value > 4096) {
        const char* param = (const char*)value;
        if (strcasecmp("internal", param) == 0) {
            value = 0;
        } else if (strcasecmp("external", param) == 0) {
            value = 1;
        } else {
            return -EINVAL;
        }
    }

    xsdr_set_extref(&d->xdev, value ? true : false, d->xdev.base.fref);
    USDR_LOG("UDEV", USDR_LOG_INFO, "m2_lm7_1_GPS: set clk ref path to %d\n", (int)value);
    return 0;
}

static
void usdr_device_m2_lm7_1_destroy(pdevice_t udev)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)udev;

    // Destroy streams
    if (d->rx) {
        d->rx->ops->destroy(d->rx);
    }
    if (d->tx) {
        d->tx->ops->destroy(d->tx);
    }

    xsdr_dtor(&d->xdev);
    USDR_LOG("UDEV", USDR_LOG_INFO, "m2_lm7_1_GPS: turnoff\n");

    usdr_device_base_destroy(udev);
}

int dev_m2_lm7_1_tx_antennat_port_cfg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    USDR_LOG("UDEV", USDR_LOG_INFO, "m2_lm7_1_GPS: tx antennat port cfg:%d\n", (unsigned)value);
    return xsdr_tx_antennat_port_cfg(&d->xdev, value);
}

// RFIC power on
int dev_m2_lm7_1_pwren_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    bool on = (value) ? true : false;

    return xsdr_pwren(&d->xdev, on);
}

int dev_m2_lm7_1_sensor_freqpps_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *value)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    uint32_t v = 0;
    int res = dev_gpi_get32(d->base.dev, IGPI_CLK1PPS, &v);
    *value = v;
    return res;
}

static
int usdr_device_m2_lm7_1_lsop(lldev_t dev, subdev_t subdev,
           unsigned ls_op, lsopaddr_t ls_op_addr,
           size_t meminsz, void* pin, size_t memoutsz,
           const void* pout)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)lowlevel_get_device(dev);
    int res = -ENOENT;
    if (ls_op == USDR_LSOP_DRP) {
        res = xsdr_override_drp(&d->xdev, ls_op_addr, meminsz, pin, memoutsz, pout);
    }

    if (res != -ENOENT)
        return res;

    return d->p_original_ops->ls_op(dev, subdev, ls_op, ls_op_addr, meminsz, pin, memoutsz, pout);
}

xsdr_dev_t* get_xsdr_dev(pdevice_t udev)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)udev;
    return &d->xdev;
}

static
int usdr_device_m2_lm7_1_initialize(pdevice_t udev, unsigned pcount, const char** devparam, const char** devval)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)udev;
    lldev_t dev = d->base.dev;
    int res;
    const char* fe = NULL;

    d->bifurcation_en = false;
    d->nodecint = false;
    d->double_pump = false;

    for (unsigned i = 0; i < pcount; i++) {
        if (strcmp(devparam[i], "fe") == 0) {
            fe = devval[i];
        }
        if (strcmp(devparam[i], "bifurcation") == 0) {
            d->bifurcation_en = (devval[i]) ? atoi(devval[i]) : 1;
        }
        if (strcmp(devparam[i], "nodec") == 0) {
            d->nodecint = true;
        }
        if (strcmp(devparam[i], "dpump") == 0) {
            d->double_pump = true;
        }
    }

    res = xsdr_init(&d->xdev);
    if (res)
        return res;

    if (d->xdev.ssdr)
    {
        res = vfs_add_const_i64_vec(&udev->rootfs, ssdr_params_m2_lm7_1_rev000, SIZEOF_ARRAY(ssdr_params_m2_lm7_1_rev000));
        if (res)
            USDR_LOG("UDEV", USDR_LOG_WARNING, "Unable to set device name \"ssdr\"!\n");
    }
    else
    {
        res = vfs_add_const_i64_vec(&udev->rootfs, xsdr_params_m2_lm7_1_rev000, SIZEOF_ARRAY(xsdr_params_m2_lm7_1_rev000));
        if (res)
            USDR_LOG("UDEV", USDR_LOG_WARNING, "Unable to set device name \"xsdr\"!\n");
    }

    d->xdev.dpump = d->double_pump;

    // Proxy operations
    memcpy(&d->my_ops, lowlevel_get_ops(dev), sizeof (lowlevel_ops_t));
    d->my_ops.ls_op = &usdr_device_m2_lm7_1_lsop;
    d->p_original_ops = lowlevel_get_ops(dev);
    dev->ops = &d->my_ops;

    // Probe fe
    if (d->xdev.new_rev) {
        // Init FE
        res = device_fe_probe(udev, d->xdev.ssdr ? "m2b+m" : "m2a+e", fe, I2C_BUS_FRONTEND, &d->fe);
        if (res) {
            return res;
        }
    }

#if 0
    //Load DSP ucode
    lowlevel_reg_wr32(dev, 0, 0, 0x02000001);
    for (unsigned k = 0; k < SIZEOF_ARRAY(s_dsp_ucode_fir2); k++)
        lowlevel_reg_wr32(dev, 0, 0, s_dsp_ucode_fir2[k]);
    lowlevel_reg_wr32(dev, 0, 0, 0x02000000);
#endif


    return 0;
}


int dev_m2_lm7_1_usb_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    *ovalue = 0;
    return 0;
}

int dev_m2_lm7_1_qspi_flash_master_off_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    if (d->xdev.ssdr_pro) {
        *ovalue = 0x07b0000;
    } else {
        *ovalue = MASTER_IMAGE_OFF;
    }
    return 0;
}

int dev_m2_lm7_1_revision_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue)
{
    unsigned rev_lo, rev_hi;
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)ud;
    int res = 0;

    res = dev_gpi_get32(d->base.dev, IGPI_USR_ACCESS2, &rev_lo);
    if (res)
        return res;

    res = dev_gpi_get32(d->base.dev, IGPI_HWID, &rev_hi);
    if (res)
        return res;

    *ovalue = rev_hi;
    *ovalue = *ovalue << 32;
    *ovalue |= rev_lo;

    return res;
}

static const channel_map_info_t s_xsdr_chmap[] = {
    { "ai", 0 },
    { "aq", 1 },
    { "bi", 2 },
    { "bq", 3 },
    { "a", 0 },
    { "b", 1 },
    { NULL, CH_NULL },
};

int xsdr_map_channels(const usdr_channel_info_t* channels, channel_info_t* core_chans)
{
    return usdr_channel_info_map_default(channels, s_xsdr_chmap, 4, core_chans);
}

static
int usdr_device_m2_lm7_1_create_stream(device_t* dev, const char* sid, const char* dformat,
                                              const usdr_channel_info_t* channels, unsigned pktsyms,
                                              unsigned flags, const char* parameters, stream_handle_t** out_handle)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)dev;
    int res = -EINVAL;
    unsigned hwchs;
    channel_info_t lchans;

    if (getenv("USDR_BARE_DEV")) {
        return -EOPNOTSUPP;
    }

    res = xsdr_map_channels(channels, &lchans);
    if (res) {
        return res;
    }

    if (strstr(sid, "rx") != NULL) {
        if (d->rx) {
            return -EBUSY;
        }

        struct sfetrx4_config rxcfg;
        res = parse_sfetrx4(dformat, &lchans, pktsyms, channels->count, &rxcfg);
        if (res) {
            USDR_LOG("UDEV", USDR_LOG_ERROR, "Unable to parse RX stream configuration!\n");
            return res;
        }

        res = xsdr_hwchans_cnt(&d->xdev, true, rxcfg.logicchs);
        if (res) {
            return res;
        }

        // Disable bifurcation for now, since calibration NCO loop doesn't support it
        if (rxcfg.bifurcation_valid && d->bifurcation_en) {
            d->xdev.siso_sdr_active_rx = true;
            flags |= DMS_FLAG_BIFURCATION;
            // TODO: update samplerate settings
        }

        if (d->double_pump) {
            d->xdev.siso_sdr_active_rx = true;
        }

        // Reset samplerate with proper bifurcation flags
        if (rxcfg.bifurcation_valid != ((d->xdev.s_flags & XSDR_LML_SISO_DDR_RX) ? true : false)) {
            res = xsdr_set_samplerate_ex(&d->xdev, d->xdev.s_rxrate, d->xdev.s_txrate,
                                         d->xdev.s_adcclk, d->xdev.s_dacclk, d->xdev.s_flags);
            if (res) {
                return res;
            }
        }

        res = create_sfetrx4_stream(dev, CORE_SFERX_DMA32_R0, dformat, channels->count, &lchans, pktsyms,
                                    flags, M2PCI_REG_WR_RXDMA_CONFIRM, VIRT_CFG_SFX_BASE, 0,
                                    SRF4_FIFOBSZ, CSR_RFE4_BASE, &d->rx, &hwchs);
        if (res) {
            USDR_LOG("XSDR", USDR_LOG_ERROR, "Unable to create stream '%s': error=%d\n", sid, res);
            return res;
        }

        res = xsdr_prepare(&d->xdev, true, d->tx);
        if (res) {
            return res;
        }

        *out_handle = d->rx;
    } else if (strstr(sid, "tx") != NULL) {
        if (d->tx) {
            return -EBUSY;
        }

        struct sfetrx4_config txcfg;
        res = parse_sfetrx4(dformat, &lchans, pktsyms, channels->count, &txcfg);
        if (res) {
            USDR_LOG("UDEV", USDR_LOG_ERROR, "Unable to parse TX stream configuration!\n");
            return res;
        }

        res = xsdr_hwchans_cnt(&d->xdev, false, txcfg.logicchs);
        if (res) {
            return res;
        }

        // Add bifurcation flag
        if (txcfg.bifurcation_valid && d->bifurcation_en) {
            d->xdev.siso_sdr_active_tx = true;
            flags |= DMS_FLAG_BIFURCATION;
            // TODO: update samplerate settings
        }

        if (d->double_pump) {
            d->xdev.siso_sdr_active_tx = true;
        }

        res = xsdr_prepare(&d->xdev, d->rx, true);
        if (res) {
            return res;
        }

        res = create_sfetrx4_stream(dev, CORE_SFETX_DMA32_R0, dformat, channels->count, &lchans, pktsyms,
                                    flags, M2PCI_REG_WR_TXDMA_CNF_L, M2PCI_REG_WR_SYNC_CTRL, M2PCI_REG_RD_TXDMA_STAT,
                                    0, 0, &d->tx, &hwchs);
        if (res) {
            return res;
        }
        *out_handle = d->tx;

        res = xsdr_hwchans_cnt(&d->xdev, false, hwchs);
    }

    return res;
}

static
int usdr_device_m2_lm7_1_unregister_stream(device_t* dev, stream_handle_t* stream)
{
    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)dev;
    if (stream == d->tx) {
        xsdr_rfic_streaming_down(&d->xdev, RFIC_LMS7_TX);

        d->tx->ops->destroy(d->tx);
        d->tx = NULL;
    } else if (stream == d->rx) {
        xsdr_rfic_streaming_down(&d->xdev, RFIC_LMS7_RX);

        d->rx->ops->destroy(d->rx);
        d->rx = NULL;
    } else {
        return -EINVAL;
    }
    return 0;
}


static
int usdr_device_m2_lm7_1_create(lldev_t dev, device_id_t devid)
{
    int res;
    unsigned hwid;

    struct dev_m2_lm7_1_gps *d = (struct dev_m2_lm7_1_gps *)malloc(sizeof(struct dev_m2_lm7_1_gps));
    res = xsdr_ctor(dev, &d->xdev);
    if (res){
        goto failed_free;
    }

    res = usdr_device_base_create(&d->base, dev);
    if (res) {
        goto failed_free;
    }

    res = dev_gpi_get32(dev, IGPI_HWID, &hwid);
    //if (res) {
    //    goto failed_free;
    //}
    unsigned did = ((hwid >> 16) & 0xff);

    if ((res == 0) && (did == SSDR_DEV || did == SSDRPRO_DEV)) {
        res = vfs_add_const_i64_vec(&d->base.rootfs,
                                    s_params_m2_lm7_1_rev_advanced,
                                    SIZEOF_ARRAY(s_params_m2_lm7_1_rev_advanced));
    } else {
        res = vfs_add_const_i64_vec(&d->base.rootfs,
                                    s_params_m2_lm7_1_rev_generic,
                                    SIZEOF_ARRAY(s_params_m2_lm7_1_rev_generic));
    }
    if (res)
        goto failed_tree_creation;

    res = vfs_add_const_i64_vec(&d->base.rootfs,
                                s_params_m2_lm7_1_rev000,
                                SIZEOF_ARRAY(s_params_m2_lm7_1_rev000));
    if (res)
        goto failed_tree_creation;

    res = usdr_vfs_obj_param_init_array(&d->base,
                                        s_fparams_m2_lm7_1_rev000,
                                        SIZEOF_ARRAY(s_fparams_m2_lm7_1_rev000));
    if (res)
        goto failed_tree_creation;

    d->base.initialize = &usdr_device_m2_lm7_1_initialize;
    d->base.destroy = &usdr_device_m2_lm7_1_destroy;
    d->base.create_stream = &usdr_device_m2_lm7_1_create_stream;
    d->base.unregister_stream = &usdr_device_m2_lm7_1_unregister_stream;
    d->base.timer_op = &sfetrx4_stream_sync;
    d->rx = NULL;
    d->tx = NULL;

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
    usdr_device_m2_lm7_1_create,
};

int usdr_device_register_m2_lm7_1()
{
    return usdr_device_register(M2_LM7_1_DEVICE_ID_C, &s_ops);
}
