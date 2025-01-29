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
//  0 - DSDR
//  1 - Hiper


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
// port 1: LMK_AFEREF
// port 2: LMK_SYSREF
// port 3: N/C
// port 4: FPGA_GT_ETHREF    => BANK224 / REF1
// port 5: FPGA_GT_AFEREF    => BANK226 / REF1
// port 6: FPGA_SYSREF       => BANK65  / T24_U24
// port 7: FPGA_1PPS         => BANK65  / T25_U25

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


static
const usdr_dev_param_constant_t s_params_m2_dsdr_rev000[] = {
    { DNLL_SPI_COUNT, 5 },
    { DNLL_I2C_COUNT, 2 }, // 2
    { DNLL_SRX_COUNT, 1 },
    { DNLL_STX_COUNT, 0 },
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


    { "/ll/sdr/0/rfic/0", (uintptr_t)"afe79xx" },
    { "/ll/sdr/max_hw_rx_chans",  4 },
    { "/ll/sdr/max_hw_tx_chans",  0 },

    { "/ll/sdr/max_sw_rx_chans",  4 },
    { "/ll/sdr/max_sw_tx_chans",  0 },
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

static int dev_m2_dsdr_sdr_rx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_dsdr_dummy(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return 0;
}

static
const usdr_dev_param_func_t s_fparams_m2_dsdr_rev000[] = {
    { "/dm/rate/master",        { dev_m2_dsdr_rate_set, NULL }},
    { "/dm/sdr/0/rx/gain",      { dev_m2_dsdr_gain_set, NULL }},
    { "/dm/sdr/0/rx/freqency",  { dev_m2_dsdr_sdr_rx_freq_set, NULL }},

    { "/dm/sensor/temp",  { NULL, dev_m2_dsdr_senstemp_get }},
    { "/dm/debug/all",    { NULL, dev_m2_dsdr_debug_all_get }},
    { "/dm/debug/rxtime", { NULL, dev_m2_dsdr_debug_rxtime_get }},
    { "/dm/power/en",     { dev_m2_dsdr_pwren_set, NULL }},
    { "/dm/stat",         { NULL, dev_m2_dsdr_stat }},
    { "/debug/lldev",     { NULL, dev_m2_dsdr_debug_lldev_get}},
    { "/dm/sdr/refclk/path", { dev_m2_dsdr_dummy, NULL}},
    { "/debug/clk_info",  { dev_m2_dsdr_debug_clk_info, NULL }},
};

enum dsdr_type {
    DSDR_M2_R0 = 0,
    DSDR_PCIE_HIPER_R0 = 1,
};

struct dev_m2_dsdr {
    device_t base;

    lmk05318_state_t lmk;

    subdev_t subdev;

    unsigned type;

    stream_handle_t* rx;
    stream_handle_t* tx;

    dsdr_hiper_fe_t hiper;
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
    return 0;
}

int dev_m2_dsdr_gain_set(pdevice_t ud, pusdr_vfs_obj_t UNUSED obj, uint64_t value)
{
    return 0;
}

int dev_m2_dsdr_rate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)ud;
    USDR_LOG("DSDR", USDR_LOG_ERROR, "Set rate: %.3f Mhz\n", value / 1.0e6);

    unsigned div = 255;
    int res = 0;

    res = res ? res : lmk05318_tune_apll2(&d->lmk, value, &div);
    res = res ? res : lmk05318_set_out_div(&d->lmk, LMK_FPGA_SYSREF, div);

    USDR_LOG("DSDR", USDR_LOG_ERROR, "Res: %d DIV=%d\n", res, div);

    for (int i = 0; i <10; i++) {
        uint32_t clk;
        res = res ? res : dev_gpi_get32(d->base.dev, 20, &clk);

        USDR_LOG("DSDR", USDR_LOG_ERROR, "Clk %d: %d\n", clk >> 28, clk & 0xfffffff);
        usleep(0.5 * 1e6);
    }
    return 0;
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

static
int usdr_device_m2_dsdr_initialize(pdevice_t udev, unsigned pcount, const char** devparam, const char** devval)
{
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)udev;
    lldev_t dev = d->base.dev;
    int res = 0;
    d->subdev = 0;

    res = res ? res : dev_gpo_set(dev, IGPO_BANK_LEDS, 1);

    if (getenv("USDR_BARE_DEV")) {
        USDR_LOG("XDEV", USDR_LOG_WARNING, "USDR_BARE_DEV is set, skipping initialization!\n");
        return res;
    }

    uint32_t hwid, usr2, pg;
    res = res ? res : dev_gpi_get32(dev, IGPI_USR_ACCESS2, &usr2);
    res = res ? res : dev_gpi_get32(dev, IGPI_HWID, &hwid);

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
        res = res ? res : lmk05318_create(dev, d->subdev, I2C_LMK, &d->lmk);
        if (res == 0)
            break;
    }

    res = res ? res : lmk05318_set_out_mux(&d->lmk, LMK_FPGA_SYSREF, false, LVDS);

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

    afe79xx_state_t st;
    res = res ? res : afe79xx_create(dev, d->subdev, 0, &st);

    sleep(1);
    USDR_LOG("DSDR", USDR_LOG_ERROR, "Initializing AFE...\n");

    res = res ? res : afe79xx_init(&st);

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
    int res = -EINVAL;
    unsigned hwchs;

    if (strstr(sid, "rx") != NULL) {
        if (d->rx) {
            return -EBUSY;
        }

        struct sfetrx4_config rxcfg;
        res = parse_sfetrx4(dformat, channels, pktsyms, &rxcfg);
        if (res) {
            USDR_LOG("UDEV", USDR_LOG_ERROR, "Unable to parse RX stream configuration!\n");
            return res;
        }

        // gearbox initialization
        // res = dev_gpo_set(d->base.dev, IGPO_BANK_ADC_CHMSK, 0x0f);
        // fgearbox_load_fir(d->base.dev, IGPO_BANK_ADC_DSPCHAIN_PRG, d->decim);


        res = create_sfetrx4_stream(dev, CORE_SFERX_DMA32_R0, dformat, channels, pktsyms,
                                    flags, M2PCI_REG_WR_RXDMA_CONFIRM, VIRT_CFG_SFX_BASE,
                                    SRF4_FIFOBSZ, CSR_RFE4_BASE, &d->rx, &hwchs);
        if (res) {
            return res;
        }
        *out_handle = d->rx;
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
