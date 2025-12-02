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
#include "../device_fe.h"

#include "../hw/tps6594/tps6594.h"
#include "../hw/si549/si549.h"
#include "../hw/ads42lbx9/ads42lbx9.h"
#include "../hw/tmp108/tmp108.h"
#include "../hw/lmk04832/lmk04832.h"

#include "../ipblks/streams/sfe_rx_4.h"
#include "../ipblks/streams/stream_sfetrx4_dma32.h"
#include "../ipblks/fgearbox.h"

#include "../generic_usdr/generic_regs.h"

#include "../device/ext_fe_100_5000/ext_fe_100_5000.h"


// single ADC, no DAC
static const
device_id_t s_uuid_000 = { { 0x63, 0x51, 0x02, 0x06, 0x7f, 0x31, 0x44, 0x2a, 0xa7, 0xa1, 0x6c, 0x05, 0xf9, 0xc5, 0xad, 0x48 } };

enum {
    SRF4_FIFOBSZ = 0x10000, // 64kB
};

// dual ADC + clk_gen, no DAC
// dual ADC + clk_gen + DAC

enum BUSIDX_m2_da09_4_ad45_2_rev000 {
    I2C_BUS_SI549 = MAKE_LSOP_I2C_ADDR(0, 0, 0x67),
    I2C_BUS_TPS6594 = MAKE_LSOP_I2C_ADDR(0, 0, 0x48),
    I2C_BUS_TMP108 = MAKE_LSOP_I2C_ADDR(0, 1, 0x4b),

    SPI_ADS42LBX9_0 = 0,
    SPI_ADS42LBX9_1 = 1,
    SPI_LMK04832 = 2,
    SPI_DAC = 3,
    SPI_EXTERNAL = 4, //SPI to external frontend
};

// dual ADC, and DAC
static
const usdr_dev_param_constant_t s_params_m2_da09_4_ad45_2_rev000[] = {
    { DNLL_SPI_COUNT, 5 },
    { DNLL_I2C_COUNT, 1 },
    { DNLL_SRX_COUNT, 1 },
    { DNLL_STX_COUNT, 0 },
    { DNLL_RFE_COUNT, 1 },
    { DNLL_TFE_COUNT, 0 },
    { DNLL_IDX_REGSP_COUNT, 1 },
    { DNLL_IRQ_COUNT, 8 },
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
    { "/ll/spi/4/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_SIMPLE) }, //USDR_BS_SPI_CFG_CS8
    { "/ll/spi/4/base", REG_SPI_EXT_DATA },
    { "/ll/spi/4/irq",  M2PCI_INT_SPI_EXT },

    { "/ll/i2c/0/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_DI2C_SIMPLE) },
    { "/ll/i2c/0/base", M2PCI_REG_I2C },
    { "/ll/i2c/0/irq",  M2PCI_INT_I2C_0 },

    { "/ll/gpio/0/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_GPIO15_SIMPLE) },
    { "/ll/gpio/0/base", M2PCI_REG_GPIO_S },
    { "/ll/gpio/0/irq",  -1 },

    { "/ll/qspi_flash/core", USDR_MAKE_COREID(USDR_CS_BUS, USDR_QSPI_FLASH_24_RW) },
    { "/ll/qspi_flash/base", M2PCI_REG_QSPI_FLASH },
    { "/ll/qspi_flash/master_off", 0x1C0000 },

    { "/ll/gpi/0/core", USDR_MAKE_COREID(USDR_CS_GPI, USDR_GPI_32BIT_12) },
    { "/ll/gpi/0/base", M2PCI_REG_RD_GPI0_12 },
    { "/ll/gpo/0/core", USDR_MAKE_COREID(USDR_CS_GPO, USDR_GPO_8BIT) },
    { "/ll/gpo/0/base", M2PCI_REG_STAT_CTRL },

    // Indexed area map
    { "/ll/idx_regsp/0/base", M2PCI_REG_WR_BADDR },
    { "/ll/idx_regsp/0/virt_base", VIRT_CFG_SFX_BASE },

    { "/ll/bucket/0/core", USDR_MAKE_COREID(USDR_CS_BUCKET, USDR_BUCKET_16B) },
    { "/ll/bucket/0/base", M2PCI_REG_WR_PNTFY_CFG },

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

    { "/ll/sdr/0/rfic/0", (uintptr_t)"ad45lb49" },
    { "/ll/device/name",  (uintptr_t)"lsdr"},

    { "/ll/sdr/max_hw_rx_chans",  1 },
    { "/ll/sdr/max_hw_tx_chans",  0 },

    { "/ll/sdr/max_sw_rx_chans",  1 },
    { "/ll/sdr/max_sw_tx_chans",  0 },

    // Frontend interface
    { "/ll/fe/0/gpio_busno/0", 0 },
    { "/ll/fe/0/spi_busno/0", 4},
    { "/ll/fe/0/i2c_busno/0", -1},

};

static int dev_m2_d09_4_ad45_2_rate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_d09_4_ad45_2_rate_m_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_d09_4_ad45_2_gain_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_d09_4_ad45_2_gainpga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_d09_4_ad45_2_gainvga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_d09_4_ad45_2_gainlna_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_d09_4_ad45_2_senstemp_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dev_m2_d09_4_ad45_2_debug_all_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dev_m2_d09_4_ad45_2_pwren_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_d09_4_ad45_2_debug_rxtime_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dev_m2_d09_4_ad45_2_stat(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dev_m2_d09_4_ad45_2_debug_lldev_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dev_m2_d09_4_ad45_2_debug_clk_info(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_d09_4_ad45_2_sdr_rx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_d09_4_ad45_2_sdr_rx_bandwidth_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_m2_d09_4_ad45_2_dummy(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return 0;
}

static int dev_m2_d09_4_ad45_2_sdr_fe_lo1(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_d09_4_ad45_2_sdr_fe_lo2(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_d09_4_ad45_2_sdr_fe_ifsel(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_m2_d09_4_ad45_2_sdr_fe_band(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static
const usdr_dev_param_func_t s_fparams_m2_da09_4_ad45_2_rev000[] = {
    { "/dm/rate/master",        { dev_m2_d09_4_ad45_2_rate_set, NULL }},
    { "/dm/rate/rxtxadcdac",    { dev_m2_d09_4_ad45_2_rate_m_set, NULL }},
    { "/dm/sdr/0/rx/gain",      { dev_m2_d09_4_ad45_2_gain_set, NULL }},
    { "/dm/sdr/0/rx/gain/pga",  { dev_m2_d09_4_ad45_2_gainpga_set, NULL }},
    { "/dm/sdr/0/rx/gain/vga",  { dev_m2_d09_4_ad45_2_gainvga_set, NULL }},
    { "/dm/sdr/0/rx/gain/lna",  { dev_m2_d09_4_ad45_2_gainlna_set, NULL }},

    { "/dm/sdr/0/rx/freqency",  { dev_m2_d09_4_ad45_2_sdr_rx_freq_set, NULL }},
    { "/dm/sdr/0/rx/bandwidth", { dev_m2_d09_4_ad45_2_sdr_rx_bandwidth_set, NULL }},

    { "/dm/sdr/0/rx/path",      { dev_m2_d09_4_ad45_2_dummy, NULL }},
    { "/dm/sdr/0/tx/path",      { dev_m2_d09_4_ad45_2_dummy, NULL }},

    { "/dm/sdr/channels",       { NULL, NULL }},

    { "/dm/sensor/temp",  { NULL, dev_m2_d09_4_ad45_2_senstemp_get }},
    { "/dm/debug/all",    { NULL, dev_m2_d09_4_ad45_2_debug_all_get }},
    { "/dm/debug/rxtime", { NULL, dev_m2_d09_4_ad45_2_debug_rxtime_get }},
    { "/dm/power/en",     { dev_m2_d09_4_ad45_2_pwren_set, NULL }},
    { "/dm/stat",         { NULL, dev_m2_d09_4_ad45_2_stat }},
    { "/debug/lldev",     { NULL, dev_m2_d09_4_ad45_2_debug_lldev_get}},
    { "/dm/sdr/refclk/path", { dev_m2_d09_4_ad45_2_dummy, NULL}},
    { "/debug/clk_info",  { dev_m2_d09_4_ad45_2_debug_clk_info, NULL }},

    { "/dm/sdr/0/fe/lo1",      { dev_m2_d09_4_ad45_2_sdr_fe_lo1, NULL }},
    { "/dm/sdr/0/fe/lo2",      { dev_m2_d09_4_ad45_2_sdr_fe_lo2, NULL }},
    { "/dm/sdr/0/fe/ifsel",    { dev_m2_d09_4_ad45_2_sdr_fe_ifsel, NULL }},
    { "/dm/sdr/0/fe/band",     { dev_m2_d09_4_ad45_2_sdr_fe_band, NULL }},
};

struct dev_m2_d09_4_ad45_2 {
    device_t base;

    uint32_t adc_rate;
    unsigned rxbb_rate;
    unsigned rxbb_decim;

    bool lmk04832_present;
    bool dac38rf_present;
    bool adc_present[2];

    unsigned decim;

    struct dev_fe* fe;

    uint8_t active_adc; //Enabled ADCs
    stream_handle_t* rx;
    stream_handle_t* tx;

    // fe
    uint64_t fe_lo1_khz;
    uint64_t fe_lo2_khz;
    unsigned fe_ifsel;
    unsigned fe_band;
};

enum dev_gpi {
    IGPI_BANK_ADC_CLK_HI = 24 + 0,
    IGPI_BANK_ADC_CLK_LO = 24 + 1,

    IGPI_BANK_ADC_STAT   = 4,
    IGPI_BANK_ADC_PBRSHI = 5,
    IGPI_BANK_ADC_PBRSLO = 6,
    IGPI_BANK_LMK_STAT = 7,


    IGPI_BANK_REFCNTR0_0 = 8,
    IGPI_BANK_REFCNTR0_1 = 9,
    IGPI_BANK_REFCNTR0_2 = 10,
    IGPI_BANK_REFCNTR0_3 = 11,
    IGPI_BANK_REFCNTR1_0 = 12,
    IGPI_BANK_REFCNTR1_1 = 13,
    IGPI_BANK_REFCNTR1_2 = 14,
    IGPI_BANK_REFCNTR1_3 = 15,
    IGPI_BANK_ALARMCNTR_0 = 16,
    IGPI_BANK_ALARMCNTR_1 = 17,
    IGPI_BANK_ALARMCNTR_2 = 18,
    IGPI_BANK_ALARMCNTR_3 = 19,
    IGPI_BANK_DACLINKST_0 = 20,
    IGPI_BANK_DACLINKST_1 = 21,
    IGPI_BANK_DACLINKST_2 = 22,
    IGPI_BANK_DACLINKST_3 = 23,
};

enum dev_gpo {
    IGPO_BANK_CFG_RCLK    = 0,  //  clkselref
    IGPO_BANK_CFG_DAC     = 1,
    IGPO_BANK_CFG_ADC     = 2,  //  [7:4] -- FIFO reset;  [1:0] adc_reset
    IGPO_BANK_PGA_LE      = 3,
    IGPO_BANK_PGA_GAIN    = 4,

    IGPO_BANK_ADC_CHMSK     = 5, // [5:4] -- rb_selector; [3:0] -- set_mask
    IGPO_BANK_ADC_PHY_CTL_0 = 6,
    IGPO_BANK_ADC_PHY_CTL_1 = 7,
    IGPO_BANK_ADC_PHY_PBRS  = 8,
    IGPO_BANK_ADC_DSP_MIX2  = 9,

    // NCO freq
    IGPO_BANK_ADC_NCO_0 = 10,
    IGPO_BANK_ADC_NCO_1 = 11,
    IGPO_BANK_ADC_NCO_2 = 12,
    IGPO_BANK_ADC_NCO_3 = 13,

    // DSP FIR gearbox
    IGPO_BANK_ADC_DSPCHAIN_RST   = 14,
    IGPO_BANK_ADC_DSPCHAIN_PRG   = 15,
};

static int dev_gpo_set(lldev_t dev, unsigned bank, unsigned data)
{
    return lowlevel_reg_wr32(dev, 0, 0, ((bank & 0x7f) << 24) | (data & 0xff));
}

static int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, 0, 16 + (bank / 4), data);
}

enum {
    PHY_RD_L0 = 0,
    PHY_RD_L1 = 1,
    PHY_RD_L2 = 2,
    PHY_RD_L3 = 3,
    PHY_RD_FR = 4,
    PHY_RD_CK = 5,
};

enum {
    PHY_LVDS_DATA_OFF = 0,
    PHY_LVDS_BITSLIP_RST_OFF = 5,
    PHY_LVDS_FCK_LD_OFF = 6,
    PHY_LVDS_CLK_LD_OFF = 7,
    PHY_LVDS_L0_LD_OFF = 8,
    PHY_LVDS_L1_LD_OFF = 9,
    PHY_LVDS_L2_LD_OFF = 10,
    PHY_LVDS_L3_LD_OFF = 11,
    PHY_LVDS_RD_OFF = 12,
};

static const unsigned s_meas_clk_mask[4] = {0xdeadbeef, 0x1ee1c0de, 0xccddeeff, 0x0};

int dev_m2_d09_4_ad45_2_get_clk(struct dev_m2_d09_4_ad45_2 *d, bool aftergbox, unsigned adc, uint32_t* clk)
{
    int res;
    lldev_t dev = d->base.dev;
    uint32_t stat;

    res = dev_gpo_set(dev, IGPO_BANK_ADC_CHMSK, (aftergbox ? 0x80 : 0x00) | ((adc & 0x3) << 4));
    if (res)
        return res;

    res = dev_gpi_get32(dev, IGPI_BANK_ADC_CLK_HI, &stat);
    if (res)
        return res;

    uint32_t masked = (stat ^ s_meas_clk_mask[adc]);

    USDR_LOG("0944", USDR_LOG_ERROR, "RAW[%d:%d] %08x\n", aftergbox, adc, masked);

    uint32_t freq = (((uint64_t)(masked & 0xffffff) * 100000000ul)) / (1u<<20);
    *clk = freq;
    return 0;
}

int dev_m2_d09_4_ad45_2_debug_clk_info(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t ovalue)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)ud;
    uint32_t f = ~0u, fg = ~0u;
    int res = 0;

    res = (res) ? res : dev_m2_d09_4_ad45_2_get_clk(d, false, 0, &f);
    res = (res) ? res : dev_m2_d09_4_ad45_2_get_clk(d, true, 0, &fg);

    USDR_LOG("0944", USDR_LOG_ERROR, "CLK_0 %d -- %d \n", f, fg);
    return res;
}

int dev_m2_d09_4_ad45_2_stat(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* value)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)ud;
    lldev_t dev = d->base.dev;
    unsigned stat[2];
    int res;
    unsigned adc;


    for (unsigned k = 0; k < 24; k++) {
        for (adc = 0; adc < 4; adc++) {
            res = dev_gpo_set(dev, IGPO_BANK_ADC_CHMSK, 0 | (adc << 4));
            if (res)
                return res;

            res = dev_gpi_get32(dev, IGPI_BANK_ADC_CLK_HI, &stat[0]);
            if (res)
                return res;
            uint32_t masked = (stat[0] ^ s_meas_clk_mask[adc]);
            uint64_t freq = ((masked & 0xffffff) * 100000000ul) / (1u<<20);
            USDR_LOG("0944", USDR_LOG_ERROR, "ADC_CLK[%d] = %08d (%08x %08x)\n", adc, (unsigned)freq, stat[0], masked);

            res = dev_gpi_get32(dev, IGPI_BANK_ADC_STAT, &stat[1]);
            if (res)
                return res;

        }

        usleep(10000);
    }


    unsigned bad[2] = { 0, 0 };

    enum {
        CLK_FAIL = 1,
        FCK_FAIL = 2,
        CLK_CHANGE = 16,
        FCK_CHANGE = 32,
    };

    uint32_t delay_st[32];
    unsigned lvds_clk[2], lvds_fck[2];
    unsigned plvds_clk[2], plvds_fck[2];

    //Disable bitslip and probe every TAP
    for (unsigned step = 0; step < 32; step++) {
        res = dev_gpo_set(dev, IGPO_BANK_ADC_CHMSK, 0xf);
        if (res)
            return res;

        res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_CTL_0, (1 << PHY_LVDS_BITSLIP_RST_OFF) | (1 << PHY_LVDS_CLK_LD_OFF) | step);
        if (res)
            return res;
        res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_CTL_0, (1 << PHY_LVDS_BITSLIP_RST_OFF) | step);
        if (res)
            return res;

        usleep(10);

        for (unsigned j = 0; j < 8; j++) {           
            for (unsigned p = 0; p < 4; p++) {
                res = dev_gpo_set(dev, IGPO_BANK_ADC_CHMSK, (1u << p) | (p << 4));
                if (res)
                    return res;

                res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_CTL_1, (PHY_RD_CK) << (PHY_LVDS_RD_OFF - 8));
                if (res)
                    return res;

                res = dev_gpi_get32(dev, IGPI_BANK_ADC_STAT, &lvds_clk[p]);
                if (res)
                    return res;

                res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_CTL_1, (PHY_RD_FR) << (PHY_LVDS_RD_OFF - 8));
                if (res)
                    return res;

                res = dev_gpi_get32(dev, IGPI_BANK_ADC_STAT, &lvds_fck[p]);
                if (res)
                    return res;

                if (step == 0 && j == 0) {
                    plvds_clk[p] = lvds_clk[p];
                    plvds_fck[p] = lvds_fck[p];
                }
                if (j == 0) {
                    delay_st[step] = 0;
                }


                unsigned clkst = (lvds_clk[p]) & 0xff;
                unsigned clkstp = (plvds_clk[p]) & 0xff;
                unsigned fckst = (lvds_fck[p]) & 0xff;
                unsigned fckstp = (plvds_fck[p]) & 0xff;
                unsigned msk = 0;

                if ((clkst != 0x55) && (clkst != 0xAA))
                    msk |= CLK_FAIL;
                else if (((clkstp == 0x55 || clkstp == 0xAA)) && (clkstp != clkst))
                    msk |= CLK_CHANGE;


                if ((fckst != 0x33) && (fckst != 0x66) && (fckst != 0xCC) && (fckst != 0x99))
                    msk |= FCK_FAIL;
                else if (((fckstp == 0x33 || fckstp == 0x66 || fckstp == 0xCC || fckstp == 0x99)) && (fckstp != fckst))
                    msk |= FCK_CHANGE;

                delay_st[step] |= (msk & 0xff) << (8 * p);

                plvds_clk[p] = lvds_clk[p];
                plvds_fck[p] = lvds_fck[p];
            }
        }
    }

    for (unsigned step = 0; step < 32; step++) {
        USDR_LOG("0944", USDR_LOG_ERROR, "STEP[%2d] = %08x\n", step, delay_st[step]);
    }

    unsigned s[2] = { 0, 0 };
    for (unsigned p = 0; p < 2; p++) {
        for (unsigned step = 0; step < 32; step++) {

            unsigned msk = delay_st[step] >> (8*p);
            if (msk != 0)
                s[p] = 1;

            if (msk == 0 && s[p] != 0) {
                bad[p] = step;
                break;
            }
        }
    }

    bad[0]++;
    bad[1]++;

    USDR_LOG("0944", USDR_LOG_ERROR, "SET1/0 [%d] [%d]\n", bad[1], bad[0]);

    res = dev_gpo_set(dev, IGPO_BANK_ADC_CHMSK, 1);
    if (res)
        return res;
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_CTL_0, 0x20 | 0x80 | bad[0]);
    if (res)
        return res;
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_CTL_0, 0x20 | bad[0]);
    if (res)
        return res;

    res = dev_gpo_set(dev, IGPO_BANK_ADC_CHMSK, 2);
    if (res)
        return res;
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_CTL_0, 0x20 | 0x80 | bad[1]);
    if (res)
        return res;
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_CTL_0, 0x20 | bad[1]);
    if (res)
        return res;

    usleep(10);

    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_CTL_0, 0);
    if (res)
        return res;
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_CTL_1, 0);
    if (res)
        return res;

    usleep(100);
/*
    //Start PBRS check
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS_0, 1 << 7);
    if (res)
        return res;
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS_1, 1 << 7);
    if (res)
        return res;
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS_0, 0);
    if (res)
        return res;
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS_1, 0);
    if (res)
        return res;

    usleep(1000);

    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS_0, 1 << 5);
    if (res)
        return res;
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS_1, 1 << 5);
    if (res)
        return res;
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS_0, 0);
    if (res)
        return res;
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS_1, 0);
    if (res)
        return res;

    res = dev_gpi_get32(dev, IGPI_BANK_ADCSTAT_0, &stat[0]);
    if (res)
        return res;

    for (unsigned z = 0; z < 8; z++) {
        res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS_0, (1 << 5) | z);
        if (res)
            return res;
        res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS_1, (1 << 5) | z);
        if (res)
            return res;
        res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS_0, 0);
        if (res)
            return res;
        res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS_1, 0);
        if (res)
            return res;

        res = dev_gpi_get32(dev, IGPI_BANK_ADCCLKF_0, &stat[1]);
        if (res)
            return res;

    }
*/
    *value = bad[0];
    return 0;
}

static int set_nco(struct dev_m2_d09_4_ad45_2 *d, unsigned chans, int offset)
{
    lldev_t dev = d->base.dev;
    int res;

    res = dev_gpo_set(dev, IGPO_BANK_ADC_CHMSK, chans);
    if (res)
        return res;

    if (d->adc_rate == 0)
        return -EINVAL;

    while (offset > d->adc_rate)
        offset -= d->adc_rate;

    int32_t v = offset;
    v -= d->adc_rate / 4;

    int32_t freq = ((int64_t)v << 33) /  d->adc_rate / 2;

    res = dev_gpo_set(dev, IGPO_BANK_ADC_DSP_MIX2, 0 /*reverse_spec ? 0 : 3*/);

    for (unsigned p = 0; p < 4; p++) {
        res = dev_gpo_set(dev, IGPO_BANK_ADC_NCO_0 + p, (freq >> (8*p)) & 0xff);
        if (res)
            return res;
    }

    return res;
}

int dev_m2_d09_4_ad45_2_sdr_rx_bandwidth_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return 0;
}

int dev_m2_d09_4_ad45_2_sdr_rx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)ud;
    int res;
    ext_fe_100_5000_t* f;
    int offset = value;

    if ((f = (ext_fe_100_5000_t*)device_fe_to(d->fe, "fe1005000"))) {
        unsigned fr = (value / 1000) * 1000;
        if (fr < 2000000)
            fr = 2000000;


        res = ext_fe_100_5000_set_frequency(f, fr);
        if (res)
            return res;

        offset = value - fr;
    }

    res = set_nco(d, 0xf, offset);
    if (res)
        return res;

    return 0;
}

static int dev_m2_d09_4_ad45_2_pga(struct dev_m2_d09_4_ad45_2 *d, unsigned value)
{
    lldev_t dev = d->base.dev;
    int res;

    if (value > 31)
        value = 31;

    res = dev_gpo_set(dev, IGPO_BANK_PGA_GAIN, 63 - (value << 1));
    if (res)
        return res;

    res = dev_gpo_set(dev, IGPO_BANK_PGA_LE, 0xF);
    if (res)
        return res;

    res = dev_gpo_set(dev, IGPO_BANK_PGA_LE, 0);
    if (res)
        return res;

    return 0;
}


int dev_m2_d09_4_ad45_2_sdr_fe_lo1(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)ud;
    ext_fe_100_5000_t* f;
    if ((f = (ext_fe_100_5000_t*)device_fe_to(d->fe, "fe1005000"))) {
        d->fe_lo1_khz = value / 1000;
        return ext_fe_100_5000_rf_manual(f, d->fe_lo1_khz, d->fe_lo2_khz, d->fe_band, d->fe_ifsel);
    }
    return -EINVAL;
}
int dev_m2_d09_4_ad45_2_sdr_fe_lo2(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)ud;
    ext_fe_100_5000_t* f;
    if ((f = (ext_fe_100_5000_t*)device_fe_to(d->fe, "fe1005000"))) {
        d->fe_lo2_khz = value / 1000;
        return ext_fe_100_5000_rf_manual(f, d->fe_lo1_khz, d->fe_lo2_khz, d->fe_band, d->fe_ifsel);
    }
    return -EINVAL;
}

int dev_m2_d09_4_ad45_2_sdr_fe_ifsel(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)ud;
    ext_fe_100_5000_t* f;
    if ((f = (ext_fe_100_5000_t*)device_fe_to(d->fe, "fe1005000"))) {
        d->fe_ifsel = value;
        return ext_fe_100_5000_rf_manual(f, d->fe_lo1_khz, d->fe_lo2_khz, d->fe_band, d->fe_ifsel);
    }
    return -EINVAL;
}

int dev_m2_d09_4_ad45_2_sdr_fe_band(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)ud;
    ext_fe_100_5000_t* f;
    if ((f = (ext_fe_100_5000_t*)device_fe_to(d->fe, "fe1005000"))) {
        d->fe_band = value;
        return ext_fe_100_5000_rf_manual(f, d->fe_lo1_khz, d->fe_lo2_khz, d->fe_band, d->fe_ifsel);
    }
    return -EINVAL;
}


int dev_m2_d09_4_ad45_2_gain_set(pdevice_t ud, pusdr_vfs_obj_t UNUSED obj, uint64_t value)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)ud;
    ext_fe_100_5000_t* f;
    if ((f = (ext_fe_100_5000_t*)device_fe_to(d->fe, "fe1005000"))) {

        USDR_LOG("LSDR", USDR_LOG_ERROR, "Gain: %d\n", (unsigned)value);
        return ext_fe_100_5000_set_attenuator(f, 127 - value * 4);

    } else {
        return dev_m2_d09_4_ad45_2_pga(d, value);
    }
}

int dev_m2_d09_4_ad45_2_gainpga_set(pdevice_t ud, pusdr_vfs_obj_t UNUSED obj, uint64_t value)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)ud;

    USDR_LOG("LSDR", USDR_LOG_ERROR, "GainPGA: %d\n", (unsigned)value);
    return dev_m2_d09_4_ad45_2_pga(d, value);
}

int dev_m2_d09_4_ad45_2_gainvga_set(pdevice_t ud, pusdr_vfs_obj_t UNUSED obj, uint64_t value)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)ud;
    ext_fe_100_5000_t* f;
    if ((f = (ext_fe_100_5000_t*)device_fe_to(d->fe, "fe1005000"))) {

        USDR_LOG("LSDR", USDR_LOG_ERROR, "GainVGA: %d\n", (unsigned)value);
        return ext_fe_100_5000_set_attenuator(f, 127 - value * 4);
    } else {
        return -EINVAL;
    }
}

int dev_m2_d09_4_ad45_2_gainlna_set(pdevice_t ud, pusdr_vfs_obj_t UNUSED obj, uint64_t value)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)ud;
    ext_fe_100_5000_t* f;
    if ((f = (ext_fe_100_5000_t*)device_fe_to(d->fe, "fe1005000"))) {
        USDR_LOG("LSDR", USDR_LOG_ERROR, "GainLNA: %d\n", (unsigned)value);
        return ext_fe_100_5000_set_lna(f, value);
    } else {
        return -EINVAL;
    }
}

int dev_m2_d09_4_ad45_2_rate_m_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    uint32_t *rates = (uint32_t *)(uintptr_t)value;
    uint32_t rx_rate = rates[0];
    // uint32_t tx_rate = rates[1];
    // uint32_t adc_rate = rates[2];
    // uint32_t dac_rate = rates[3];

    return dev_m2_d09_4_ad45_2_rate_set(ud, obj, rx_rate);
}

#define MAX_RATE 260e6

int dev_m2_d09_4_ad45_2_rate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)ud;
    lldev_t dev = d->base.dev;
    int res;

    unsigned rx_decims[] = { 2, 3, 4, 5, 6, 8, 16, 32, 64, 128 };
    unsigned i = 0;
    unsigned ii;

    for (ii = 0; ii < SIZEOF_ARRAY(rx_decims); ii++) {
        if (value * rx_decims[ii] < MAX_RATE)
            i = ii;
        else
            break;
    }

    d->adc_rate = value * rx_decims[i];
    d->rxbb_rate = value;
    d->rxbb_decim = rx_decims[i];

    res = si549_set_freq(dev, 0, I2C_BUS_SI549, d->adc_rate);
    if (res) {
        USDR_LOG("LSDR", USDR_LOG_ERROR, "Unable to set %d freq on si549!\n", d->adc_rate);
        return res;
    }

    USDR_LOG("LSDR", USDR_LOG_ERROR, "Decimation set to %d, ADC %d\n", d->rxbb_decim, d->adc_rate);

    res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_BANK_ADC_CHMSK, 0x0f);
    res = (res) ? res : fgearbox_load_fir(d->base.dev, IGPO_BANK_ADC_DSPCHAIN_PRG, (fgearbox_firs_t)d->rxbb_decim);
    if (res) {
        USDR_LOG("LSDR", USDR_LOG_ERROR, "Unable to initialize FIR gearbox, error = %d!\n", res);
        return res;
    }

    if (d->lmk04832_present) {
        //Turn on bypass mode
        lmk04832_layout_t lmkl;
        lmk04832_layout_out_t lmko;
        //lmkl.clk_route = LMK04832_CLKPATH_CLK1IN;
        lmkl.clk_route = LMK04832_CLKPATH_CLK0IN_PLL2; // Internal
        lmkl.distribution_frequency = value;
        lmkl.ext_vco_frequency = 0;
        lmkl.in_frequency = value;
        res = lmk04832_configure_layout(dev, 0, 2, &lmkl, &lmko);
        if (res)
            return res;

        lmk04832_dualport_config_t pcfg;
        pcfg.div_delay = 0;
        pcfg.divider = (lmko.vco_frequency / lmkl.distribution_frequency);
        pcfg.flags = 0;
        pcfg.portx_fmt = LMK04832_DP_LVDS;
        pcfg.porty_fmt = LMK04832_DP_POWERDOWN;
        // ADC0
        res = lmk04832_configure_dports(dev, 0, 2, LMK04832_PORT_CLK0_1, &pcfg);
        if (res)
            return res;
        // ADC1
        res = lmk04832_configure_dports(dev, 0, 2, LMK04832_PORT_CLK12_13, &pcfg);
        if (res)
            return res;
    }

    // TODO wait stble frequency!!!
    usleep(100000);

    if (d->lmk04832_present) {
        uint32_t stat;
        bool locked = false;
        res = dev_gpi_get32(dev, IGPI_BANK_LMK_STAT, &stat);
        if (res)
            return res;

        res = lmk04832_check_lock(dev, 0, 2, &locked);
        if (res)
            return res;

        USDR_LOG("0944", USDR_LOG_ERROR, "LMK_STAT: %08x, locked=%d\n", stat, locked);
    }

    // Reset Bitslip logic after changing frequency
    res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_BANK_CFG_ADC, 0xf0);
    res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_BANK_CFG_ADC, 0x00);
    res = (res) ? res : ads42lbx9_set_normal(dev, 0, SPI_ADS42LBX9_0, true, false);
    res = (res) ? res : ads42lbx9_set_normal(dev, 0, SPI_ADS42LBX9_1, false, false);
    res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_BANK_ADC_CHMSK, 0xf);
    res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_BANK_ADC_DSP_MIX2, 3);

    unsigned step =
         (value >= 320e6) ? 2 :
         (value >= 300e6) ? 3 :
         (value >= 290e6) ? 4 :
         (value >= 275e6) ? 5 :
         (value >= 160e6) ? 0 :
         (value >= 110e6) ? 6 :  0;
    res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_BANK_ADC_PHY_CTL_0, (1 << PHY_LVDS_CLK_LD_OFF) | step);
    //res = (res) ? res : ads42lbx9_set_pbrs(dev, 0, SPI_ADS42LBX9_0);
    //res = (res) ? res : ads42lbx9_set_pbrs(dev, 0, SPI_ADS42LBX9_1);
    res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_BANK_ADC_DSPCHAIN_RST, 0x1);
    usleep(10);
    res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_BANK_ADC_DSPCHAIN_RST, 0x0);

    return res;
}

int dev_m2_d09_4_ad45_2_debug_lldev_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)ud;
    lldev_t dev = d->base.dev;
    *ovalue = (intptr_t)dev;
    return 0;
}

int dev_m2_d09_4_ad45_2_senstemp_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)ud;
    lldev_t dev = d->base.dev;
    int temp, res;
    res = tmp108_temp_get(dev, 0, I2C_BUS_TMP108, &temp);
    if (res)
        return res;

    *ovalue = (int64_t)temp;
    return 0;
}

int dev_m2_d09_4_ad45_2_debug_all_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)ud;
    lldev_t dev = d->base.dev;
    int res;
    unsigned tmp;

    for (unsigned dreg = 0; dreg < 16; dreg++) {
        res = lowlevel_reg_rd32(dev, 0, dreg, &tmp);
        if (res)
            return res;
    }

    //dev_m2_d09_4_ad45_2_stat(ud, obj, ovalue);

    *ovalue = 0;
    return 0;
}

static int _lsdr_adc_activate(struct dev_m2_d09_4_ad45_2 *d, unsigned msk)
{
    int res;

    res = tps6594_vout_ctrl(d->base.dev, 0, I2C_BUS_TPS6594, TPS6594_LDO4, (msk != 0) ? true : false);
    if (res)
        return res;

    usleep(10000);

    // ADC Reset
    res = dev_gpo_set(d->base.dev, IGPO_BANK_CFG_ADC, 0x03);
    if (res)
        return res;
    usleep(1000);
    res = dev_gpo_set(d->base.dev, IGPO_BANK_CFG_ADC, 0xf0);
    if (res)
        return res;
    usleep(1000);

    res = dev_gpo_set(d->base.dev, IGPO_BANK_CFG_ADC, 0x03 ^ (msk & 3));
    if (res)
        return res;

    USDR_LOG("LSDR", USDR_LOG_ERROR, "ADC active mask %08x\n", msk);
    return 0;
}

int dev_m2_d09_4_ad45_2_pwren_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)ud;
    return _lsdr_adc_activate(d, value ? 3 : 0);
}

static int dev_m2_d09_4_ad45_2_debug_rxtime_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)ud;
    int res;
    unsigned tmp;

    res = lowlevel_reg_wr32(d->base.dev, 0,
                            CSR_RFE4_BASE, (3u<<28) | 2);
    if (res)
        return res;

    res = lowlevel_reg_rd32(d->base.dev, 0, 9, &tmp);
    if (res)
        return res;

    *ovalue = tmp;
    return 0;
}


static
void usdr_device_m2_d09_4_ad45_2_destroy(pdevice_t udev)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)udev;
    lldev_t dev = d->base.dev;

    si549_enable(dev, 0, I2C_BUS_SI549, false);
    tps6594_vout_ctrl(dev, 0, I2C_BUS_TPS6594, TPS6594_LDO4, false);

    ads42lbx9_ctrl(dev, 0, SPI_ADS42LBX9_0, false, false, true, false);
    ads42lbx9_ctrl(dev, 0, SPI_ADS42LBX9_1, false, false, true, false);

    usdr_device_base_destroy(udev);
}

static
int usdr_device_m2_d09_4_ad45_2_initialize(pdevice_t udev, unsigned pcount, const char** devparam, const char** devval)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)udev;
    lldev_t dev = d->base.dev;
    int res;
    const char* fe = NULL;
    bool try_lmk = false;
    d->decim = 2;

    for (unsigned i = 0; i < pcount; i++) {
        if (strcmp(devparam[i], "lmk") == 0) {
            try_lmk = atoi(devval[i]) == 0 ? false : true;
        }
        if (strcmp(devparam[i], "decim") == 0) {
            int dv = atoi(devval[i]);
            if (dv != 128)
                dv = 2;

            d->decim = dv;
        }
        if (strcmp(devparam[i], "fe") == 0) {
            fe = devval[i];
        }
    }

    d->lmk04832_present = false;
    d->dac38rf_present = false;
    d->adc_present[0] = false;
    d->adc_present[1] = false;

    if (getenv("USDR_BARE_DEV")) {
        USDR_LOG("XDEV", USDR_LOG_WARNING, "USDR_BARE_DEV is set, skipping initialization!\n");
        return 0;
    }

    uint32_t data = 0xdeaddead;
    lowlevel_reg_rd32(dev, 0, 0, &data);
    if (data == 0xffffffff) {
        USDR_LOG("XXXX", USDR_LOG_ERROR, "Device is dead!\n");
        return -EIO;
    }

    res = tps6594_check(dev, 0, I2C_BUS_TPS6594);
    if (res)
        return res;

    res = tps6594_vout_set(dev, 0, I2C_BUS_TPS6594, TPS6594_BUCK1, 920);
    if (res)
        return res;

    res = tps6594_vout_set(dev, 0, I2C_BUS_TPS6594, TPS6594_BUCK2, 920);
    if (res)
        return res;

    res = tps6594_vout_set(dev, 0, I2C_BUS_TPS6594, TPS6594_BUCK3, 2200);
    if (res)
        return res;

    res = tps6594_vout_set(dev, 0, I2C_BUS_TPS6594, TPS6594_BUCK4, 1200);
    if (res)
        return res;

    res = tps6594_vout_ctrl(dev, 0, I2C_BUS_TPS6594, TPS6594_LDO4, false);
    if (res)
        return res;

    usleep(1000);

    res = si549_enable(dev, 0, I2C_BUS_SI549, false);
    if (res)
        return res;

    // FIXUP BEGIN
    usleep(1000);
    res = tps6594_vout_ctrl(dev, 0, I2C_BUS_TPS6594, TPS6594_LDO4, true);
    if (res)
        return res;
    usleep(2500);
    // FIXUP END

    // check LMK04832 and DAC38RFxx
    if (try_lmk) {
        res = lmk04832_reset_and_test(dev, 0, SPI_LMK04832);
        if (res == 0)
            d->lmk04832_present = true;
        else if (res != -ENODEV)
            return res;
    }

    // check ADC present
    unsigned devs[] = { SPI_ADS42LBX9_0, SPI_ADS42LBX9_1 };

    /*
    res = dev_gpo_set(d->base.dev, IGPO_BANK_CFG_ADC, 3);
    usleep(1000);
    res = dev_gpo_set(d->base.dev, IGPO_BANK_CFG_ADC, 0);
    usleep(10000);
    */

    for (unsigned j = 0; j < SIZEOF_ARRAY(devs); j++) {
        res = ads42lbx9_reset_and_check(dev, 0, devs[j]);
        if (res == 0)
            d->adc_present[j] = true;
        else if (res != -ENODEV)
            return res;
    }


    USDR_LOG("0944", USDR_LOG_ERROR, "Configuration: LMK=%d DAC=%d AD0=%d AD1=%d\n",
             d->lmk04832_present, d->dac38rf_present, d->adc_present[0], d->adc_present[1]);

    // FIXUP BEGIN
    res = tps6594_vout_ctrl(dev, 0, I2C_BUS_TPS6594, TPS6594_LDO4, false);
    if (res)
        return res;
    // FIXUP END


    // Init FE
    res = device_fe_probe(udev, "lsdr", fe, 0, &d->fe);
    if (res) {
        return res;
    }

    return 0;
}

// 2 & 3 are disable now, so ignore them
static const channel_map_info_t s_lsdr_chmap[] = {
    { "0a", 0 },
    { "0b", 1 },
    // { "1a", 2 },
    // { "1b", 3 },
    { "a", 0 },
    { "b", 1 },
    // { "c", 2 },
    // { "d", 3 },
    { NULL, CH_NULL },
};

int lsdr_map_channels(const usdr_channel_info_t* channels, channel_info_t* core_chans)
{
    return usdr_channel_info_map_default(channels, s_lsdr_chmap, 2, core_chans);
}

static
int usdr_device_m2_d09_4_ad45_2_create_stream(device_t* dev, const char* sid, const char* dformat,
                                                     const usdr_channel_info_t* channels, unsigned pktsyms,
                                                     unsigned flags, const char* parameters, stream_handle_t** out_handle)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)dev;
    int res = -EINVAL;
    unsigned hwchs;
    channel_info_t lchans;

    res = lsdr_map_channels(channels, &lchans);
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

        // TODO: acticate/deactivate ADC
        _lsdr_adc_activate(d, 3);

        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_BANK_ADC_DSPCHAIN_RST, 0x1);
        usleep(1000);
        res = (res) ? res : dev_gpo_set(d->base.dev, IGPO_BANK_ADC_DSPCHAIN_RST, 0x0);


        res = (res) ? res : create_sfetrx4_stream(dev, CORE_SFERX_DMA32_R0, dformat, channels->count, &lchans, pktsyms,
                                    flags, M2PCI_REG_WR_RXDMA_CONFIRM, VIRT_CFG_SFX_BASE, 0,
                                    SRF4_FIFOBSZ, CSR_RFE4_BASE, &d->rx, &hwchs);
        if (res) {
            return res;
        }

        *out_handle = d->rx;
    }
    return res;
}

static
int usdr_device_m2_d09_4_ad45_2_unregister_stream(device_t* dev, stream_handle_t* stream)
{
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)dev;
    if (stream == d->rx) {
        d->rx->ops->destroy(d->rx);
        d->rx = NULL;
    } else {
        return -EINVAL;
    }
    return 0;
}

static
int usdr_device_m2_d09_4_ad45_2_create(lldev_t dev, device_id_t devid)
{
    int res;
    struct dev_m2_d09_4_ad45_2 *d = (struct dev_m2_d09_4_ad45_2 *)malloc(sizeof(struct dev_m2_d09_4_ad45_2));
    res = usdr_device_base_create(&d->base, dev);
    if (res) {
        goto failed_free;
    }

    res = vfs_add_const_i64_vec(&d->base.rootfs,
                                s_params_m2_da09_4_ad45_2_rev000,
                                SIZEOF_ARRAY(s_params_m2_da09_4_ad45_2_rev000));
    if (res)
        goto failed_tree_creation;

    res = usdr_vfs_obj_param_init_array(&d->base,
                                        s_fparams_m2_da09_4_ad45_2_rev000,
                                        SIZEOF_ARRAY(s_fparams_m2_da09_4_ad45_2_rev000));
    if (res)
        goto failed_tree_creation;

    d->base.initialize = &usdr_device_m2_d09_4_ad45_2_initialize;
    d->base.destroy = &usdr_device_m2_d09_4_ad45_2_destroy;
    d->base.create_stream = &usdr_device_m2_d09_4_ad45_2_create_stream;
    d->base.unregister_stream = &usdr_device_m2_d09_4_ad45_2_unregister_stream;
    d->base.timer_op = &sfetrx4_stream_sync;
    d->active_adc = 0;
    d->decim = 2;
    d->rx = NULL;
    d->tx = NULL;

    d->fe_lo1_khz = 0;
    d->fe_lo2_khz = 0;
    d->fe_ifsel = 0;
    d->fe_band = 0;

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
    usdr_device_m2_d09_4_ad45_2_create,
};


int usdr_device_register_m2_d09_4_ad45_2()
{
    return usdr_device_register(s_uuid_000, &s_ops);
}
