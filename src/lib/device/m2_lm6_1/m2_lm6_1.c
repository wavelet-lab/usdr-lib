// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <unistd.h>
#include <usdr_logging.h>
#include <string.h>
#include <strings.h>

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
    { "/ll/srx/0/rfe",     (uintptr_t)"/ll/rfe/0" },
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

    { "/ll/sdr/0/rfic/0", (uintptr_t)"lms6002d" },
    { "/ll/device/name",  (uintptr_t)"usdr"},

    { "/ll/sdr/max_hw_rx_chans",  1 },
    { "/ll/sdr/max_hw_tx_chans",  1 },

    { "/ll/sdr/max_sw_rx_chans",  1 },
    { "/ll/sdr/max_sw_tx_chans",  1 },

    { "/ll/poll_event/in",  M2PCI_INT_RX },
    { "/ll/poll_event/out", M2PCI_INT_TX },

    // Frontend interface
    { "/ll/fe/0/gpio_busno/0",  0},
    { "/ll/fe/0/uart_busno/0",  0},

    { "/ll/fe/0/spi_busno/0", -1},
    { "/ll/fe/0/i2c_busno/0", -1},
};

static int dev_m2_lm6_1_rate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_rate_m_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_debug_all_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dev_m2_lm6_1_pwren_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_rx_freq_lob_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_rx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_rx_gain_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_gain_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_gain_vga1_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_gain_vga2_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_gainauto_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_rx_bandwidth_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_bandwidth_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_rx_gainpga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_rx_gainvga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_rx_gainlna_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_rx_gainauto_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_rx_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_dc_calib(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_rx_dccorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_dccorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_rx_dc_meas_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dev_m2_lm6_1_sdr_rx_tia_cfb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_rx_tia_rfb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_tx_waveform_gen_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_tx_enable_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

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

static int dev_m2_lm6_1_sdr_tx_bbloopbackm_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_senstemp_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue);

static int dev_m2_lm6_1_sdr_vctcxo_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_clkmeas_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_lm6_1_sdr_clkmeas_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue);

static int dev_m2_lm6_1_sdr_revision_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue);
static int dev_m2_lm6_1_sdr_rfe_throttle_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_lm6_1_sdr_dccorr_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue);

static
const usdr_dev_param_func_t s_fparams_m2_lm6_1_rev000[] = {
    { "/dm/rate/master",        { dev_m2_lm6_1_rate_set, NULL }},
    { "/dm/rate/rxtxadcdac",    { dev_m2_lm6_1_rate_m_set, NULL }},

    { "/dm/debug/all",          { NULL, dev_m2_lm6_1_debug_all_get }},
    { "/dm/power/en",           { dev_m2_lm6_1_pwren_set, NULL }},

    { "/dm/sdr/channels",       { NULL, NULL }},
    { "/dm/sensor/temp",        { NULL, dev_m2_lm6_1_sdr_senstemp_get }},

    { "/dm/sdr/refclk/frequency", {dev_m2_lm6_1_sdr_refclk_frequency_set, dev_m2_lm6_1_sdr_refclk_frequency_get}},
    { "/dm/sdr/refclk/path",      {dev_m2_lm6_1_sdr_refclk_path_set, NULL}},

    { "/dm/sdr/0/rx/tia/cfb",   { dev_m2_lm6_1_sdr_rx_tia_cfb_set, NULL }},
    { "/dm/sdr/0/rx/tia/rfb",   { dev_m2_lm6_1_sdr_rx_tia_rfb_set, NULL }},

    { "/dm/sdr/0/rx/dc/meas",   { NULL, dev_m2_lm6_1_sdr_rx_dc_meas_get }},
    { "/dm/sdr/0/rx/dccorr",    { dev_m2_lm6_1_sdr_rx_dccorr_set, dev_m2_lm6_1_sdr_dccorr_get }},
    { "/dm/sdr/0/tx/dccorr",    { dev_m2_lm6_1_sdr_tx_dccorr_set, NULL }},

    { "/dm/sdr/0/calibrate",    { dev_m2_lm6_1_sdr_dc_calib, NULL }},

    { "/dm/sdr/0/rx/freqency/lob",{ dev_m2_lm6_1_sdr_rx_freq_lob_set, NULL }},
    { "/dm/sdr/0/rx/freqency",  { dev_m2_lm6_1_sdr_rx_freq_set, NULL }},
    { "/dm/sdr/0/tx/freqency",  { dev_m2_lm6_1_sdr_tx_freq_set, NULL }},
    { "/dm/sdr/0/rx/gain",      { dev_m2_lm6_1_sdr_rx_gain_set, NULL }},
    { "/dm/sdr/0/tx/gain",      { dev_m2_lm6_1_sdr_tx_gain_set, NULL }},
    { "/dm/sdr/0/tx/gain/vga1", { dev_m2_lm6_1_sdr_tx_gain_vga1_set, NULL }},
    { "/dm/sdr/0/tx/gain/vga2", { dev_m2_lm6_1_sdr_tx_gain_vga2_set, NULL }},
    { "/dm/sdr/0/rx/gain/pga",  { dev_m2_lm6_1_sdr_rx_gainpga_set, NULL }},
    { "/dm/sdr/0/rx/gain/vga",  { dev_m2_lm6_1_sdr_rx_gainvga_set, NULL }},
    { "/dm/sdr/0/rx/gain/lna",  { dev_m2_lm6_1_sdr_rx_gainlna_set, NULL }},

    { "/dm/sdr/0/rx/gain/auto",  { dev_m2_lm6_1_sdr_rx_gainauto_set, NULL }},
    { "/dm/sdr/0/tx/gain/auto",  { dev_m2_lm6_1_sdr_tx_gainauto_set, NULL }},

    { "/dm/sdr/0/rx/path",      { dev_m2_lm6_1_sdr_rx_path_set, NULL }},
    { "/dm/sdr/0/tx/path",      { dev_m2_lm6_1_sdr_tx_path_set, NULL }},

    { "/dm/sdr/0/rx/bandwidth", { dev_m2_lm6_1_sdr_rx_bandwidth_set, NULL }},
    { "/dm/sdr/0/tx/bandwidth", { dev_m2_lm6_1_sdr_tx_bandwidth_set, NULL }},

    { "/dm/sdr/0/tx/waveform_gen", { dev_m2_lm6_1_sdr_tx_waveform_gen_set, NULL}},

    { "/debug/hw/lms6002d/0/reg",  { dev_m2_lm6_1_debug_lms6002d_reg_set, dev_m2_lm6_1_debug_lms6002d_reg_get }},
    { "/debug/hw/si5332/0/reg",    { dev_m2_lm6_1_debug_si5332_reg_set, dev_m2_lm6_1_debug_si5332_reg_get }},
    { "/debug/hw/tps6381x/0/reg",  { dev_m2_lm6_1_debug_tps6381x_reg_set, dev_m2_lm6_1_debug_tps6381x_reg_get }},


    { "/dm/sdr/0/tx/enable",      { dev_m2_lm6_1_sdr_tx_enable_set, NULL }},

    { "/dm/sdr/0/tfe/antcfg",     { dev_m2_lm6_1_sdr_tx_antennat_port_cfg_set, NULL }},

    { "/dm/sdr/0/core/atcrbs/reg", { dev_m2_lm6_1_sdr_atcrbs_set, dev_m2_lm6_1_sdr_atcrbs_get }},
    { "/dm/sdr/0/tx/bbloopbackm",  { dev_m2_lm6_1_sdr_tx_bbloopbackm_set, NULL }},

    { "/dm/sdr/0/dac_vctcxo",      { dev_m2_lm6_1_sdr_vctcxo_set, NULL }},
    { "/dm/sdr/0/clkmeas", { dev_m2_lm6_1_sdr_clkmeas_set, dev_m2_lm6_1_sdr_clkmeas_get }},

    { "/dm/revision", { NULL, dev_m2_lm6_1_sdr_revision_get }},
    { "/dm/sdr/0/rfe/throttle", { dev_m2_lm6_1_sdr_rfe_throttle_set, NULL }},
};

struct dev_m2_lm6_1 {
    device_t base;

    struct usdr_dev d;
    struct dev_fe* fe;

    uint32_t debug_lms6002d_last;
    uint32_t debug_si5332_last;
    uint32_t debug_tps6381x_last;
    uint32_t debug_lp8758_last;

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

static int dev_gpo_set(lldev_t dev, unsigned bank, unsigned data)
{
    return lowlevel_reg_wr32(dev, 0, 0, ((bank & 0x7f) << 24) | (data & 0xff));
}

static int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, 0, 16 + (bank / 4), data);
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
   // struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    uint32_t v = 0;
    int res = dev_gpi_get32(ud->dev, 20, &v);
    if (res)
        return res;

    *ovalue = v;
    return 0;
}

int dev_m2_lm6_1_debug_lms6002d_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    int res;

    d->debug_lms6002d_last = ~0u;
    res = lowlevel_spi_tr32(d->base.dev, 0, 0, value & 0xffff, &d->debug_lms6002d_last);

    USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug LMS6 REG %04x => %04x\n",
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


int dev_m2_lm6_1_sdr_tx_waveform_gen_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    //struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    //USDR_LOG("UDEV", USDR_LOG_WARNING, "M2_LM6_1: TX WAVEFORM GEN %d\n", (int)value);
    //return dev_txbuffill(d, value);
    return -EINVAL;
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

int dev_m2_lm6_1_sdr_tx_bbloopbackm_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    //struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    //int res;
    // res = sfe_tx4_ctl(d->base.dev, 0, M2PCI_REG_WR_TXDMA_CNF_L, 0, 0,
    //                   value > 1 ? true : false,
    //                   true);
    //return res;
    return -EINVAL;
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
    RATE_MIN =  1000000,
    RATE_MAX = 80000000,
};

int dev_m2_lm6_1_rate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    if (value < RATE_MIN || value > RATE_MAX)
        return -ERANGE;
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_set_samplerate_ex(&d->d, value, value, 0, 0, 0);
}

int dev_m2_lm6_1_rate_m_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
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
    // struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    USDR_LOG("UDEV", USDR_LOG_INFO, "M2_LM6_1: power en:%d\n", (int)value);
    // return usdr_pwren(&d->d, value);

    return 0;
}

int dev_m2_lm6_1_sdr_tx_enable_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    //struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    USDR_LOG("UDEV", USDR_LOG_INFO, "M2_LM6_1: TX en:%d\n", (int)value);
    return 0;
}

int dev_m2_lm6_1_sdr_rx_freq_lob_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_set_lob_freq(&d->d, value);
}

int dev_m2_lm6_1_sdr_rx_freq_set(pdevice_t ud, UNUSED pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_fe_set_freq(&d->d, false, value, NULL);
}
int dev_m2_lm6_1_sdr_tx_freq_set(pdevice_t ud, UNUSED pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return usdr_rfic_fe_set_freq(&d->d, true, value, NULL);
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

    // 00 –All output buffers powered down;
    // 01 –First buffer enabled for LNA1 path (default);   WB:  250 - 2700
    // 10 –Second buffer enabledfor LNA2 path;             HB: 2700 - 3800
    // 11 –Third buffer enabledfor LNA3 path               LB: external / MIXER
#if 0
    enum {
        LMS6_PATH_NONE = 0,
        LMS6_PATH_WB = 1,
        LMS6_PATH_HB = 2,
        LMS6_PATH_LB = 3,
    };

    int bandsw = -1;
    if (value > 4096) {
        const char* param = (const char*)value;
        if (strcasecmp("rxl", param) == 0) {
            value = LMS6_PATH_LB;
        } else if (strcasecmp("rxw", param) == 0) {
            value = LMS6_PATH_WB; bandsw = 0;
        } else if (strcasecmp("rxh", param) == 0) {
            value = LMS6_PATH_HB; bandsw = 1;
        } else {
            value = LMS6_PATH_NONE;
        }
    }

    if (bandsw != -1) {
        int res = dev_gpo_set(d->base.dev, IGPO_RXSW, (d->revision == 0) ? !bandsw : bandsw);
        if (res)
            return res;
    }

    return lms6002d_set_rx_path(&d->lms, value);
#endif

    // if (value > 4096) {
    //     const char* param = (const char*)value;
    //     int idx = find_param_list(param, s_rx_path_list, SIZEOF_ARRAY(s_rx_path_list));
    //     if (idx < 0) {
    //         USDR_LOG("UDEV", USDR_LOG_WARNING, "MP_LM7_1_GPS: unknown '%s' path!\n",
    //                  param);
    //         return -EINVAL;
    //     }

    //     value = s_rx_path_list[idx].param;
    // }

    if (value > 4096) {
        const char* param = (const char*)value;
        if (strcasecmp("rxl", param) == 0) {
            value = (uintptr_t)"EXT";
        } else if (strcasecmp("rxw", param) == 0) {
            value = (uintptr_t)"W";
        } else if (strcasecmp("rxh", param) == 0) {
            value = (uintptr_t)"H";
        };
    }

    return usdr_rfic_fe_set_rxlna(&d->d, (const char *)(uintptr_t)value);
}

int dev_m2_lm6_1_sdr_tx_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    //struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    return 0;
#if 0
    int bandsw = -1;
    if (value > 4096) {
        const char* param = (const char*)value;
        if (strcasecmp("txw", param) == 0) {
            bandsw = 1; value = TXPATH_PA1;
        } else if (strcasecmp("txh", param) == 0) {
            bandsw = 0; value = TXPATH_PA2;
        } else {
            value = TXPATH_OFF;
        }
    }

    if (bandsw != -1) {
        int res = dev_gpo_set(d->base.dev, IGPO_TXSW, (d->revision == 0) ? !bandsw : bandsw);
        if (res)
            return res;
    }

    return lms6002d_set_tx_path(&d->lms, value);
#endif
}

int dev_m2_lm6_1_sdr_rx_dccorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
#if 0
    struct dev_m2_lm6_1 *d = (struct dev_m2_lm6_1 *)ud;
    int res;
    unsigned chan = (unsigned)(value >> 32);
    unsigned vi = (value >> 16) & 0xffff;
    unsigned vq = (value >> 0) & 0xffff;

    if (chan == 1)
        res = lms6002d_set_rxfedc(&d->lms, vi, vq);
    else
        res = -EINVAL;

    return res;
#endif
    return -EINVAL;
}

int dev_m2_lm6_1_sdr_tx_dccorr_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return -EINVAL;
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
    board_ext_pciefe_t* board_fe = device_fe_to(d->fe, "pciefe");
    board_exm2pe_t* board = device_fe_to(d->fe, "exm2pe");
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

        res = create_sfetrx4_stream(dev, CORE_SFERX_DMA32_R0, dformat, channels->count, &lchans, pktsyms,
                                    flags, M2PCI_REG_WR_RXDMA_CONFIRM, VIRT_CFG_SFX_BASE, 0,
                                    SRF4_FIFOBSZ, CSR_RFE4_BASE, &d->rx, &chans);
        if (res) {
            return res;
        }
        *out_handle = d->rx;
    } else if (strstr(sid, "tx") != NULL) {
        if (d->tx) {
            return -EBUSY;
        }
        res = usdr_rfic_streaming_up(&d->d, RFIC_LMS6_TX);
        if (res) {
            return res;
        }

        res = create_sfetrx4_stream(dev, CORE_SFETX_DMA32_R0, dformat, channels->count, &lchans, pktsyms,
                                    flags, M2PCI_REG_WR_TXDMA_CNF_L, M2PCI_REG_WR_SYNC_CTRL, M2PCI_REG_RD_TXDMA_STAT,
                                    0, 0, &d->tx, &chans);
        if (res) {
            return res;
        }
        *out_handle = d->tx;
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
    } else if (stream == d->rx) {
        usdr_rfic_streaming_down(&d->d, RFIC_LMS6_RX);
        d->rx->ops->destroy(d->rx);
        d->rx = NULL;
    } else {
        return -EINVAL;
    }
    return 0;
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

    res = usdr_vfs_obj_param_init_array(&d->base,
                                        s_fparams_m2_lm6_1_rev000,
                                        SIZEOF_ARRAY(s_fparams_m2_lm6_1_rev000));
    if (res)
        goto failed_tree_creation;

    d->base.initialize = &usdr_device_m2_lm6_1_initialize;
    d->base.destroy = &usdr_device_m2_lm6_1_destroy;
    d->base.create_stream = &usdr_device_m2_lm6_1_create_stream;
    d->base.unregister_stream = &usdr_device_m2_lm6_1_unregister_stream;
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
    usdr_device_m2_lm6_1_create,
};

int usdr_device_register_m2_lm6_1()
{
    return usdr_device_register(M2_LM6_1_DEVICE_ID_C, &s_ops);
}
