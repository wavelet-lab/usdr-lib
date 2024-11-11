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

};



static int dsdr_hiper_fe_lms8_reg_set(dsdr_hiper_fe_t* fe, unsigned idx, uint64_t value)
{
    uint32_t v = value;
    int res = lowlevel_spi_tr32(fe->dev, fe->subdev, fe->lms8[idx].lsaddr, v, &fe->debug_lms8001_last[idx]);

    USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug LMS8[%d] REG %08x => %08x\n",
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
    return res;
}

int dsdr_hiper_fe_create(lldev_t dev, unsigned int spix_num, dsdr_hiper_fe_t* dfe)
{
    int res = 0;
    device_t* base = lowlevel_get_device(dev);
    dfe->dev = dev;
    dfe->subdev = 0;

    USDR_LOG("HIPR", USDR_LOG_WARNING, "Initializing HIPER front end...\n");

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

    // ADF4002 (MUX -> DVDD)
    res = res ? res : lowlevel_spi_tr32(dfe->dev, dfe->subdev, MAKE_SPIEXT_LSOPADR(SPI_ADF4002_CFG, 0, spix_num), 0x33, NULL);


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

    memset(dfe->fe_gpo_regs, 0, sizeof(dfe->fe_gpo_regs));
    USDR_LOG("HIPR", USDR_LOG_WARNING, "HIPER front end is ready!\n");
    return 0;
}

int dsdr_hiper_fe_destroy(dsdr_hiper_fe_t* dfe)
{
    return 0;
}
