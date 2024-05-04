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

#include "../hw/tmp108/tmp108.h"

#include "../ipblks/streams/sfe_rx_4.h"
#include "../ipblks/streams/stream_sfetrx4_dma32.h"
#include "../ipblks/fgearbox.h"

#include "../generic_usdr/generic_regs.h"

#include "../hw/tps6381x/tps6381x.h"
#include "../hw/lmk05318/lmk05318.h"


enum {
    SRF4_FIFOBSZ = 0x10000, // 64kB
};

enum {
    I2C_ADDR_LMK = 0x65,
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

static
const usdr_dev_param_constant_t s_params_m2_dsdr_rev000[] = {
    { DNLL_SPI_COUNT, 4 },
    { DNLL_I2C_COUNT, 1 },
    { DNLL_SRX_COUNT, 1 },
    { DNLL_STX_COUNT, 0 },
    { DNLL_RFE_COUNT, 1 },
    { DNLL_TFE_COUNT, 0 },
    { DNLL_IDX_REGSP_COUNT, 1 },
    { DNLL_IRQ_COUNT, 8 },

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
    { "/ll/i2c/0/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_DI2C_SIMPLE) },
    { "/ll/i2c/0/base", M2PCI_REG_I2C },
    { "/ll/i2c/0/irq",  M2PCI_INT_I2C_0 },
    { "/ll/qspi_flash/base", M2PCI_REG_QSPI_FLASH },

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

struct dev_m2_dsdr {
    device_t base;
    usdr_vfs_obj_constant_t vfs_const_objs[SIZEOF_ARRAY(s_params_m2_dsdr_rev000)];
    usdr_vfs_obj_base_t vfs_cfg_obj[SIZEOF_ARRAY(s_fparams_m2_dsdr_rev000)];

    lmk05318_state_t lmk;

    subdev_t subdev;

    stream_handle_t* rx;
    stream_handle_t* tx;
};

// enum dev_gpi {
// };

enum dev_gpo {
    IGPO_BANK_LEDS        = 0,
    IGPO_PWR_LMK          = 1,
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

    // for (unsigned i = 0; i < pcount; i++) {
    //     if (strcmp(devparam[i], "lmk") == 0) {
    //         try_lmk = atoi(devval[i]) == 0 ? false : true;
    //     }
    //     if (strcmp(devparam[i], "decim") == 0) {
    //         int dv = atoi(devval[i]);
    //         if (dv != 128)
    //             dv = 2;

    //         d->decim = dv;
    //     }
    // }


    // res = lowlevel_reg_wr32(dev, 0, M2PCI_REG_STAT_CTRL, (1u << 31) | (0x4bu << 24) | 0x67 | (0x48u << 8) |  (0x4bu << 16));
    // if (res)
    //     return res;

    dev_gpo_set(dev, IGPO_BANK_LEDS, 1);

    uint32_t hwid, usr2, pg;
    res = res ? res : dev_gpi_get32(dev, IGPI_USR_ACCESS2, &usr2);
    res = res ? res : dev_gpi_get32(dev, IGPI_HWID, &hwid);

    dev_gpo_set(dev, IGPO_PWR_LMK, 0xf);

    res = res ? res : lowlevel_reg_wr32(d->base.dev, d->subdev, M2PCI_REG_STAT_CTRL,
                                        MAKE_I2C_LUT(0x80 | I2C_ADDR_LMK, 0x80 | I2C_DEV_DCDCBOOST, 0, 0));

    for (unsigned j = 0; j < 10; j++) {
        usleep(10000);
        res = res ? res : dev_gpi_get32(dev, 16, &pg);
        if (res || (pg & 1))
            break;
    }

    for (unsigned j = 0; j < 20; j++) {
        usleep(10000);
        res = res ? res : tps6381x_init(dev, d->subdev, 2, true, true, 3450);
        if (res == 0)
            break;
    }

    usleep(40000);


    for (unsigned j = 0; j < 25; j++) {
        usleep(40000);
        res = res ? res : lmk05318_create(dev, d->subdev, 3, lowlevel_get_ops(dev)->ls_op, &d->lmk);
        if (res == 0)
            break;
    }


    res = res ? res : lmk05318_set_out_mux(&d->lmk, LMK_FPGA_SYSREF, false, LVDS);

    USDR_LOG("DSDR", USDR_LOG_ERROR, "Configuration: OK [%08x, %08x] res=%d   PG=%08x\n", usr2, hwid, res, pg);
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
    unsigned uid = 0;
    struct dev_m2_dsdr *d = (struct dev_m2_dsdr *)malloc(sizeof(struct dev_m2_dsdr));
    res = usdr_device_base_create(&d->base, dev);
    if (res) {
        goto failed_free;
    }

    res = usdr_vfs_obj_const_init_array(&d->base, uid, d->vfs_const_objs,
                                        s_params_m2_dsdr_rev000,
                                        SIZEOF_ARRAY(s_params_m2_dsdr_rev000));
    if (res)
        goto failed_tree_creation;

    uid += SIZEOF_ARRAY(s_params_m2_dsdr_rev000);
    res = usdr_vfs_obj_param_init_array(&d->base, uid, d->vfs_cfg_obj,
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
    return usdr_device_register(M2_DSDR_DEVICE_ID, &s_ops);
}
