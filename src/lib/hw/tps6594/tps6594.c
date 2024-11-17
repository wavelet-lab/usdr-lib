// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "tps6594.h"
#include <usdr_logging.h>
#include <usdr_lowlevel.h>

enum tps6594_regs
{
    DEV_REV = 0x1,
    NVM_CODE_1 = 0x2,
    NVM_CODE_2 = 0x3,
    BUCK1_CTRL = 0x4,
    BUCK1_CONF = 0x5,
    BUCK2_CTRL = 0x6,
    BUCK2_CONF = 0x7,
    BUCK3_CTRL = 0x8,
    BUCK3_CONF = 0x9,
    BUCK4_CTRL = 0xA,
    BUCK4_CONF = 0xB,
    BUCK5_CTRL = 0xC,
    BUCK5_CONF = 0xD,
    BUCK1_VOUT_1 = 0xE,
    BUCK1_VOUT_2 = 0xF,
    BUCK2_VOUT_1 = 0x10,
    BUCK2_VOUT_2 = 0x11,
    BUCK3_VOUT_1 = 0x12,
    BUCK3_VOUT_2 = 0x13,
    BUCK4_VOUT_1 = 0x14,
    BUCK4_VOUT_2 = 0x15,
    BUCK5_VOUT_1 = 0x16,
    BUCK5_VOUT_2 = 0x17,
    BUCK1_PG_WINDOW = 0x18,
    BUCK2_PG_WINDOW = 0x19,
    BUCK3_PG_WINDOW = 0x1A,
    BUCK4_PG_WINDOW = 0x1B,
    BUCK5_PG_WINDOW = 0x1C,
    LDO1_CTRL = 0x1D,
    LDO2_CTRL = 0x1E,
    LDO3_CTRL = 0x1F,
    LDO4_CTRL = 0x20,
    LDORTC_CTRL = 0x22,
    LDO1_VOUT = 0x23,
    LDO2_VOUT = 0x24,
    LDO3_VOUT = 0x25,
    LDO4_VOUT = 0x26,
    LDO1_PG_WINDOW = 0x27,
    LDO2_PG_WINDOW = 0x28,
    LDO3_PG_WINDOW = 0x29,
    LDO4_PG_WINDOW = 0x2A,
    VCCA_VMON_CTRL = 0x2B,
    VCCA_PG_WINDOW = 0x2C,
    GPIO1_CONF = 0x31,
    GPIO2_CONF = 0x32,
    GPIO3_CONF = 0x33,
    GPIO4_CONF = 0x34,
    GPIO5_CONF = 0x35,
    GPIO6_CONF = 0x36,
    GPIO7_CONF = 0x37,
    GPIO8_CONF = 0x38,
    GPIO9_CONF = 0x39,
    GPIO10_CONF = 0x3A,
    GPIO11_CONF = 0x3B,
    NPWRON_CONF = 0x3C,
    GPIO_OUT_1 = 0x3D,
    GPIO_OUT_2 = 0x3E,
    GPIO_IN_1 = 0x3F,
    GPIO_IN_2 = 0x40,
    RAIL_SEL_1 = 0x41,
    RAIL_SEL_2 = 0x42,
    RAIL_SEL_3 = 0x43,
    FSM_TRIG_SEL_1 = 0x44,
    FSM_TRIG_SEL_2 = 0x45,
    FSM_TRIG_MASK_1 = 0x46,
    FSM_TRIG_MASK_2 = 0x47,
    FSM_TRIG_MASK_3 = 0x48,
    MASK_BUCK1_2 = 0x49,
    MASK_BUCK3_4 = 0x4A,
    MASK_BUCK5 = 0x4B,
    MASK_LDO1_2 = 0x4C,
    MASK_LDO3_4 = 0x4D,
    MASK_VMON = 0x4E,
    MASK_GPIO1_8_FALL = 0x4F,
    MASK_GPIO1_8_RISE = 0x50,
    MASK_GPIO9_11 = 0x51,
    MASK_STARTUP = 0x52,
    MASK_MISC = 0x53,
    MASK_MODERATE_ERR = 0x54,
    MASK_FSM_ERR = 0x56,
    MASK_COMM_ERR = 0x57,
    MASK_READBACK_ERR = 0x58,
    MASK_ESM = 0x59,
    INT_TOP = 0x5A,
    INT_BUCK = 0x5B,
    INT_BUCK1_2 = 0x5C,
    INT_BUCK3_4 = 0x5D,
    INT_BUCK5 = 0x5E,
    INT_LDO_VMON = 0x5F,
    INT_LDO1_2 = 0x60,
    INT_LDO3_4 = 0x61,
    INT_VMON = 0x62,
    INT_GPIO = 0x63,
    INT_GPIO1_8 = 0x64,
    INT_STARTUP = 0x65,
    INT_MISC = 0x66,
    INT_MODERATE_ERR = 0x67,
    INT_SEVERE_ERR = 0x68,
    INT_FSM_ERR = 0x69,
    INT_COMM_ERR = 0x6A,
    INT_READBACK_ERR = 0x6B,
    INT_ESM = 0x6C,
    STAT_BUCK1_2 = 0x6D,
    STAT_BUCK3_4 = 0x6E,
    STAT_BUCK5 = 0x6F,
    STAT_LDO1_2 = 0x70,
    STAT_LDO3_4 = 0x71,
    STAT_VMON = 0x72,
    STAT_STARTUP = 0x73,
    STAT_MISC = 0x74,
    STAT_MODERATE_ERR = 0x75,
    STAT_SEVERE_ERR = 0x76,
    STAT_READBACK_ERR = 0x77,
    PGOOD_SEL_1 = 0x78,
    PGOOD_SEL_2 = 0x79,
    PGOOD_SEL_3 = 0x7A,
    PGOOD_SEL_4 = 0x7B,
    PLL_CTRL = 0x7C,
    CONFIG_1 = 0x7D,
    CONFIG_2 = 0x7E,
    ENABLE_DRV_REG = 0x80,
    MISC_CTRL = 0x81,
    ENABLE_DRV_STAT = 0x82,
    RECOV_CNT_REG_1 = 0x83,
    RECOV_CNT_REG_2 = 0x84,
    FSM_I2C_TRIGGERS = 0x85,
    FSM_NSLEEP_TRIGGERS = 0x86,
    BUCK_RESET_REG = 0x87,
    SPREAD_SPECTRUM_1 = 0x88,
    FREQ_SEL = 0x8A,
    FSM_STEP_SIZE = 0x8B,
    LDO_RV_TIMEOUT_REG_1 = 0x8C,
    LDO_RV_TIMEOUT_REG_2 = 0x8D,
    USER_SPARE_REGS = 0x8E,
    ESM_MCU_START_REG = 0x8F,
    ESM_MCU_DELAY1_REG = 0x90,
    ESM_MCU_DELAY2_REG = 0x91,
    ESM_MCU_MODE_CFG = 0x92,
    ESM_MCU_HMAX_REG = 0x93,
    ESM_MCU_HMIN_REG = 0x94,
    ESM_MCU_LMAX_REG = 0x95,
    ESM_MCU_LMIN_REG = 0x96,
    ESM_MCU_ERR_CNT_REG = 0x97,
    ESM_SOC_START_REG = 0x98,
    ESM_SOC_DELAY1_REG = 0x99,
    ESM_SOC_DELAY2_REG = 0x9A,
    ESM_SOC_MODE_CFG = 0x9B,
    ESM_SOC_HMAX_REG = 0x9C,
    ESM_SOC_HMIN_REG = 0x9D,
    ESM_SOC_LMAX_REG = 0x9E,
    ESM_SOC_LMIN_REG = 0x9F,
    ESM_SOC_ERR_CNT_REG = 0xA0,
    REGISTER_LOCK = 0xA1,
    RTC_SECONDS = 0xB5,
    RTC_MINUTES = 0xB6,
    RTC_HOURS = 0xB7,
    RTC_DAYS = 0xB8,
    RTC_MONTHS = 0xB9,
    RTC_YEARS = 0xBA,
    RTC_WEEKS = 0xBB,
    ALARM_SECONDS = 0xBC,
    ALARM_MINUTES = 0xBD,
    ALARM_HOURS = 0xBE,
    ALARM_DAYS = 0xBF,
    ALARM_MONTHS = 0xC0,
    ALARM_YEARS = 0xC1,
    RTC_CTRL_1 = 0xC2,
    RTC_CTRL_2 = 0xC3,
    RTC_STATUS = 0xC4,
    RTC_INTERRUPTS = 0xC5,
    RTC_COMP_LSB = 0xC6,
    RTC_COMP_MSB = 0xC7,
    RTC_RESET_STATUS = 0xC8,
    SCRATCH_PAD_REG_1 = 0xC9,
    SCRATCH_PAD_REG_2 = 0xCA,
    SCRATCH_PAD_REG_3 = 0xCB,
    SCRATCH_PAD_REG_4 = 0xCC,
    PFSM_DELAY_REG_1 = 0xCD,
    PFSM_DELAY_REG_2 = 0xCE,
    PFSM_DELAY_REG_3 = 0xCF,
    PFSM_DELAY_REG_4 = 0xD0,
};

enum reg6594_buckx_ctrl {
    BUCKX_RV_SEL = 7,
    BUCKX_PLDN = 5,
    BUCKX_VMON_EN = 4,
    BUCKX_VSEL = 3,
    BUCK13_FPWM_MP = 2, //For BUCK1 & BUCK3 only
    BUCKX_FPWM = 1,
    BUCKX_EN = 0,
};

enum reg6594_ldox_ctrl {
    LDOX_RV_SEL = 7,
    LDOX_PLDN = 5,
    LDOX_VMON_EN = 4,
    LDOX_EN = 0,
};

static
int tps6594_reg_rd(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                   uint8_t addr, uint8_t* val)
{
    return lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        USDR_LSOP_I2C_DEV, ls_op_addr,
                                        1, val, 1, &addr);
}

static
int tps6594_reg_wr(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                   uint8_t addr, uint8_t val)
{
    USDR_LOG("6594", USDR_LOG_DEBUG, "TPS6594 REG %02d 0x%02x\n", addr, val);
    uint8_t data[2] = {addr, val};
    return lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        USDR_LSOP_I2C_DEV, ls_op_addr,
                                        0, NULL, 2, data);
}

static
uint8_t tps6594_buck_vout(unsigned vout_mv)
{
    if (vout_mv < 300)
        return 0;
    if (vout_mv < 600)
        return (vout_mv - 300) / 20;
    if (vout_mv < 1100)
        return ((vout_mv - 600) / 5) + 0x0f;
    if (vout_mv < 1660)
        return ((vout_mv - 1100) / 10) + 0x73;
    if (vout_mv < 3340)
        return ((vout_mv - 1660) / 20) + 0xAB;

    return 0xff;
}

static
uint8_t tps6594_ldo123_vout(unsigned vout_mv)
{
    if (vout_mv < 600)
        return 0x04;
    if (vout_mv < 3300)
        return ((vout_mv - 600) / 50) + 0x04;
    return 0x3a;
}

static
uint8_t tps6594_ldo4_vout(unsigned vout_mv)
{
    if (vout_mv < 1200)
        return 0x20;
    if (vout_mv < 3300)
        return ((vout_mv - 1200) / 25) + 0x20;
    return 0x74;
}


int tps6594_check(lldev_t dev, subdev_t subdev, lsopaddr_t addr)
{
    uint8_t r[3];
    int res = 0;

    res = res ? res :tps6594_reg_rd(dev, subdev, addr, DEV_REV, &r[0]);
    res = res ? res :tps6594_reg_rd(dev, subdev, addr, NVM_CODE_1, &r[1]);
    res = res ? res :tps6594_reg_rd(dev, subdev, addr, NVM_CODE_2, &r[2]);

    if (res)
        return res;

    USDR_LOG("6594", USDR_LOG_INFO, "REV:%02x CODE:%02x_%02x\n", r[0], r[1], r[2]);
    return 0;
}

int tps6594_vout_set(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                     unsigned ch, unsigned vout)
{
    uint8_t reg;
    uint8_t vout_val;

    switch (ch) {
    case TPS6594_BUCK1: reg = BUCK1_VOUT_1; vout_val = tps6594_buck_vout(vout); break;
    case TPS6594_BUCK2: reg = BUCK2_VOUT_1; vout_val = tps6594_buck_vout(vout); break;
    case TPS6594_BUCK3: reg = BUCK3_VOUT_1; vout_val = tps6594_buck_vout(vout); break;
    case TPS6594_BUCK4: reg = BUCK4_VOUT_1; vout_val = tps6594_buck_vout(vout); break;
    case TPS6594_BUCK5: reg = BUCK5_VOUT_1; vout_val = tps6594_buck_vout(vout); break;
    case TPS6594_LDO1: reg = LDO1_VOUT; vout_val = tps6594_ldo123_vout(vout) << 1; break;
    case TPS6594_LDO2: reg = LDO2_VOUT; vout_val = tps6594_ldo123_vout(vout) << 1; break;
    case TPS6594_LDO3: reg = LDO3_VOUT; vout_val = tps6594_ldo123_vout(vout) << 1; break;
    case TPS6594_LDO4: reg = LDO4_VOUT; vout_val = tps6594_ldo4_vout(vout); break;
    default:
        return -EINVAL;
    }

    USDR_LOG("6594", USDR_LOG_NOTE, "Set voltage %s%d to %d\n",
             (ch < TPS6594_LDO1) ? "BUCK" : "LDO",
             (ch < TPS6594_LDO1) ? ch + 1 : ch - TPS6594_LDO1 + 1,
             vout);

    return tps6594_reg_wr(dev, subdev, addr, reg, vout_val);
}

int tps6594_vout_ctrl(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                      unsigned ch, bool en)
{
    uint8_t reg;
    uint8_t vout_ctrl;

    switch (ch) {
    case TPS6594_BUCK1: reg = BUCK1_CTRL; vout_ctrl = (1u << BUCKX_PLDN); break;
    case TPS6594_BUCK2: reg = BUCK2_CTRL; vout_ctrl = (1u << BUCKX_PLDN); break;
    case TPS6594_BUCK3: reg = BUCK3_CTRL; vout_ctrl = (1u << BUCKX_PLDN); break;
    case TPS6594_BUCK4: reg = BUCK4_CTRL; vout_ctrl = (1u << BUCKX_PLDN); break;
    case TPS6594_BUCK5: reg = BUCK5_CTRL; vout_ctrl = (1u << BUCKX_PLDN); break;
    case TPS6594_LDO1: reg = LDO1_CTRL; vout_ctrl = (1u << LDOX_PLDN); break;
    case TPS6594_LDO2: reg = LDO2_CTRL; vout_ctrl = (1u << LDOX_PLDN); break;
    case TPS6594_LDO3: reg = LDO3_CTRL; vout_ctrl = (1u << LDOX_PLDN); break;
    case TPS6594_LDO4: reg = LDO4_CTRL; vout_ctrl = (1u << LDOX_PLDN); break;
    default:
        return -EINVAL;
    }

    if (en)
        vout_ctrl |= (1u << BUCKX_EN) | (1u << BUCKX_FPWM) | (1u << BUCKX_VMON_EN);

    USDR_LOG("6594", USDR_LOG_NOTE, "Enable flag %s%d set to %d\n",
             (ch < TPS6594_LDO1) ? "BUCK" : "LDO",
             (ch < TPS6594_LDO1) ? ch + 1 : ch - TPS6594_LDO1 + 1,
             en);

    return tps6594_reg_wr(dev, subdev, addr, reg, vout_ctrl);
}

int tps6594_reg_dump(lldev_t dev, subdev_t subdev, lsopaddr_t addr)
{
    uint8_t rout;
    uint8_t raddr;
    int res;
    for (raddr = 0x6d; raddr < 0x74; raddr++) {
        res = tps6594_reg_rd(dev, subdev, addr, raddr, &rout);
        if (res)
            return res;

         USDR_LOG("6594", USDR_LOG_INFO, "TPS[%02x] = %02x\n", raddr, rout);
    }
    return 0;
}

int tps6594_set_fr(lldev_t dev, subdev_t subdev, lsopaddr_t addr)
{
    tps6594_reg_wr(dev, subdev, addr, 0x8a, 0x1f);

    tps6594_reg_wr(dev, subdev, addr, 0x8b, 0x5);
    return tps6594_reg_wr(dev, subdev, addr, 0x88, 0x5);
}
