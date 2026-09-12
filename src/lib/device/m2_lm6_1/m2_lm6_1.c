// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <unistd.h>
#include <usdr_logging.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <ctype.h>

#include "../device.h"
#include "../device_ids.h"
#include "../device_vfs.h"
#include "../device_names.h"
#include "../device_cores.h"
#include "../device_fe.h"

#include "usdr_ctrl.h"

#include "../ipblks/streams/stream_sfetrx4_dma32.h"
#include "../ipblks/streams/sfe_rx_4.h"
#include "../ipblks/streams/sfe_tx_4.h"

#include "../device/ext_exm2pe/board_exm2pe.h"
#include "../device/ext_pciefe/ext_pciefe.h"
#include "../device/ext_supersync/ext_supersync.h"
#include "../device/ext_simplesync/ext_simplesync.h"


//
// NOTE Freq set required for IO opeartion since it's automatically trigger POWERON for LMS
//
enum {
    I2C_BUS_SI5332A  = MAKE_LSOP_I2C_ADDR(0, 0, I2C_DEV_CLKGEN),
    I2C_BUS_TPS63811 = MAKE_LSOP_I2C_ADDR(0, 0, I2C_DEV_DCDCBOOST),

    I2C_BUS_FRONTEND = MAKE_LSOP_I2C_ADDR(0, 1, 0),
};

enum {
    SRF4_FIFOBSZ = 0x10000, // 64kB
};

static
const usdr_dev_param_constant_t s_params_m2_lm6_1_rev000[] = {
    { DNLL_SPI_COUNT, 1 },
    { DNLL_I2C_COUNT, 1 },
    { DNLL_SRX_COUNT, 1 },
    { DNLL_STX_COUNT, 1 },
    { DNLL_RFE_COUNT, 1 },
    { DNLL_TFE_COUNT, 0 },
    { DNLL_IDX_REGSP_COUNT, 1 },
    { DNLL_IRQ_COUNT, 8 }, //TODO fix segfault when int count < configured
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

    // data stream cores
    { "/ll/srx/0/core",    USDR_MAKE_COREID(USDR_CS_STREAM, USDR_SC_RXDMA_BRSTN) },
    { "/ll/srx/0/base",    M2PCI_REG_WR_RXDMA_CONFIRM},
    { "/ll/srx/0/cfg_base",VIRT_CFG_SFX_BASE },
    { "/ll/srx/0/irq",     M2PCI_INT_RX},
    { "/ll/srx/0/dmacap",  0x855 },
//    { "/ll/srx/0/rfe",     (uintptr_t)"/ll/rfe/0" },
    { "/ll/rfe/0/fifobsz", SRF4_FIFOBSZ },
    { "/ll/rfe/0/core",    USDR_MAKE_COREID(USDR_CS_FE, USDR_FC_BRSTN) },
    { "/ll/rfe/0/base",    CSR_RFE4_BASE /*VIRT_CFG_SFX_BASE + 256 */},

    { "/ll/stx/0/core",    USDR_MAKE_COREID(USDR_CS_STREAM, USDR_SC_TXDMA_OLD) },
    { "/ll/stx/0/base",    M2PCI_REG_WR_TXDMA_CNF_L},
    { "/ll/stx/0/cfg_base",VIRT_CFG_SFX_BASE + 512 },
    { "/ll/stx/0/irq",     M2PCI_INT_TX},
    { "/ll/stx/0/dmacap",  0x555 },

    { "/ll/qspi_flash/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_QSPI_FLASH_24_RW) },
    { "/ll/qspi_flash/base", M2PCI_REG_QSPI_FLASH },
    { "/ll/qspi_flash/master_off", 0x1C0000 },

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

//    { "/ll/sdr/0/rfic/0", (uintptr_t)"lms6002d" },
//    { "/ll/device/name",  (uintptr_t)"usdr"},

    { "/ll/sdr/max_hw_rx_chans",  1 },
    { "/ll/sdr/max_hw_tx_chans",  1 },

//    { "/ll/sdr/max_sw_rx_chans",  2 },
//    { "/ll/sdr/max_sw_tx_chans",  2 },

    { "/ll/poll_event/in",  M2PCI_INT_RX },
    { "/ll/poll_event/out", M2PCI_INT_TX },

    // Frontend interface
    { "/ll/fe/0/gpio_busno/0",  0},
    { "/ll/fe/0/uart_busno/0",  0},

    { "/ll/fe/0/spi_busno/0", -1},
    { "/ll/fe/0/i2c_busno/0", -1},
};

static const vfs_constant_str_t s_params_m2_lm6_1_rev000_s[] = {
    { "/ll/srx/0/rfe",    "/ll/rfe/0" },
    { "/ll/sdr/0/rfic/0", "lms6002d" },
    { "/ll/device/name",  "usdr"},
};

static int dev_m2_lm6_1_max_sw_rx_chans_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dev_m2_lm6_1_max_sw_tx_chans_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dev_m2_lm6_1_rate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_rate_m_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_debug_all_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dev_m2_lm6_1_pwren_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_rx_freq_lob_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_rx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_rx_freqbb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_freqbb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_rx_gain_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_gain_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_gain_vga1_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_gain_vga2_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_gainauto_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_rx_bandwidth_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_bandwidth_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_tx_phgaincorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_rx_phgaincorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_rx_gainpga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_rx_gainvga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_rx_gainvga2a_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_rx_gainvga2b_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_rx_gainlna_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_rx_gainauto_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_rx_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_dc_calib(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_rx_dccorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_rx_ip2corr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_dccorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_rx_dc_meas_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dev_m2_lm6_1_sdr_rx_tia_cfb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_rx_tia_rfb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_tx_antennat_port_cfg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_refclk_frequency_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_refclk_frequency_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dev_m2_lm6_1_sdr_refclk_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_debug_lms6002d_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_debug_lms6002d_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dev_m2_lm6_1_debug_si5332_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_debug_si5332_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dev_m2_lm6_1_debug_tps6381x_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_debug_tps6381x_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dev_m2_lm6_1_sdr_atcrbs_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_atcrbs_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* value);

static int dev_m2_lm6_1_sdr_senstemp_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue);

static int dev_m2_lm6_1_sdr_vctcxo_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_clkmeas_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_clkmeas_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue);

static int dev_m2_lm6_1_sdr_revision_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue);
static int dev_m2_lm6_1_sdr_rfe_throttle_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_dccorr_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue);
static int dev_m2_lm6_1_sdr_tfe_gen_const_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int usdr_device_m2_lm6_1_calibrate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int usdr_device_m2_lm6_1_calibrate_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue);

static int dev_m2_lm6_1_sdr_rx_dccorrmode_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static
const usdr_dev_param_func_t s_fparams_m2_lm6_1_rev000[] = {
    { "/ll/sdr/max_sw_rx_chans",  { NULL, dev_m2_lm6_1_max_sw_rx_chans_get } },
    { "/ll/sdr/max_sw_tx_chans",  { NULL, dev_m2_lm6_1_max_sw_tx_chans_get } },

    { "/dm/rate/master",        { dev_m2_lm6_1_rate_set, NULL }},
    { "/dm/rate/rxtxadcdac",    { dev_m2_lm6_1_rate_m_set, NULL }},

    { "/dm/debug/all",          { NULL, dev_m2_lm6_1_debug_all_get }},
    { "/dm/power/en",           { dev_m2_lm6_1_pwren_set, NULL }},

    { "/dm/sdr/channels",       { NULL, NULL }},
    { "/dm/sensor/temp",        { NULL, dev_m2_lm6_1_sdr_senstemp_get }},

    { "/dm/sdr/refclk/frequency", {dev_m2_lm6_1_sdr_refclk_frequency_set, dev_m2_lm6_1_sdr_refclk_frequency_get}},
    { "/dm/sdr/refclk/path",      {dev_m2_lm6_1_sdr_refclk_path_set, NULL}},

    { "/dm/sdr/0/calibrate",      { usdr_device_m2_lm6_1_calibrate_set, usdr_device_m2_lm6_1_calibrate_get }},

    { "/dm/sdr/0/rx/tia/cfb",   { dev_m2_lm6_1_sdr_rx_tia_cfb_set, NULL }},
    { "/dm/sdr/0/rx/tia/rfb",   { dev_m2_lm6_1_sdr_rx_tia_rfb_set, NULL }},

    { "/dm/sdr/0/rx/dc/meas",   { NULL, dev_m2_lm6_1_sdr_rx_dc_meas_get }},
    { "/dm/sdr/0/rx/dccorr",    { dev_m2_lm6_1_sdr_rx_dccorr_set, dev_m2_lm6_1_sdr_dccorr_get }},
    { "/dm/sdr/0/rx/ip2corr",   { dev_m2_lm6_1_sdr_rx_ip2corr_set, NULL }},
    { "/dm/sdr/0/tx/dccorr",    { dev_m2_lm6_1_sdr_tx_dccorr_set, NULL }},

    { "/dm/sdr/0/calibrate_dc", { dev_m2_lm6_1_sdr_dc_calib, NULL }},

    { "/dm/sdr/0/rx/frequency/lob",{ dev_m2_lm6_1_sdr_rx_freq_lob_set, NULL }},
    { "/dm/sdr/0/rx/frequency",  { dev_m2_lm6_1_sdr_rx_freq_set, NULL }},
    { "/dm/sdr/0/tx/frequency",  { dev_m2_lm6_1_sdr_tx_freq_set, NULL }},
    { "/dm/sdr/0/rx/frequency/bb", { dev_m2_lm6_1_sdr_rx_freqbb_set, NULL }},
    { "/dm/sdr/0/tx/frequency/bb", { dev_m2_lm6_1_sdr_tx_freqbb_set, NULL }},

    /* TODO: delete block below after several releases, these are just aliases to above due typo for compatibility with old code */
    { "/dm/sdr/0/rx/freqency/lob",{ dev_m2_lm6_1_sdr_rx_freq_lob_set, NULL }},
    { "/dm/sdr/0/rx/freqency",  { dev_m2_lm6_1_sdr_rx_freq_set, NULL }},
    { "/dm/sdr/0/tx/freqency",  { dev_m2_lm6_1_sdr_tx_freq_set, NULL }},

    { "/dm/sdr/0/rx/gain",      { dev_m2_lm6_1_sdr_rx_gain_set, NULL }},
    { "/dm/sdr/0/tx/gain",      { dev_m2_lm6_1_sdr_tx_gain_set, NULL }},
    { "/dm/sdr/0/tx/gain/vga1", { dev_m2_lm6_1_sdr_tx_gain_vga1_set, NULL }},
    { "/dm/sdr/0/tx/gain/vga2", { dev_m2_lm6_1_sdr_tx_gain_vga2_set, NULL }},
    { "/dm/sdr/0/rx/gain/pga",  { dev_m2_lm6_1_sdr_rx_gainpga_set, NULL }},
    { "/dm/sdr/0/rx/gain/vga",  { dev_m2_lm6_1_sdr_rx_gainvga_set, NULL }},
    { "/dm/sdr/0/rx/gain/vga2a",{ dev_m2_lm6_1_sdr_rx_gainvga2a_set, NULL }},
    { "/dm/sdr/0/rx/gain/vga2b",{ dev_m2_lm6_1_sdr_rx_gainvga2b_set, NULL }},
    { "/dm/sdr/0/rx/gain/lna",  { dev_m2_lm6_1_sdr_rx_gainlna_set, NULL }},

    { "/dm/sdr/0/rx/gain/auto",  { dev_m2_lm6_1_sdr_rx_gainauto_set, NULL }},
    { "/dm/sdr/0/tx/gain/auto",  { dev_m2_lm6_1_sdr_tx_gainauto_set, NULL }},

    { "/dm/sdr/0/rx/path",      { dev_m2_lm6_1_sdr_rx_path_set, NULL }},
    { "/dm/sdr/0/tx/path",      { dev_m2_lm6_1_sdr_tx_path_set, NULL }},

    { "/dm/sdr/0/rx/dccorrmode",  { dev_m2_lm6_1_sdr_rx_dccorrmode_set, NULL }},

    { "/dm/sdr/0/rx/bandwidth", { dev_m2_lm6_1_sdr_rx_bandwidth_set, NULL }},
    { "/dm/sdr/0/tx/bandwidth", { dev_m2_lm6_1_sdr_tx_bandwidth_set, NULL }},

    { "/dm/sdr/0/tx/phgaincorr", {  dev_m2_lm6_1_sdr_tx_phgaincorr_set, NULL }},
    { "/dm/sdr/0/rx/phgaincorr", {  dev_m2_lm6_1_sdr_rx_phgaincorr_set, NULL }},

    { "/dm/sdr/0/tfe/generator/const",   { dev_m2_lm6_1_sdr_tfe_gen_const_set, NULL }},

    { "/debug/hw/lms6002d/0/reg",  { dev_m2_lm6_1_debug_lms6002d_reg_set, dev_m2_lm6_1_debug_lms6002d_reg_get }},
    { "/debug/hw/si5332/0/reg",    { dev_m2_lm6_1_debug_si5332_reg_set, dev_m2_lm6_1_debug_si5332_reg_get }},
    { "/debug/hw/tps6381x/0/reg",  { dev_m2_lm6_1_debug_tps6381x_reg_set, dev_m2_lm6_1_debug_tps6381x_reg_get }},

    { "/dm/sdr/0/tfe/antcfg",     { dev_m2_lm6_1_sdr_tx_antennat_port_cfg_set, NULL }},

    { "/dm/sdr/0/core/atcrbs/reg", { dev_m2_lm6_1_sdr_atcrbs_set, dev_m2_lm6_1_sdr_atcrbs_get }},

    { "/dm/sdr/0/dac_vctcxo",      { dev_m2_lm6_1_sdr_vctcxo_set, NULL }},
    { "/dm/sdr/0/clkmeas", { dev_m2_lm6_1_sdr_clkmeas_set, dev_m2_lm6_1_sdr_clkmeas_get }},

    { "/dm/revision", { NULL, dev_m2_lm6_1_sdr_revision_get }},
    { "/dm/sdr/0/rfe/throttle", { dev_m2_lm6_1_sdr_rfe_throttle_set, NULL }},
};

static const usdr_dev_link_t s_links[] = {
    { "/dm/sdr/0/rx/frequency/0",    "/dm/sdr/0/rx/frequency" },
    { "/dm/sdr/0/rx/frequency/1",    "/dm/sdr/0/rx/frequency" },
    { "/dm/sdr/0/tx/frequency/0",    "/dm/sdr/0/tx/frequency" },
    { "/dm/sdr/0/tx/frequency/1",    "/dm/sdr/0/tx/frequency" },
    { "/dm/sdr/0/rx/frequency/bb/0", "/dm/sdr/0/rx/frequency/bb" },
    { "/dm/sdr/0/rx/frequency/bb/1", "/dm/sdr/0/rx/frequency/bb" },
    { "/dm/sdr/0/tx/frequency/bb/0", "/dm/sdr/0/tx/frequency/bb" },
    { "/dm/sdr/0/tx/frequency/bb/1", "/dm/sdr/0/tx/frequency/bb" },

    { "/dm/sdr/0/rx/gain/0",      "/dm/sdr/0/rx/gain" },
    { "/dm/sdr/0/tx/gain/0",      "/dm/sdr/0/tx/gain" },
 //   { "/dm/sdr/0/tx/gain/lb/0",   "/dm/sdr/0/tx/gain/lb" },
    { "/dm/sdr/0/tx/gain/vga1/0", "/dm/sdr/0/tx/gain/vga1" },
    { "/dm/sdr/0/tx/gain/vga2/0", "/dm/sdr/0/tx/gain/vga2" },
    { "/dm/sdr/0/rx/gain/pga/0",  "/dm/sdr/0/rx/gain/pga" },
    { "/dm/sdr/0/rx/gain/vga/0",  "/dm/sdr/0/rx/gain/vga" },
    { "/dm/sdr/0/rx/gain/vga2a/0",  "/dm/sdr/0/rx/gain/vga2a" },
    { "/dm/sdr/0/rx/gain/vga2b/0",  "/dm/sdr/0/rx/gain/vga2b" },
    { "/dm/sdr/0/rx/gain/lna/0",  "/dm/sdr/0/rx/gain/lna" },
 //   { "/dm/sdr/0/rx/gain/lb/0",   "/dm/sdr/0/rx/gain/lb" },
    { "/dm/sdr/0/rx/gain/1",      "/dm/sdr/0/rx/gain" },
    { "/dm/sdr/0/tx/gain/1",      "/dm/sdr/0/tx/gain" },
 //   { "/dm/sdr/0/tx/gain/lb/1",   "/dm/sdr/0/tx/gain/lb" },
    { "/dm/sdr/0/tx/gain/vga1/1", "/dm/sdr/0/tx/gain/vga1" },
    { "/dm/sdr/0/tx/gain/vga2/1", "/dm/sdr/0/tx/gain/vga2" },
    { "/dm/sdr/0/rx/gain/pga/1",  "/dm/sdr/0/rx/gain/pga" },
    { "/dm/sdr/0/rx/gain/vga/1",  "/dm/sdr/0/rx/gain/vga" },
    { "/dm/sdr/0/rx/gain/vga2a/1",  "/dm/sdr/0/rx/gain/vga2a" },
    { "/dm/sdr/0/rx/gain/vga2b/1",  "/dm/sdr/0/rx/gain/vga2b" },
    { "/dm/sdr/0/rx/gain/lna/1",  "/dm/sdr/0/rx/gain/lna" },
 //   { "/dm/sdr/0/rx/gain/lb/1",   "/dm/sdr/0/rx/gain/lb" },

    { "/dm/sdr/0/rx/bandwidth/0", "/dm/sdr/0/rx/bandwidth" },
    { "/dm/sdr/0/tx/bandwidth/0", "/dm/sdr/0/tx/bandwidth" },
    { "/dm/sdr/0/rx/bandwidth/1", "/dm/sdr/0/rx/bandwidth" },
    { "/dm/sdr/0/tx/bandwidth/1", "/dm/sdr/0/tx/bandwidth" },

    { "/dm/sdr/0/rx/path/0",      "/dm/sdr/0/rx/path" },
    { "/dm/sdr/0/rx/path/1",      "/dm/sdr/0/rx/path" },
    { "/dm/sdr/0/tx/path/0",      "/dm/sdr/0/tx/path" },
    { "/dm/sdr/0/tx/path/1",      "/dm/sdr/0/tx/path" },

    { "/dm/sdr/0/rx/dccorr/0",    "/dm/sdr/0/rx/dccorr" },
    { "/dm/sdr/0/tx/dccorr/0",    "/dm/sdr/0/tx/dccorr" },
    { "/dm/sdr/0/rx/phgaincorr/0","/dm/sdr/0/rx/phgaincorr" },
    { "/dm/sdr/0/tx/phgaincorr/0","/dm/sdr/0/tx/phgaincorr" },
    { "/dm/sdr/0/rx/dccorr/1",    "/dm/sdr/0/rx/dccorr" },
    { "/dm/sdr/0/tx/dccorr/1",    "/dm/sdr/0/tx/dccorr" },
    { "/dm/sdr/0/rx/phgaincorr/1","/dm/sdr/0/rx/phgaincorr" },
    { "/dm/sdr/0/tx/phgaincorr/1","/dm/sdr/0/tx/phgaincorr" },

    { "/dm/sdr/0/tfe/generator/const/0", "/dm/sdr/0/tfe/generator/const" },
    { "/dm/sdr/0/tfe/generator/const/1", "/dm/sdr/0/tfe/generator/const" },

};


struct dev_m2_lm6_1 {
    device_t base;

    struct usdr_dev d;
    struct dev_fe* fe;

    uint32_t debug_lms6002d_last;
    uint32_t debug_si5332_last;
    uint32_t debug_tps6381x_last;
    uint32_t debug_lp8758_last;

    int cal_data[8];

    stream_handle_t* rx;
    stream_handle_t* tx;
};

struct param_list_idx {
    const char* name;
    unsigned param;
};
typedef struct param_list_idx param_list_idx_t;

// static const param_list_idx_t s_rx_path_list[] = {
//     { "rxl", USDR_RX_EXT },
//     { "rxw", USDR_RX_W },
//     { "rxh", USDR_RX_H },
//     { "auto", USDR_RX_AUTO },
//     { "rx_auto", USDR_RX_AUTO },
// };

#if 0
static int find_param_list(const char* param, const param_list_idx_t* lst, unsigned size)
{
    for (unsigned i = 0; i < size; i++) {
        if (strcasecmp(lst[i].name, param) == 0) {
            return i;
        }
    }
    return -1;
}
#endif

static int _channel_info_string_parse(char* chanlist, unsigned max_chans, unsigned* cinfo)
{
    unsigned ch = 0;
    for (; *chanlist; chanlist++) {
        unsigned chn;
        if (isdigit(*chanlist)) {
            chn = atoi(chanlist);
        } else {
            return -ENAVAIL;
        }

        if (chn > max_chans) {
            USDR_LOG("STRM", USDR_LOG_ERROR, "Channel parsing: incorrect channel num: %d\n", chn);
            return -EINVAL;
        }

        ch |= 1 << chn;
    }

    *cinfo = ch;
    return 0;
}

static int _device_path_to_chmsk(const char* full_path, unsigned max_chs, unsigned* lms_ch)
{
    char chanlist[64*4];
    const char* lst;
    const char* pos = full_path;
    lst = NULL;

    while ((pos = strchr(pos, '/')) != NULL) {
        lst = ++pos;
    }
    if (lst == NULL) {
        return -ENAVAIL;
    }

    SAFE_STRCPY(chanlist, lst);
    return _channel_info_string_parse(chanlist, max_chs, lms_ch);
}

static int _iterate_ordinal_chans(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t val, bool rxchans)
{
    vfs_object_t ph;
    unsigned selected;
    const unsigned max_chs = 2;

    int res = _device_path_to_chmsk(obj->full_path, max_chs, &selected);
    if (res == -ENAVAIL) {
        selected = (1 << max_chs) - 1;
        res = 0;
    } else if (res != 0) {
        return res;
    }

    ph.type = obj->type;
    ph.object = obj->object;
    ph.data = obj->data;
    ph.ops = obj->ops;
    ph.full_path[0] = 0;
    ph.full_path[1] = selected;
    return obj->ops.si64(&ph, val);
}

static int dev_gpo_set(lldev_t dev, unsigned bank, unsigned data)
{
    return lowlevel_reg_wr32(dev, 0, 0, ((bank & 0x7f) << 24) | (data & 0xff));
}

static int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, 0, 16 + (bank / 4), data);
}

int dev_m2_lm6_1_max_sw_rx_chans_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    *ovalue = d->d.has_rxchain ? 2 : 1;
    return 0;
}

int dev_m2_lm6_1_max_sw_tx_chans_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    *ovalue = d->d.has_txchain ? 2 : 1;
    return 0;
}

int dev_m2_lm6_1_sdr_clkmeas_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return dev_gpo_set(ud->dev, 16, value);
}

int dev_m2_lm6_1_sdr_clkmeas_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue)
{
    uint32_t v = 0;
    int res = dev_gpi_get32(ud->dev, 24, &v);
    if (res)
        return res;

    *ovalue = v;
    return 0;
}

int dev_m2_lm6_1_sdr_dccorr_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    uint32_t v = 0;
    int res = dev_gpi_get32(ud->dev, 20, &v);
    if (res)
        return res;

    int16_t i, q;
    i = (v >> 0) & 0xffff;
    q = (v >> 16) & 0xffff;

    USDR_LOG("UDEV", USDR_LOG_WARNING, "%s: DC_AAVG I=%d Q=%d\n", lowlevel_get_devname(d->base.dev), i, q);

    *ovalue = v;

    usdr_rxdccorr(&d->d, ovalue);
    return 0;
}

int dev_m2_lm6_1_debug_lms6002d_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    int res;

    d->debug_lms6002d_last = ~0u;
    res = lowlevel_spi_tr32(d->base.dev, 0, 0, value & 0xffff, &d->debug_lms6002d_last);

    USDR_LOG("UDEV", USDR_LOG_WARNING, "%s: Debug LMS6 REG %04x => %04x\n",
             lowlevel_get_devname(d->base.dev), (unsigned)value,
             d->debug_lms6002d_last);
    return res;
}

int dev_m2_lm6_1_debug_lms6002d_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    *ovalue = d->debug_lms6002d_last;
    return 0;
}

int dev_m2_lm6_1_debug_si5332_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    uint8_t data[2] = { value >> 8, value};
    uint8_t out = ~0;
    bool wr = (value & 0x800000);
    int res = lowlevel_get_ops(d->base.dev)->ls_op(d->base.dev, 0,
                                                   USDR_LSOP_I2C_DEV, I2C_BUS_SI5332A,
                                                   wr ? 0 : 1, &out,
                                                   wr ? 2 : 1, data);

    USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug SI5322 REG %02x => %02x\n",
             lowlevel_get_devname(d->base.dev), (unsigned)value, out);

    d->debug_si5332_last = out;
    return res;
}

int dev_m2_lm6_1_debug_si5332_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    *ovalue = d->debug_si5332_last;
    return 0;
}

int dev_m2_lm6_1_debug_tps6381x_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    uint8_t data[2] = { value >> 8, value};
    uint8_t out = ~0;
    bool wr = (value & 0x8000);
    int res = lowlevel_get_ops(d->base.dev)->ls_op(d->base.dev, 0,
                                                   USDR_LSOP_I2C_DEV, I2C_BUS_TPS63811,
                                                   wr ? 0 : 1, &out,
                                                   wr ? 2 : 1, data);

    USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug TPS6381X REG %02x => %02x\n",
             lowlevel_get_devname(d->base.dev), (unsigned)value, out);

    d->debug_tps6381x_last = out;
    return res;
}

int dev_m2_lm6_1_debug_tps6381x_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    *ovalue = d->debug_tps6381x_last;
    return 0;
}


int dev_m2_lm6_1_sdr_senstemp_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    int temp, res;

    res = usdr_gettemp(&d->d, &temp);
    *ovalue = (int64_t)temp;
    return res;
}

int dev_m2_lm6_1_sdr_dc_calib(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    //return -EINVAL;
    return usdr_calib_dc(&d->d, true);
}

int dev_m2_lm6_1_sdr_tx_antennat_port_cfg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    // mockup
    return 0;
}

int dev_m2_lm6_1_sdr_refclk_frequency_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    d->d.fref = value;
    d->d.lms.fref = d->d.fref;
    return 0;
}

int dev_m2_lm6_1_sdr_refclk_frequency_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    *ovalue = d->d.fref;
    return 0;
}

int dev_m2_lm6_1_sdr_atcrbs_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    int res;

    res = lowlevel_reg_wr32(d->base.dev, 0, M2PCI_REG_WR_LBDSP, value);
    return res;
}

int dev_m2_lm6_1_sdr_atcrbs_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    int res;
    uint32_t value32 = 0;

    res = lowlevel_reg_rd32(d->base.dev, 0, M2PCI_REG_RD_LBDSP, &value32);
    *value = value32;
    return res;
}

int dev_m2_lm6_1_sdr_refclk_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
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

    d->d.refclkpath = value;
    int res = usdr_set_extref(&d->d, value == 1, d->d.fref);

    if(res)
        USDR_LOG("UDEV", USDR_LOG_ERROR, "LM6: error setting clk ref path to %d: err=%d\n", (unsigned)value, res);
    else
        USDR_LOG("UDEV", USDR_LOG_INFO, "LM6: set clk ref path to %d\n", (unsigned)value);

    return res;
}

enum {
    DECIM_INTER_MAX = 32,
};
enum {
    RATE_MIN =  1000000,
    RATE_MAX = 80000000,
};

int dev_m2_lm6_1_rate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    unsigned rate_min = (d->d.has_rxchain && d->d.has_txchain) ? RATE_MIN / DECIM_INTER_MAX : RATE_MIN;
    if (value < rate_min || value > RATE_MAX)
        return -ERANGE;

    return usdr_set_samplerate_ex(&d->d, value, value, 0, 0, 0);
}

int dev_m2_lm6_1_rate_m_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    unsigned rate_min = (d->d.has_rxchain && d->d.has_txchain) ? RATE_MIN / DECIM_INTER_MAX : RATE_MIN;

    uint32_t *rates = (uint32_t *)(uintptr_t)value;

    uint32_t rx_rate = rates[0];
    uint32_t tx_rate = rates[1];

    uint32_t adc_rate = rates[2];
    uint32_t dac_rate = rates[3];

    if (rx_rate == 0 && tx_rate == 0)
        return -EINVAL;

    if ((rx_rate != 0) && (rx_rate < rate_min || rx_rate > RATE_MAX))
        return -ERANGE;

    if ((tx_rate != 0) && (tx_rate < rate_min || tx_rate > RATE_MAX))
        return -ERANGE;

    return usdr_set_samplerate_ex(&d->d, rx_rate, tx_rate, adc_rate, dac_rate, 0);
}


int dev_m2_lm6_1_debug_all_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    union {
        uint32_t i32[4];
        uint64_t i64[2];
    } data;

    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
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

int dev_m2_lm6_1_pwren_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return 0;
}

int dev_m2_lm6_1_sdr_rx_freq_lob_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_set_lob_freq(&d->d, value);
}

int dev_m2_lm6_1_sdr_rx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    if (obj->full_path[0]) {
        return _iterate_ordinal_chans(ud, obj, value, true);
    }

    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_fe_set_freq(&d->d, FE_FREQ_LO_RX, obj->full_path[1], value, NULL);
}
int dev_m2_lm6_1_sdr_tx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    if (obj->full_path[0]) {
        return _iterate_ordinal_chans(ud, obj, value, false);
    }

    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_fe_set_freq(&d->d, FE_FREQ_LO_TX, obj->full_path[1], value, NULL);
}

int dev_m2_lm6_1_sdr_rx_freqbb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    if (obj->full_path[0]) {
        return _iterate_ordinal_chans(ud, obj, value, true);
    }

    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_fe_set_freq(&d->d, FE_FREQ_BB_RX, obj->full_path[1], (int64_t)value, NULL);
}

int dev_m2_lm6_1_sdr_tx_freqbb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    if (obj->full_path[0]) {
        return _iterate_ordinal_chans(ud, obj, value, false);
    }

    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_fe_set_freq(&d->d, FE_FREQ_BB_TX, obj->full_path[1], (int64_t)value, NULL);
}

int dev_m2_lm6_1_sdr_rx_gain_set(pdevice_t ud, UNUSED pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_set_gain(&d->d, GAIN_RX_VGA1, value, NULL);
}

int dev_m2_lm6_1_sdr_tx_gainauto_set(pdevice_t ud, UNUSED pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_set_gain(&d->d, GAIN_TX_AUTO, value, NULL);
}

int dev_m2_lm6_1_sdr_tx_gain_set(pdevice_t ud, UNUSED pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_set_gain(&d->d, GAIN_TX_VGA2, value, NULL);
}
int dev_m2_lm6_1_sdr_tx_gain_vga1_set(pdevice_t ud, UNUSED pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1*)ud;
    return usdr_rfic_set_gain(&d->d, GAIN_TX_VGA1, value, NULL);
}
int dev_m2_lm6_1_sdr_tx_gain_vga2_set(pdevice_t ud, UNUSED pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_set_gain(&d->d, GAIN_TX_VGA2, value, NULL);
}

int dev_m2_lm6_1_sdr_rx_dccorrmode_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_set_rxdccorr(&d->d, value & 1);
}

int dev_m2_lm6_1_sdr_rx_bandwidth_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_bb_set_badwidth(&d->d, false, value, NULL);
}
int dev_m2_lm6_1_sdr_tx_bandwidth_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_bb_set_badwidth(&d->d, true, value, NULL);
}
int dev_m2_lm6_1_sdr_rx_gainpga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_set_gain(&d->d, GAIN_RX_VGA2, value * 2, NULL);
}

int dev_m2_lm6_1_sdr_rx_gainvga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_set_gain(&d->d, GAIN_RX_VGA1, value, NULL);
}

int dev_m2_lm6_1_sdr_rx_gainvga2a_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_set_gain(&d->d, GAIN_RX_VGA2A, value, NULL);
}

int dev_m2_lm6_1_sdr_rx_gainvga2b_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_set_gain(&d->d, GAIN_RX_VGA2B, value, NULL);
}

int dev_m2_lm6_1_sdr_rx_gainlna_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_set_gain(&d->d, GAIN_RX_LNA, value, NULL);
}

int dev_m2_lm6_1_sdr_rx_gainauto_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_set_gain(&d->d, GAIN_RX_AUTO, value, NULL);
}

int dev_m2_lm6_1_sdr_rx_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    bool rflb = false;
    // 00 – All output buffers powered down;
    // 01 – First buffer enabled for LNA1 path (default);   WB:  250 - 2700
    // 10 – Second buffer enabledfor LNA2 path;             HB: 2700 - 3800
    // 11 – Third buffer enabledfor LNA3 path               LB: external / MIXER
    if (value > 4096) {
        const char* param = (const char*)value;
        if (strcasecmp("rxl", param) == 0) {
            value = (uintptr_t)"EXT";
        } else if (strcasecmp("rxw", param) == 0) {
            value = (uintptr_t)"W";
        } else if (strcasecmp("rxh", param) == 0) {
            value = (uintptr_t)"H";
        } else if (strcasecmp("rxl_lb", param) == 0) {
            value = (uintptr_t)"EXT"; rflb = true;
        } else if (strcasecmp("rxw_lb", param) == 0) {
            value = (uintptr_t)"W"; rflb = true;
        } else if (strcasecmp("rxh_lb", param) == 0) {
            value = (uintptr_t)"H"; rflb = true;
        }
    } else {
        return -EINVAL;
    }

    return usdr_rfic_fe_set_rxlna(&d->d, (const char *)(uintptr_t)value, rflb);
}

int dev_m2_lm6_1_sdr_tx_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;

    if (value > 4096) {
        const char* param = (const char*)value;
        if (strcasecmp("txw", param) == 0) {
            value = (uintptr_t)"W";
        } else if (strcasecmp("txh", param) == 0) {
            value = (uintptr_t)"H";
        }
    }

    return usdr_rfic_fe_set_txlna(&d->d, (const char *)(uintptr_t)value);
}

int dev_m2_lm6_1_sdr_rx_dccorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    int res;
    //unsigned chan = (unsigned)(value >> 32);
    unsigned vi = (value >> 16) & 0xffff;
    unsigned vq = (value >> 0) & 0xffff;

    res = lms6002d_set_rxfedc(&d->d.lms, vi, vq);
    return res;
}

int dev_m2_lm6_1_sdr_rx_ip2corr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    int res;
    //unsigned chan = (unsigned)(value >> 32);
    unsigned vi = (value >> 16) & 0xffff;
    unsigned vq = (value >> 0) & 0xffff;

    res = lms6002d_set_rxfe_ip2corr(&d->d.lms, vi, vq);
    return res;
}

int dev_m2_lm6_1_sdr_tx_dccorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;

    unsigned vi = (value >> 16) & 0xffff;
    unsigned vq = (value >> 0) & 0xffff;

    return usdr_tx_dccorr(&d->d, vi, vq);
}

int dev_m2_lm6_1_sdr_rx_tia_cfb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
#if 0
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return lms6002d_set_tia_cfb(&d->lms, value);
#endif
    return -EINVAL;
}

int dev_m2_lm6_1_sdr_rx_tia_rfb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
#if 0
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return lms6002d_set_tia_rfb(&d->lms, value);
#endif
    return -EINVAL;
}

int dev_m2_lm6_1_sdr_rx_dc_meas_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
#if 0
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    int res;
    uint32_t vi, vq;

    res = dev_gpi_get32(d->base.dev, IGPI_RX_I, &vi);
    if (res)
        return res;
    res = dev_gpi_get32(d->base.dev, IGPI_RX_Q, &vq);
    if (res)
        return res;

    uint64_t v = (( (uint64_t)vi ) << 32 ) | vq;
    *ovalue = v;
    return 0;
#endif
    *ovalue = 0;
    return 0;
}

static int _phgaincorr_set(struct dev_m2_lm6_1 *d, bool tx, uint64_t value)
{
    unsigned ig = (value & 0xffff);
    unsigned qg = ((value >> 16) & 0xffff);
    int32_t pcorr = (int16_t)((value >> 48) & 0xffff);
    int amp_imb;
    if (ig < 2047) {
        amp_imb = (2047 - ig) * 8;
    } else {
        amp_imb = - (2047 - qg) * 8;
    }
    pcorr *= 16;
    USDR_LL_LOG(d->base.dev, "UDEV", USDR_LOG_WARNING, "%cXGAC I=%d Q=%d A=%d => AMP_IMB=%d\n",
                tx ? 'T' : 'R', ig, qg, pcorr, amp_imb);

    return tx ? usdr_tx_iqimb_set(&d->d, amp_imb, pcorr) : usdr_rx_iqimb_set(&d->d, amp_imb, pcorr);
}

int dev_m2_lm6_1_sdr_tx_phgaincorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
#if 0
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;

    unsigned ig = (value & 0xffff);
    unsigned qg = ((value >> 16) & 0xffff);
    int16_t pcorr = (int16_t)((value >> 48) & 0xffff);
    int amp_imb;
    if (ig < 2047) {
        amp_imb = (2047 - ig) * 8;
    } else {
        amp_imb = - (2047 - qg) * 8;
    }
    pcorr *= 16;
    USDR_LL_LOG(d->base.dev, "UDEV", USDR_LOG_WARNING, "TXGAC I=%d Q=%d A=%d => AMP_IMB=%d\n", ig, qg, pcorr, amp_imb);

    return usdr_tx_iqimb_set(&d->d, amp_imb, pcorr);
#endif
    return _phgaincorr_set((struct dev_m2_lm6_1 *)ud, true, value);
}

int dev_m2_lm6_1_sdr_rx_phgaincorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return _phgaincorr_set((struct dev_m2_lm6_1 *)ud, false, value);
}


int dev_m2_lm6_1_sdr_revision_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    unsigned rev_lo, rev_hi;
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
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

int dev_m2_lm6_1_sdr_rfe_throttle_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    if (d->rx) {
        return d->rx->ops->option_set(d->rx, "throttle", value);
    }
    return -EINVAL;
}

static
void usdr_device_m2_lm6_1_destroy(pdevice_t udev)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)udev;

    // Destroy streams
    if (d->rx) {
        d->rx->ops->destroy(d->rx);
    }
    if (d->tx) {
        d->tx->ops->destroy(d->tx);
    }

    usdr_dtor(&d->d);

    USDR_LOG("UDEV", USDR_LOG_INFO, "M2_LM6: turnoff\n");
    usdr_device_base_destroy(udev);
}

usdr_dev_t* get_usdr_dev(pdevice_t udev)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)udev;
    return &d->d;
}

int dev_m2_lm6_1_sdr_vctcxo_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    board_ext_pciefe_t* board_fe = (board_ext_pciefe_t*)device_fe_to(d->fe, "pciefe");
    board_exm2pe_t* board = (board_exm2pe_t*)device_fe_to(d->fe, "exm2pe");
    if (board_fe) {
        return board_ext_pciefe_set_dac(board_fe, value);
    } else if (board) {
        return board_exm2pe_set_dac(board, value);
    }

    return -EINVAL;
}


static
int usdr_device_m2_lm6_1_initialize(pdevice_t udev, unsigned pcount, const char** devparam, const char** devval)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)udev;
    lldev_t dev = d->base.dev;
    int res;
    int ext_clk = -1;
    unsigned ext_refclk = 0;
    const char* fe = NULL;

    for (unsigned i = 0; i < pcount; i++) {
        if (strcmp(devparam[i], "extclk") == 0) {
            const char* val = devval[i];
            if (*val == '1' || *val == 'o') {
                ext_clk = 1;
            } else {
                ext_clk = 0;
            }
        } else if (strcmp(devparam[i], "extref") == 0) {
            ext_refclk = atoi(devval[i]);
        } else if (strcmp(devparam[i], "fe") == 0) {
            fe = devval[i];
        }
    }

    res = usdr_ctor(dev, 0, &d->d);
    if (res)
        return res;

    res = usdr_init(&d->d, ext_clk, ext_refclk);
    if (res)
        return res;


    // Init FE
    res = device_fe_probe(udev, "m2a+e", fe, I2C_BUS_FRONTEND, &d->fe);
    if (res == -ENODEV) {
        // Ignore no front end was found error
        res = 0;
    }

    return res;
}

int dev_m2_lm6_1_sdr_tfe_gen_const_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    bool normal = (value == UINT64_MAX);
    int16_t vi = (int16_t)((value >> 16) & 0xffff);
    int16_t vq = (int16_t)((value >> 0) & 0xffff);

    return usdr_tx_gen_set(&d->d, !normal, 0x3, vi, vq);
}

int usdr_device_m2_lm6_1_calibrate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    int res;
    unsigned flags = value & 0xfffff;
    unsigned chan = value >> 32;

    if (flags > 2*65536 || chan > 1) {
        const char* v = (const char* )value;
        chan = 0; // TODO B
        flags = 0;

        if (strncmp(v, "e", 1) == 0) {
            v += 1;
            flags |= USDR_CAL_EXT_FB;
        }

        if (strncmp(v, "a:", 2) == 0) {
            v += 2;
        } if (strncmp(v, "b:", 2) == 0) {
            v += 2;
            chan = 1;
        }

        if (strcmp(v, "txlo") == 0) {
            flags |= USDR_CAL_TXLO;
        } else if (strcmp(v, "rxlo") == 0) {
            flags |= USDR_CAL_RXLO;
        } else if (strcmp(v, "txiqimb") == 0) {
            flags |= USDR_CAL_TXIQIMB;
        } else if (strcmp(v, "rxiqimb") == 0) {
            flags |= USDR_CAL_RXIQIMB;
        } else if (strcmp(v, "tx") == 0) {
            flags |= USDR_CAL_TXLO | USDR_CAL_TXIQIMB;
        } else if (strcmp(v, "rx") == 0) {
            flags |= USDR_CAL_RXLO | USDR_CAL_RXIQIMB;
        } else if (strcmp(v, "all") == 0) {
            flags |= USDR_CAL_TXLO | USDR_CAL_TXIQIMB | USDR_CAL_RXLO | USDR_CAL_RXIQIMB;
        } else if (strcmp(v, "lo") == 0) {
            flags |= USDR_CAL_TXLO | USDR_CAL_RXLO;
        } else {
            return -EINVAL;
        }
    }

    res = usdr_calibrate(&d->d, chan, flags, &d->cal_data[0]);
    return res;
}

int usdr_device_m2_lm6_1_calibrate_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    *value = (intptr_t)&d->cal_data[0];
    return 0;
}

static const channel_map_info_t s_usdr_chmap[] = {
    { "ai", 0 },
    { "aq", 1 },
    { "i", 0 },
    { "q", 1 },
    { "a", 0 },
    { NULL, CH_NULL },
};


int usdr_map_channels(const usdr_channel_info_t* channels, channel_info_t* core_chans)
{
    return usdr_channel_info_map_default(channels, s_usdr_chmap, 2, core_chans);
}

static
int usdr_device_m2_lm6_1_create_stream(device_t* dev, const char* sid, const char* dformat,
                                              const usdr_channel_info_t* channels, unsigned pktsyms,
                                              unsigned flags, const char* parameters, stream_handle_t** out_handle)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)dev;
    int res = -EINVAL;
    unsigned chans;
    channel_info_t lchans;

    res = usdr_map_channels(channels, &lchans);
    if (res) {
        return res;
    }

    if (strstr(sid, "rx") != NULL) {
        if (d->rx) {
            return -EBUSY;
        }
        res = usdr_rfic_streaming_up(&d->d, RFIC_LMS6_RX);
        if (res) {
            return res;
        }

        if (d->d.rx_lo == 0) {
            res = usdr_rfic_fe_set_freq(&d->d, FE_FREQ_LO_RX, ~0U, 320e6, NULL);
            if (res) {
                return res;
            }

            d->d.rx_lo = 0;
        }

        res = usdr_calib_dc(&d->d, true);
        if (res) {
            return res;
        }
        if (d->d.tx_lo) {
            lms6002d_tune_pll(&d->d.lms, true, d->d.tx_lo);
        }

        for (unsigned i = 0; i < MAX_NCO_STREAMS; i++) {
            if (d->d.rx_raw.lo[i].set) {
                usdr_rfic_fe_set_freq(&d->d, FE_FREQ_BB_RX, 1 << i, d->d.rx_raw.lo[i].value, NULL);
            }
        }

        res = create_sfetrx4_stream(dev, CORE_SFERX_DMA32_R0, dformat, channels->count, &lchans, pktsyms,
                                    flags, M2PCI_REG_WR_RXDMA_CONFIRM, VIRT_CFG_SFX_BASE, 0,
                                    SRF4_FIFOBSZ, CSR_RFE4_BASE, &d->rx, &chans);
        if (res) {
            return res;
        }
        d->d.rx_lchans = chans;
        *out_handle = d->rx;

        // TODO: handle NCO changes
        res = res ? res : usdr_rxupdate_cal(&d->d);
    } else if (strstr(sid, "tx") != NULL) {
        bool extended_core = (d->d.hwid & (1 << (24 + 2))) ? true : false;

        if (d->tx) {
            return -EBUSY;
        }
        res = usdr_rfic_streaming_up(&d->d, RFIC_LMS6_TX);
        if (res) {
            return res;
        }

        if (extended_core) {
            res = res ? res : usdr_reset_txfex(&d->d);
        }

        res = res ? res : create_sfetrx4_stream(dev, extended_core ? CORE_EXFETX_DMA32_R0_2 : CORE_SFETX_DMA32_R0, dformat, channels->count, &lchans, pktsyms,
                            flags,
                            extended_core ? M2PCI_REG_WR_TXDMA_CFG0 : M2PCI_REG_WR_TXDMA_CNF_L,
                            M2PCI_REG_WR_SYNC_CTRL,
                            M2PCI_REG_RD_TXDMA_STAT,
                            0, CSR_TFE4_BASE, &d->tx, &chans);
        if (res) {
            return res;
        }
        d->d.tx_lchans = chans;
        *out_handle = d->tx;

        // TODO: handle NCO changes
        res = res ? res : usdr_tx_gen_set(&d->d, false, 0, 0, 0);
        res = res ? res : usdr_txupdate_cal(&d->d);
    }

    return res;
}

static
int usdr_device_m2_lm6_1_unregister_stream(device_t* dev, stream_handle_t* stream)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)dev;
    if (stream == d->tx) {
        usdr_rfic_streaming_down(&d->d, RFIC_LMS6_TX);
        d->tx->ops->destroy(d->tx);
        d->tx = NULL;
        d->d.tx_lchans = 0;
    } else if (stream == d->rx) {
        usdr_rfic_streaming_down(&d->d, RFIC_LMS6_RX);
        d->rx->ops->destroy(d->rx);
        d->rx = NULL;
        d->d.rx_lchans = 0;
    } else {
        return -EINVAL;
    }
    return 0;
}

static int usdr_device_m2_lm6_1_sync(device_t* dev,
                                     stream_handle_t** pstreams,
                                     unsigned stream_count,
                                     const char* sync_op)
{
    if (sync_op != NULL && strcmp(sync_op, "off")) {
        struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)dev;

        //if (d->tx) {
        //    usdr_calibrate(&d->d, 3, USDR_CAL_TXLO | USDR_CAL_TXIQIMB, NULL);
        //}
    }

    return sfetrx4_stream_sync(dev, pstreams, stream_count, sync_op);
}


static
int usdr_device_m2_lm6_1_create(lldev_t dev, /*UNUSED*/ device_id_t devid)
{
    int res;

    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)malloc(sizeof(struct dev_m2_lm6_1));
    res = usdr_device_base_create(&d->base, dev);
    if (res) {
        goto failed_free;
    }

    res = vfs_add_const_i64_vec(&d->base.rootfs,
                                s_params_m2_lm6_1_rev000,
                                SIZEOF_ARRAY(s_params_m2_lm6_1_rev000));
    if (res)
        goto failed_tree_creation;

    res = vfs_add_const_str_vec(&d->base.rootfs,
                                s_params_m2_lm6_1_rev000_s,
                                SIZEOF_ARRAY(s_params_m2_lm6_1_rev000_s));
    if (res)
        goto failed_tree_creation;

    res = usdr_vfs_obj_param_init_array(&d->base,
                                        s_fparams_m2_lm6_1_rev000,
                                        SIZEOF_ARRAY(s_fparams_m2_lm6_1_rev000));
    if (res)
        goto failed_tree_creation;

    res = usdr_vfs_obj_link_init_array(&d->base,
                                       s_links,
                                       SIZEOF_ARRAY(s_links));
    if (res)
        goto failed_tree_creation;

    d->base.initialize = &usdr_device_m2_lm6_1_initialize;
    d->base.destroy = &usdr_device_m2_lm6_1_destroy;
    d->base.create_stream = &usdr_device_m2_lm6_1_create_stream;
    d->base.unregister_stream = &usdr_device_m2_lm6_1_unregister_stream;
    d->base.timer_op = usdr_device_m2_lm6_1_sync;
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
    usdr_device_m2_lm6_1_create,
};

int usdr_device_register_m2_lm6_1()
{
    return usdr_device_register(M2_LM6_1_DEVICE_ID_C, &s_ops);
}
