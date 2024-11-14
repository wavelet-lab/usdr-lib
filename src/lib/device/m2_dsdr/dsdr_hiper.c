// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <usdr_port.h>
#include <usdr_lowlevel.h>
#include <usdr_logging.h>

#include "../hw/tmp114/tmp114.h"
#include "../hw/dac80501/dac80501.h"
#include "../hw/tca6424a/tca6424a.h"
#include "../hw/adf4002b/adf4002b.h"

#include "../ipblks/uart.h"
#include "../ipblks/spiext.h"

#include "../device_vfs.h"
#include "../device_names.h"
#include "../device_cores.h"

#include "../generic_usdr/generic_regs.h"

#include "dsdr_hiper.h"

static int dev_gpo_set(lldev_t dev, unsigned bank, unsigned data)
{
    return lowlevel_reg_wr32(dev, 0, 0, ((bank & 0x7f) << 24) | (data & 0xff));
}

static int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, 0, 16 + (bank / 4), data);
}

enum osc_defaults {
    DEF_OSC_GPS_FREQ = 25000000, // U82
    DEF_OSC_INT_FREQ = 40000000, // VCO1/VCO2
};

enum {
    IGPO_OFF_ADDR = 4,
    IGPO_OFF_YAML = 16,

    IGPO_FE_REG_COUNT = 9,
};

enum {
    TCA6424A_ADDR_L = 0x22,
    TCA6424A_ADDR_H = 0x23,
};

enum i2c_idx_extra {
    I2C_TCA6424AR_U114 = MAKE_LSOP_I2C_ADDR(1, 0, TCA6424A_ADDR_L),
    I2C_TCA6424AR_U113 = MAKE_LSOP_I2C_ADDR(1, 0, TCA6424A_ADDR_H),
    I2C_TCA6424AR_U115 = MAKE_LSOP_I2C_ADDR(1, 1, TCA6424A_ADDR_H),

    I2C_TEMP_U69 = MAKE_LSOP_I2C_ADDR(1, 0, I2C_DEV_TMP114NB),
    I2C_TEMP_U70 = MAKE_LSOP_I2C_ADDR(1, 1, I2C_DEV_TMP114NB),
    I2C_TEMP_U71 = MAKE_LSOP_I2C_ADDR(0, 1, I2C_DEV_TMP114NB),

    I2C_DAC      = MAKE_LSOP_I2C_ADDR(1, 1, I2C_DEV_DAC80501M_A0_GND),
};

enum spi_idx {
    SPI_LMS8001B_U1_RX_AB_IDX = 0,
    SPI_LMS8001B_U2_RX_CD_IDX = 1,
    SPI_LMS8001A_U3_RX_AB_IDX = 2,
    SPI_LMS8001A_U4_RX_CD_IDX = 3,
    SPI_LMS8001B_U5_TX_AB_IDX = 4,
    SPI_LMS8001B_U6_TX_CD_IDX = 5,
    SPI_ADF4002 = 7,
};

static const char* s_lms8_names[] = {
    "RX_H_AB", "RX_H_CD", "RX_L_AB", "RX_L_CD", "TX_AB", "TX_CD"
};

enum spi_cfg {
    LMS8_DIV = 10,
    ADF4002_DIV = 16, //50 ns Cycle

    LMS8_BCNTZ = 3,
    ADF4002_BCNTZ = 2,

    SPI_ADF4002_CFG = MAKE_SPIEXT_CFG(ADF4002_BCNTZ, SPI_ADF4002, ADF4002_DIV),
};




static int dsdr_hiper_debug_lms8001_u1_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dsdr_hiper_debug_lms8001_u1_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dsdr_hiper_debug_lms8001_u2_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dsdr_hiper_debug_lms8001_u2_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dsdr_hiper_debug_lms8001_u3_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dsdr_hiper_debug_lms8001_u3_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dsdr_hiper_debug_lms8001_u4_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dsdr_hiper_debug_lms8001_u4_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dsdr_hiper_debug_lms8001_u5_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dsdr_hiper_debug_lms8001_u5_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dsdr_hiper_debug_lms8001_u6_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dsdr_hiper_debug_lms8001_u6_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dsdr_hiper_debug_lms8001_u6_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dsdr_hiper_debug_lms8001_u6_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dsdr_hiper_dsdr_hiper_ctrl_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dsdr_hiper_dsdr_hiper_ctrl_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dsdr_hiper_dsdr_hiper_exp_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dsdr_hiper_dsdr_hiper_exp_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);


static int dsdr_hiper_sens0temp_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue);
static int dsdr_hiper_sens1temp_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue);
static int dsdr_hiper_sens2temp_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue);


static int dsdr_hiper_lms8001_rabl_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dsdr_hiper_lms8001_rabl_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dsdr_hiper_lms8001_rcdl_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dsdr_hiper_lms8001_rcdl_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dsdr_hiper_lms8001_rabh_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dsdr_hiper_lms8001_rabh_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dsdr_hiper_lms8001_rcdh_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dsdr_hiper_lms8001_rcdh_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dsdr_hiper_lms8001_tab_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dsdr_hiper_lms8001_tab_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dsdr_hiper_lms8001_tcd_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dsdr_hiper_lms8001_tcd_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dsdr_hiper_meas_clk40_int_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dsdr_hiper_meas_clk40_pps_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dsdr_hiper_meas_adfmux_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);


static int dsdr_hiper_dacvctcxo_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dsdr_hiper_dacvctcxo_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int dsdr_hiper_adf4002b_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dsdr_hiper_adf4002b_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static const usdr_dev_param_func_t s_fe_parameters[] = {
    { "/debug/hw/lms8001/0/reg" ,  { dsdr_hiper_debug_lms8001_u1_reg_set, dsdr_hiper_debug_lms8001_u1_reg_get }},
    { "/debug/hw/lms8001/1/reg" ,  { dsdr_hiper_debug_lms8001_u2_reg_set, dsdr_hiper_debug_lms8001_u2_reg_get }},
    { "/debug/hw/lms8001/2/reg" ,  { dsdr_hiper_debug_lms8001_u3_reg_set, dsdr_hiper_debug_lms8001_u3_reg_get }},
    { "/debug/hw/lms8001/3/reg" ,  { dsdr_hiper_debug_lms8001_u4_reg_set, dsdr_hiper_debug_lms8001_u4_reg_get }},
    { "/debug/hw/lms8001/4/reg" ,  { dsdr_hiper_debug_lms8001_u5_reg_set, dsdr_hiper_debug_lms8001_u5_reg_get }},
    { "/debug/hw/lms8001/5/reg" ,  { dsdr_hiper_debug_lms8001_u6_reg_set, dsdr_hiper_debug_lms8001_u6_reg_get }},

    { "/debug/hw/dsdr_hiper_ctrl/0/reg", { dsdr_hiper_dsdr_hiper_ctrl_reg_set, dsdr_hiper_dsdr_hiper_ctrl_reg_get }  },
    { "/debug/hw/dsdr_hiper_exp/0/reg" , { dsdr_hiper_dsdr_hiper_exp_reg_set, dsdr_hiper_dsdr_hiper_exp_reg_get }  },

    { "/dm/sensor/temp1",          { NULL, dsdr_hiper_sens0temp_get }},
    { "/dm/sensor/temp2",          { NULL, dsdr_hiper_sens1temp_get }},
    { "/dm/sensor/temp3",          { NULL, dsdr_hiper_sens2temp_get }},

    { "/dm/sdr/0/rx/ab_l/freqency", { dsdr_hiper_lms8001_rabl_reg_set, dsdr_hiper_lms8001_rabl_reg_get }},
    { "/dm/sdr/0/rx/cd_l/freqency", { dsdr_hiper_lms8001_rcdl_reg_set, dsdr_hiper_lms8001_rcdl_reg_get }},
    { "/dm/sdr/0/rx/ab_h/freqency", { dsdr_hiper_lms8001_rabh_reg_set, dsdr_hiper_lms8001_rabh_reg_get }},
    { "/dm/sdr/0/rx/cd_h/freqency", { dsdr_hiper_lms8001_rcdh_reg_set, dsdr_hiper_lms8001_rcdh_reg_get }},
    { "/dm/sdr/0/tx/ab/freqency", { dsdr_hiper_lms8001_tab_reg_set, dsdr_hiper_lms8001_tab_reg_get }},
    { "/dm/sdr/0/tx/cd/freqency", { dsdr_hiper_lms8001_tcd_reg_set, dsdr_hiper_lms8001_tcd_reg_get }},

    { "/dm/sdr/0/clk40_int",       { NULL, dsdr_hiper_meas_clk40_int_get }},
    { "/dm/sdr/0/clk40_pps",       { NULL, dsdr_hiper_meas_clk40_pps_get }},
    { "/dm/sdr/0/clkadfmux",       { NULL, dsdr_hiper_meas_adfmux_get }},

    { "/dm/sdr/0/dacvctcxo",       { dsdr_hiper_dacvctcxo_set, dsdr_hiper_dacvctcxo_get }},

    { "/debug/hw/adf4002b/0/reg",  { dsdr_hiper_adf4002b_reg_set, dsdr_hiper_adf4002b_reg_get }  },

};

int dsdr_hiper_adf4002b_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    dsdr_hiper_fe_t* hiper = (dsdr_hiper_fe_t*)obj->object;
    int res;
    unsigned addr = (value >> 24) & 0x3;
    unsigned data = value & 0xfffffc;

    hiper->debug_adf4002_reg_last = ~0u;

    if (value & 0x80000000) {
        USDR_LOG("HIPR", USDR_LOG_WARNING, "AFD4002B %08x => %08x\n", addr, data);
        res = adf4002b_reg_set(hiper->dev, hiper->subdev, hiper->adf4002_spiidx, addr, data);
        if (res)
            return res;

        hiper->adf4002_regs[addr] = value;
    } else {
        hiper->debug_adf4002_reg_last = hiper->adf4002_regs[addr];
    }
    return 0;
}

int dsdr_hiper_adf4002b_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dsdr_hiper_fe_t* hiper = (dsdr_hiper_fe_t*)obj->object;
    *ovalue = hiper->debug_adf4002_reg_last;
    return 0;
}

int dsdr_hiper_dacvctcxo_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    dsdr_hiper_fe_t* hiper = (dsdr_hiper_fe_t*)obj->object;
    return dac80501_dac_set(hiper->dev, hiper->subdev, I2C_DAC, value);
}

int dsdr_hiper_dacvctcxo_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dsdr_hiper_fe_t* hiper = (dsdr_hiper_fe_t*)obj->object;
    uint16_t v = 0;
    int res = dac80501_dac_get(hiper->dev, hiper->subdev, I2C_DAC, &v);
    *ovalue = v;
    return res;
}


static int dsdr_hiper_fe_lms8_set_lo(dsdr_hiper_fe_t* fe, unsigned idx, uint64_t freq)
{
    int res = lms8001_tune(&fe->lms8[idx], fe->ref_int_osc, freq);
    USDR_LOG("HIPR", USDR_LOG_WARNING, "HIPER_LMS8_%s: [%d] tune to %.3f Mhz result %d\n",
             s_lms8_names[idx], idx, freq / 1.0e6, res);
    if (res == 0) {
        fe->lo_lms8_freq[idx] = freq;
    }
    return res;
}

static int dsdr_hiper_fe_lms8_get_lo(dsdr_hiper_fe_t* fe, unsigned idx, uint64_t* ofreq)
{
    *ofreq = fe->lo_lms8_freq[idx];
    return 0;
}

int dsdr_hiper_lms8001_rabl_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return dsdr_hiper_fe_lms8_set_lo((dsdr_hiper_fe_t*)obj->object, SPI_LMS8001A_U3_RX_AB_IDX, value);
}
int dsdr_hiper_lms8001_rcdl_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return dsdr_hiper_fe_lms8_set_lo((dsdr_hiper_fe_t*)obj->object, SPI_LMS8001A_U4_RX_CD_IDX, value);
}
int dsdr_hiper_lms8001_rabh_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return dsdr_hiper_fe_lms8_set_lo((dsdr_hiper_fe_t*)obj->object, SPI_LMS8001B_U1_RX_AB_IDX, value);
}
int dsdr_hiper_lms8001_rcdh_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return dsdr_hiper_fe_lms8_set_lo((dsdr_hiper_fe_t*)obj->object, SPI_LMS8001B_U2_RX_CD_IDX, value);
}
int dsdr_hiper_lms8001_tab_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return dsdr_hiper_fe_lms8_set_lo((dsdr_hiper_fe_t*)obj->object, SPI_LMS8001B_U5_TX_AB_IDX, value);
}
int dsdr_hiper_lms8001_tcd_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return dsdr_hiper_fe_lms8_set_lo((dsdr_hiper_fe_t*)obj->object, SPI_LMS8001B_U6_TX_CD_IDX, value);
}

int dsdr_hiper_lms8001_rabl_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    return dsdr_hiper_fe_lms8_get_lo((dsdr_hiper_fe_t*)obj->object, SPI_LMS8001A_U3_RX_AB_IDX, ovalue);
}
int dsdr_hiper_lms8001_rcdl_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    return dsdr_hiper_fe_lms8_get_lo((dsdr_hiper_fe_t*)obj->object, SPI_LMS8001A_U4_RX_CD_IDX, ovalue);
}
int dsdr_hiper_lms8001_rabh_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    return dsdr_hiper_fe_lms8_get_lo((dsdr_hiper_fe_t*)obj->object, SPI_LMS8001B_U1_RX_AB_IDX, ovalue);
}
int dsdr_hiper_lms8001_rcdh_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    return dsdr_hiper_fe_lms8_get_lo((dsdr_hiper_fe_t*)obj->object, SPI_LMS8001B_U2_RX_CD_IDX, ovalue);
}
int dsdr_hiper_lms8001_tab_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    return dsdr_hiper_fe_lms8_get_lo((dsdr_hiper_fe_t*)obj->object, SPI_LMS8001B_U5_TX_AB_IDX, ovalue);
}
int dsdr_hiper_lms8001_tcd_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    return dsdr_hiper_fe_lms8_get_lo((dsdr_hiper_fe_t*)obj->object, SPI_LMS8001B_U6_TX_CD_IDX, ovalue);
}


static int dsdr_hiper_fe_lms8_reg_set(dsdr_hiper_fe_t* fe, unsigned idx, uint64_t value)
{
    uint32_t v = value;
    int res = lowlevel_spi_tr32(fe->dev, fe->subdev, fe->lms8[idx].lsaddr, v, &fe->debug_lms8001_last[idx]);

    USDR_LOG("HIPR", USDR_LOG_WARNING, "%s: Debug LMS8[%d] REG %08x => %08x\n",
             lowlevel_get_devname(fe->dev), idx, (unsigned)value,
             fe->debug_lms8001_last[idx]);
    return res;
}

static int dsdr_hiper_fe_lms8_reg_get(dsdr_hiper_fe_t* fe, unsigned idx, uint64_t* ovalue)
{
    *ovalue = fe->debug_lms8001_last[idx];
    return 0;
}

int dsdr_hiper_debug_lms8001_u1_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return dsdr_hiper_fe_lms8_reg_set((dsdr_hiper_fe_t*)obj->object, 0, value);
}
int dsdr_hiper_debug_lms8001_u2_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return dsdr_hiper_fe_lms8_reg_set((dsdr_hiper_fe_t*)obj->object, 1, value);
}
int dsdr_hiper_debug_lms8001_u3_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return dsdr_hiper_fe_lms8_reg_set((dsdr_hiper_fe_t*)obj->object, 2, value);
}
int dsdr_hiper_debug_lms8001_u4_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return dsdr_hiper_fe_lms8_reg_set((dsdr_hiper_fe_t*)obj->object, 3, value);
}
int dsdr_hiper_debug_lms8001_u5_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return dsdr_hiper_fe_lms8_reg_set((dsdr_hiper_fe_t*)obj->object, 4, value);
}
int dsdr_hiper_debug_lms8001_u6_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return dsdr_hiper_fe_lms8_reg_set((dsdr_hiper_fe_t*)obj->object, 5, value);
}

int dsdr_hiper_debug_lms8001_u1_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    return dsdr_hiper_fe_lms8_reg_get((dsdr_hiper_fe_t*)obj->object, 0, ovalue);
}
int dsdr_hiper_debug_lms8001_u2_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    return dsdr_hiper_fe_lms8_reg_get((dsdr_hiper_fe_t*)obj->object, 1, ovalue);
}
int dsdr_hiper_debug_lms8001_u3_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    return dsdr_hiper_fe_lms8_reg_get((dsdr_hiper_fe_t*)obj->object, 2, ovalue);
}
int dsdr_hiper_debug_lms8001_u4_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    return dsdr_hiper_fe_lms8_reg_get((dsdr_hiper_fe_t*)obj->object, 3, ovalue);
}
int dsdr_hiper_debug_lms8001_u5_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    return dsdr_hiper_fe_lms8_reg_get((dsdr_hiper_fe_t*)obj->object, 4, ovalue);
}
int dsdr_hiper_debug_lms8001_u6_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    return dsdr_hiper_fe_lms8_reg_get((dsdr_hiper_fe_t*)obj->object, 5, ovalue);
}


int dsdr_hiper_dsdr_hiper_ctrl_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    dsdr_hiper_fe_t* hiper = (dsdr_hiper_fe_t*)obj->object;
    int res;
    unsigned addr = (value >> 24) & 0x7f;
    unsigned data = value & 0xffffff;

    hiper->debug_fe_reg_last = ~0u;

    if (value & 0x80000000) {
        USDR_LOG("HIPR", USDR_LOG_WARNING, "HIPER_CTRL %08x => %08x\n", addr, data);

        if (addr >= IGPO_OFF_YAML && addr < IGPO_OFF_YAML + IGPO_FE_REG_COUNT) {
            hiper->fe_gpo_regs[addr - IGPO_OFF_YAML] = data;

            res = dev_gpo_set(hiper->dev, addr - IGPO_OFF_YAML + IGPO_OFF_ADDR, data);
            USDR_LOG("HIPR", USDR_LOG_WARNING, "%s: Debug IGPO WR REG %04x => %04x\n",
                     lowlevel_get_devname(hiper->dev), (unsigned)addr, data);
        } else {
            return -EINVAL;
        }
    } else {
        if (addr >= IGPO_OFF_YAML && addr < IGPO_OFF_YAML + IGPO_FE_REG_COUNT) {
            hiper->debug_fe_reg_last = hiper->fe_gpo_regs[addr - IGPO_OFF_YAML];
            res = 0;
        } else {
            return -EINVAL;
        }
    }

    return res;
}
int dsdr_hiper_dsdr_hiper_ctrl_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dsdr_hiper_fe_t* hiper = (dsdr_hiper_fe_t*)obj->object;
    *ovalue = hiper->debug_fe_reg_last;
    return 0;
}


int dsdr_hiper_dsdr_hiper_exp_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    dsdr_hiper_fe_t* hiper = (dsdr_hiper_fe_t*)obj->object;
    int res;
    unsigned addr = (value >> 24) & 0x7f;
    unsigned data = value & 0xffffff;

    hiper->debug_exp_reg_last = ~0u;

    if (value & 0x80000000) {
        USDR_LOG("HIPR", USDR_LOG_WARNING, "HIPER_EXP WR %08x => %08x\n", addr, data);

        switch (addr) {
        case 0x20:
            res = tca6424a_reg16_set(hiper->dev, hiper->subdev, I2C_TCA6424AR_U114, TCA6424_OUT0, data);
            res = tca6424a_reg8_set(hiper->dev, hiper->subdev, I2C_TCA6424AR_U114, TCA6424_OUT0 + 2, data >> 16);
            break;

        case 0x21:
        case 0x22:
        case 0x23:
            res = tca6424a_reg8_set(hiper->dev, hiper->subdev, I2C_TCA6424AR_U113, TCA6424_OUT0 + (addr - 0x21), data);
            break;

        case 0x24:
        case 0x25:
        case 0x26:
            res = tca6424a_reg8_set(hiper->dev, hiper->subdev, I2C_TCA6424AR_U115, TCA6424_OUT0 + (addr - 0x24), data);
            break;
        default:
            return -EINVAL;
        }
    } else {
        uint8_t di8;
        uint16_t di16;

        switch (addr) {
        case 0x20:
            res = tca6424a_reg16_get(hiper->dev, hiper->subdev, I2C_TCA6424AR_U114, TCA6424_OUT0, &di16);
            res = tca6424a_reg8_get(hiper->dev, hiper->subdev, I2C_TCA6424AR_U114, TCA6424_OUT0 + 2, &di8);

            hiper->debug_exp_reg_last = di16 | ((unsigned)di8 << 16);

            USDR_LOG("HIPR", USDR_LOG_WARNING, "HIPER_EXP RD %08x => %08x\n", addr, hiper->debug_exp_reg_last);
            return res;

        case 0x21:
        case 0x22:
        case 0x23:
            res = tca6424a_reg8_get(hiper->dev, hiper->subdev, I2C_TCA6424AR_U113, TCA6424_OUT0 + (addr - 0x21), &di8);
            break;

        case 0x24:
        case 0x25:
        case 0x26:
            res = tca6424a_reg8_get(hiper->dev, hiper->subdev, I2C_TCA6424AR_U115, TCA6424_OUT0 + (addr - 0x24), &di8);
            break;
        default:
            return -EINVAL;
        }

        USDR_LOG("HIPR", USDR_LOG_WARNING, "HIPER_EXP RD %08x => %08x\n", addr, di8);
        hiper->debug_exp_reg_last = di8;
    }

    return res;
}
int dsdr_hiper_dsdr_hiper_exp_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dsdr_hiper_fe_t* hiper = (dsdr_hiper_fe_t*)obj->object;
    *ovalue = hiper->debug_exp_reg_last;
    return 0;
}

int dsdr_hiper_sens_get(dsdr_hiper_fe_t* hiper, unsigned idx, uint64_t *ovalue)
{
    unsigned addr[] = { I2C_TEMP_U71, I2C_TEMP_U70, I2C_TEMP_U69 };
    int temp256 = 127*256, res;
    if (idx >= SIZEOF_ARRAY(addr))
        return -EINVAL;

    res = tmp114_temp_get(hiper->dev, hiper->subdev, addr[idx], &temp256);
    *ovalue = (int64_t)temp256;
    return res;
}

int dsdr_hiper_sens0temp_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue)
{
    return dsdr_hiper_sens_get((dsdr_hiper_fe_t*)obj->object, 0, ovalue);
}
int dsdr_hiper_sens1temp_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue)
{
    return dsdr_hiper_sens_get((dsdr_hiper_fe_t*)obj->object, 1, ovalue);
}
int dsdr_hiper_sens2temp_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue)
{
    return dsdr_hiper_sens_get((dsdr_hiper_fe_t*)obj->object, 2, ovalue);
}



static int dsdr_hiper_initialize_lms8(dsdr_hiper_fe_t* dfe, unsigned addr, lms8001_state_t* obj)
{
    uint32_t chipver = ~0;
    int res = 0;
    res = res ? res : lowlevel_spi_tr32(dfe->dev, dfe->subdev, addr, 0x800000ff, &chipver);
    res = res ? res : lowlevel_spi_tr32(dfe->dev, dfe->subdev, addr, 0x000f0000, &chipver);
    USDR_LOG("HIPR", USDR_LOG_WARNING, "LMS8001.%08x: version %08x\n", addr, chipver);

    res = res ? res : lms8001_create(dfe->dev, dfe->subdev, addr, obj);

    res = res ? res : lms8001_ch_enable(obj, 0xc);
    return res;
}
#include <stdio.h>
int dsdr_hiper_fe_create(lldev_t dev, unsigned int spix_num, dsdr_hiper_fe_t* dfe)
{
    int res = 0;
    device_t* base = lowlevel_get_device(dev);
    dfe->dev = dev;
    dfe->subdev = 0;

    USDR_LOG("HIPR", USDR_LOG_WARNING, "Initializing HIPER front end...\n");
    dfe->ref_int_osc = DEF_OSC_INT_FREQ;
    dfe->ref_gps_osc = DEF_OSC_GPS_FREQ;

    // Reset FE registers
    for (unsigned k = 0; k < IGPO_FE_REG_COUNT; k++) {
        res = res ? res : dev_gpo_set(dev, IGPO_OFF_ADDR + k, 0);
    }

    res = res ? res : tca6424a_reg16_set(dev, dfe->subdev, I2C_TCA6424AR_U114, TCA6424_OUT0, 0);
    res = res ? res : tca6424a_reg16_set(dev, dfe->subdev, I2C_TCA6424AR_U113, TCA6424_OUT0, 0);
    res = res ? res : tca6424a_reg16_set(dev, dfe->subdev, I2C_TCA6424AR_U115, TCA6424_OUT0, 0);
    res = res ? res : tca6424a_reg8_set(dev, dfe->subdev, I2C_TCA6424AR_U114, TCA6424_OUT0 + 2, 0);
    res = res ? res : tca6424a_reg8_set(dev, dfe->subdev, I2C_TCA6424AR_U113, TCA6424_OUT0 + 2, 0);
    res = res ? res : tca6424a_reg8_set(dev, dfe->subdev, I2C_TCA6424AR_U115, TCA6424_OUT0 + 2, 0);

    res = res ? res : tca6424a_reg16_set(dev, dfe->subdev, I2C_TCA6424AR_U114, TCA6424_CFG0, 0);
    res = res ? res : tca6424a_reg16_set(dev, dfe->subdev, I2C_TCA6424AR_U113, TCA6424_CFG0, 0);
    res = res ? res : tca6424a_reg16_set(dev, dfe->subdev, I2C_TCA6424AR_U115, TCA6424_CFG0, 0);
    res = res ? res : tca6424a_reg8_set(dev, dfe->subdev, I2C_TCA6424AR_U114, TCA6424_CFG0 + 2, 0);
    res = res ? res : tca6424a_reg8_set(dev, dfe->subdev, I2C_TCA6424AR_U113, TCA6424_CFG0 + 2, 0);
    res = res ? res : tca6424a_reg8_set(dev, dfe->subdev, I2C_TCA6424AR_U115, TCA6424_CFG0 + 2, (1 << 0) | (1 << 2));
    // TODO: sanity check

    // Reset all LMS8001
    res = res ? res : tca6424a_reg16_set(dev, dfe->subdev, I2C_TCA6424AR_U115, TCA6424_OUT0, 0xffff);
    res = res ? res : tca6424a_reg8_set(dev, dfe->subdev, I2C_TCA6424AR_U115, TCA6424_OUT0 + 2, 0xff);

    if (res)
        return res;


    // LMS8
    for (unsigned k = 0; k < 6; k++) {
        uint32_t cfg = MAKE_SPIEXT_LSOPADR(MAKE_SPIEXT_CFG(LMS8_BCNTZ, k, LMS8_DIV), 0, spix_num);
        res = res ? res : dsdr_hiper_initialize_lms8(dfe, cfg, &dfe->lms8[k]);
    }

    // ADF4002 (MUX -> GND -> DVDD readback as a sanity check)
    // res = res ? res : lowlevel_spi_tr32(dfe->dev, dfe->subdev, MAKE_SPIEXT_LSOPADR(SPI_ADF4002_CFG, 0, spix_num), 0x33, NULL);

    usleep(10000);

    // fRAKON = fREF * N / R
    // RF_IN_a/b <= 40Mhz                     N-cntr
    // REF_IN    <= 25Mhz (GPS-disciplined)   R-cntr
    // TODO: find rational numbers for R/N
    dfe->adf4002_regs[0] = (1 << 20) | (5 << 2); // R-counter to 5
    dfe->adf4002_regs[1] = (0 << 21) | (8 << 8); // N-counter to 8
    dfe->adf4002_regs[2] = (3 << 18) | (3 << 15) | (8 << 11) | (1 << 7) | (1 << 4); // Digital lock detect
    dfe->adf4002_regs[3] = (3 << 18) | (3 << 15) | (8 << 11) | (1 << 7) | (1 << 4);
    dfe->adf4002_spiidx = MAKE_SPIEXT_LSOPADR(SPI_ADF4002_CFG, 0, spix_num);
    for (unsigned k = 3; k < 4; k--) {
        //res = res ? res : lowlevel_spi_tr32(dfe->dev, dfe->subdev, dfe->adf4002_spiidx, dfe->adf4002_regs[k] | k , NULL);
        res = res ? res : adf4002b_reg_set(dfe->dev, dfe->subdev, dfe->adf4002_spiidx, k, dfe->adf4002_regs[k]);
    }


    // I2C expander
    uint32_t p[3];
    res = res ? res : tca6424a_reg24_get(dev, dfe->subdev, I2C_TCA6424AR_U114, TCA6424_IN0, &p[0]);
    res = res ? res : tca6424a_reg24_get(dev, dfe->subdev, I2C_TCA6424AR_U113, TCA6424_IN0, &p[0]);
    res = res ? res : tca6424a_reg24_get(dev, dfe->subdev, I2C_TCA6424AR_U115, TCA6424_IN0, &p[0]);


    // I2C dac

    // I2C temp
    int tmpid[6] = { -1, -1, -1, -1, -1, -1 };

    res = res ? res : tmp114_devid_get(dev, dfe->subdev, I2C_TEMP_U69, &tmpid[0]);
    res = res ? res : tmp114_devid_get(dev, dfe->subdev, I2C_TEMP_U70, &tmpid[1]);
    res = res ? res : tmp114_devid_get(dev, dfe->subdev, I2C_TEMP_U71, &tmpid[2]);
    res = res ? res : dac80501_init(dev, dfe->subdev, I2C_DAC, DAC80501_CFG_REF_DIV_GAIN_MUL);


    res = res ? res : tmp114_temp_get(dev, dfe->subdev, I2C_TEMP_U69, &tmpid[3]);
    res = res ? res : tmp114_temp_get(dev, dfe->subdev, I2C_TEMP_U70, &tmpid[4]);
    res = res ? res : tmp114_temp_get(dev, dfe->subdev, I2C_TEMP_U71, &tmpid[5]);

    USDR_LOG("HIPR", USDR_LOG_WARNING, "HIPER temp sensors %04x %04x %04x | %.2f %.2f %.2f\n",
             tmpid[0], tmpid[1], tmpid[2], tmpid[3] / 256.0, tmpid[4] / 256.0, tmpid[5] / 256.0);

    if (res) {
        USDR_LOG("HIPR", USDR_LOG_WARNING, "HIPER front end initialization failed: %d\n", res);
        return res;
    }

    res = usdr_vfs_obj_param_init_array_param(base,
                                              (void*)dfe,
                                              s_fe_parameters,
                                              SIZEOF_ARRAY(s_fe_parameters));
    if (res)
        return res;

    for (unsigned h = 0; h < 5; h++) {
        sleep(1);
        uint32_t a, b;

        dev_gpi_get32(dev, 24, &a);
        dev_gpi_get32(dev, 28, &b);

        USDR_LOG("HIPR", USDR_LOG_WARNING, "Cntr %08x %08x / %7d %7d\n", a, b, a & 0xfffffff, b & 0xfffffff);
    }

    {
        // check uart
        char b[8192];
        uart_core_t uc;
        res = (res) ? res : uart_core_init(dfe->dev, dfe->subdev, REG_UART_TRX, &uc);
        res = (res) ? res : uart_core_rx_collect(&uc, sizeof(b), b, 2250);
        USDR_LOG("HIPR", USDR_LOG_ERROR, "UART: `%s`\n", b);
    }

    memset(dfe->fe_gpo_regs, 0, sizeof(dfe->fe_gpo_regs));
    USDR_LOG("HIPR", USDR_LOG_WARNING, "HIPER front end is ready!\n");
    return 0;
}

int dsdr_hiper_meas_clk40_int_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    uint32_t a = 0;
    int res = dev_gpi_get32(((dsdr_hiper_fe_t*)obj->object)->dev, 24, &a);
    *ovalue = a & 0xfffffff;
    return res;
}

int dsdr_hiper_meas_clk40_pps_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    uint32_t a = 0;
    int res = dev_gpi_get32(((dsdr_hiper_fe_t*)obj->object)->dev, 28, &a);
    *ovalue = a  & 0xfffffff;
    return res;
}

int dsdr_hiper_meas_adfmux_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    uint32_t a = 0;
    int res = dev_gpi_get32(((dsdr_hiper_fe_t*)obj->object)->dev, 32, &a);
    *ovalue = a  & 0xfffffff;
    return res;
}

int dsdr_hiper_fe_destroy(dsdr_hiper_fe_t* dfe)
{
    return 0;
}
