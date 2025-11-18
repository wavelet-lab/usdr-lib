// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "strings.h"
#include "si5332.h"
#include "def_si5332.h"

#include <usdr_logging.h>
#include <usdr_lowlevel.h>

static
int si5332_reg_rd(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                 uint8_t addr, uint8_t* val)
{
    return lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        USDR_LSOP_I2C_DEV, ls_op_addr,
                                        1, val, 1, &addr);
}

static
int si5332_reg_wr(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                 uint8_t addr, uint8_t val)
{
    uint8_t data[2] = {addr, val};
    return lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        USDR_LSOP_I2C_DEV, ls_op_addr,
                                        0, NULL, 2, data);
}

enum si5332_helpers {
    si5332_Fvco_min = 2375000000u,
    si5332_Fvco_max = 2625000000u,
    si5332_Fref_min = 10000000,
    si5332_Fref_max = 50000000,

    si5332_hsdiv_min = 8,
    si5332_hsdiv_max = 255,

    si5332_id_min = 10,
    si5332_id_max = 255,
};

enum si5332_regs {
    VDD_OK        = 0x05,
    USYS_CTRL     = 0x06,
    USYS_STAT     = 0x07,
    UDRV_OE_ENA   = 0x08,
    USER_SCRATCH0 = 0x09,
    USER_SCRATCH1 = 0x0A,
    USER_SCRATCH2 = 0x0B,
    USER_SCRATCH3 = 0x0C,
    DEVICE_PN_BASE   = 0x0D,
    DEVICE_REV       = 0x0E,
    DEVICE_GRADE     = 0x0F,
    FACTORY_OPN_ID10 = 0x10,
    FACTORY_OPN_ID32 = 0x11,
    FACTORY_OPN_IDR4 = 0x12,
    DESIGN_ID0       = 0x13,
    DESIGN_ID1       = 0x14,
    DESIGN_ID2       = 0x15,
    I2C_ADDR         = 0x21,
    I2C_PUP_ENA      = 0x23,
    IMUX_SEL         = 0x24,
    OMUX0_SEL10      = 0x25,
    OMUX1_SEL10      = 0x26,
    OMUX2_SEL10      = 0x27,
    OMUX3_SEL10      = 0x28,
    OMUX4_SEL10      = 0x29,
    OMUX5_SEL10      = 0x2A,
    HSDIV0A_DIV      = 0x2B,
    HSDIV0B_DIV      = 0x2C,
    HSDIV1A_DIV      = 0x2D,
    HSDIV1B_DIV      = 0x2E,
    HSDIV2A_DIV      = 0x2F,
    HSDIV2B_DIV      = 0x30,
    HSDIV3A_DIV      = 0x31,
    HSDIV3B_DIV      = 0x32,
    HSDIV4A_DIV      = 0x33,
    HSDIV4B_DIV      = 0x34,
    HSDIV_ID_CFG_SEL = 0x35,

    ID0A_INTG_L      = 0x36,
    ID0A_INTG_H      = 0x37,
    ID0A_RES_L       = 0x38,
    ID0A_RES_H       = 0x39,
    ID0A_DEN_L       = 0x3A,
    ID0A_DEN_H       = 0x3B,
    ID0A_SS          = 0x3C,
    ID0A_SS_STEP_NUM_L = 0x3D,
    ID0A_SS_STEP_NUM_H = 0x3E,
    ID0A_SS_STEP_INTG_L = 0x3F,
    ID0A_SS_STEP_RES_L  = 0x40,
    ID0A_SS_STEP_RES_H  = 0x41,

    ID0B_INTG_L      = 0x42,
    ID0B_INTG_H      = 0x43,
    ID0B_RES_L       = 0x44,
    ID0B_RES_H       = 0x45,
    ID0B_DEN_L       = 0x46,
    ID0B_DEN_H       = 0x47,
    ID0B_SS          = 0x48,
    ID0B_SS_STEP_NUM_L = 0x49,
    ID0B_SS_STEP_NUM_H = 0x4A,
    ID0B_SS_STEP_INTG_L = 0x4B,
    ID0B_SS_STEP_RES_L  = 0x4C,
    ID0B_SS_STEP_RES_H  = 0x4D,

    ID1A_INTG_L      = 0x4E,
    ID1A_INTG_H      = 0x4F,
    ID1A_RES_L       = 0x50,
    ID1A_RES_H       = 0x51,
    ID1A_DEN_L       = 0x52,
    ID1A_DEN_H       = 0x53,
    ID1A_SS          = 0x54,
    ID1A_SS_STEP_NUM_L = 0x55,
    ID1A_SS_STEP_NUM_H = 0x56,
    ID1A_SS_STEP_INTG_L = 0x57,
    ID1A_SS_STEP_RES_L  = 0x58,
    ID1A_SS_STEP_RES_H  = 0x59,

    ID1B_INTG_L      = 0x5A,
    ID1B_INTG_H      = 0x5B,
    ID1B_RES_L       = 0x5C,
    ID1B_RES_H       = 0x5D,
    ID1B_DEN_L       = 0x5E,
    ID1B_DEN_H       = 0x5F,
    ID1B_SS          = 0x60,
    ID1B_SS_STEP_NUM_L = 0x61,
    ID1B_SS_STEP_NUM_H = 0x62,
    ID1B_SS_STEP_INTG_L = 0x63,
    ID1B_SS_STEP_RES_L  = 0x64,
    ID1B_SS_STEP_RES_H  = 0x65,

    IDPA_INTG_L = 0x67,
    IDPA_INTG_H = 0x68,
    IDPA_RES_L = 0x69,
    IDPA_RES_H = 0x6A,
    IDPA_DEN_L = 0x6B,
    IDPA_DEN_H = 0x6C,

    PDIV_DIV   = 0x75,

    USYS_START = 0xB8,
    PLL_MODE   = 0xBE,

    CLKIN_2_CLK_SEL = 0x73,
    CLKIN_3_CLK_SEL = 0x74,

    // Output configurations
    OUT0_MODE  = 0x7A,
    OUT0_DIV   = 0x7B,
    OUT0_SKEW  = 0x7C,
    OUT0_CMOS_INV_Z = 0x7D,
    OUT0_CMOS_SLEW = 0x7E,

    OUT1_MODE  = 0x7F,
    OUT1_DIV   = 0x80,
    OUT1_SKEW  = 0x81,
    OUT1_CMOS_INV_Z = 0x82,
    OUT1_CMOS_SLEW = 0x83,

    // 84, 85, 86, 87, 88

    OUT2_MODE  = 0x89,
    OUT2_DIV   = 0x8A,
    OUT2_SKEW  = 0x8B,
    OUT2_CMOS_INV_Z = 0x8C,
    OUT2_CMOS_SLEW = 0x8D,

    // 8E, 8F, 90, 91, 92
    // 93, 94, 95, 96, 97

    OUT3_MODE  = 0x98,
    OUT3_DIV   = 0x99,
    OUT3_SKEW  = 0x9A,
    OUT3_CMOS_INV_Z = 0x9B,
    OUT3_CMOS_SLEW = 0x9C,

    // 9D, 9E, 9F, A0, A1
    // A2, A3, A4, A5, A6

    OUT4_MODE  = 0xA7,
    OUT4_DIV   = 0xA8,
    OUT4_SKEW  = 0xA9,
    OUT4_CMOS_INV_Z = 0xAA,
    OUT4_CMOS_SLEW = 0xAB,

    OUT5_MODE  = 0xAC,
    OUT5_DIV   = 0xAD,
    OUT5_SKEW  = 0xAE,
    OUT5_CMOS_INV_Z = 0xAF,
    OUT5_CMOS_SLEW = 0xB0,

    OUT3210_OE = 0xB6,
    OUT54_OE  = 0xB7,

    XOSC_CINT_ENA = 0xBF,
    XOSC_CTRIM_XIN = 0xC0,
    XOSC_CTRIM_XOUT = 0xC1,

};

#define BIT(x) (1u << (x))
enum {
    B6_OUT0_OE = BIT(0),
    B6_OUT1_OE = BIT(1),
    B6_OUT2_OE = BIT(3),
    B6_OUT3_OE = BIT(6),
    B7_OUT4_OE = BIT(1),
    B7_OUT5_OE = BIT(2),

    B9_XOSC_DIS = BIT(0),
    B9_IBUF0_DIS = BIT(1),
    B9_IMUX_DIS = BIT(3),
    B9_PDIV_DIS = BIT(4),
    B9_PLL_DIS = BIT(5),

    BA_HSDIV0_DIS = BIT(0),
    BA_HSDIV1_DIS = BIT(1),
    BA_HSDIV2_DIS = BIT(2),
    BA_HSDIV3_DIS = BIT(3),
    BA_HSDIV4_DIS = BIT(4),
    BA_ID0_DIS = BIT(5),
    BA_ID1_DIS = BIT(6),

    BB_OMUX0_DIS = BIT(0),
    BB_OMUX1_DIS = BIT(1),
    BB_OMUX2_DIS = BIT(2),
    BB_OMUX3_DIS = BIT(3),
    BB_OMUX4_DIS = BIT(4),
    BB_OMUX5_DIS = BIT(5),

    BC_OUT0_DIS = BIT(0),
    BC_OUT1_DIS = BIT(1),
    BC_OUT2_DIS = BIT(3),
    BC_OUT3_DIS = BIT(6),

    BD_OUT4_DIS = BIT(1),
    BD_OUT5_DIS = BIT(2),
};

/*
 * 0xB6[0]     OUT0_OE          1                  0x1
 * 0xB6[1]     OUT1_OE          1                  0x1
 * 0xB6[3]     OUT2_OE          1                  0x1
 * 0xB6[6]     OUT3_OE          0                  0x0
 * 0xB7[1]     OUT4_OE          0                  0x0
 * 0xB7[2]     OUT5_OE          0                  0x0
 * 0xB9[0]     XOSC_DIS         1                  0x1
 * 0xB9[1]     IBUF0_DIS        0                  0x0
 * 0xB9[3]     IMUX_DIS         0                  0x0
 * 0xB9[4]     PDIV_DIS         0                  0x0
 * 0xB9[5]     PLL_DIS          0                  0x0
 * 0xBA[5]     ID0_DIS          1                  0x1
 * 0xBA[6]     ID1_DIS          1                  0x1
 * 0xBA[0]     HSDIV0_DIS       0                  0x0
 * 0xBA[1]     HSDIV1_DIS       1                  0x1
 * 0xBA[2]     HSDIV2_DIS       1                  0x1
 * 0xBA[3]     HSDIV3_DIS       1                  0x1
 * 0xBA[4]     HSDIV4_DIS       1                  0x1
 * 0xBB[0]     OMUX0_DIS        0                  0x0
 * 0xBB[1]     OMUX1_DIS        0                  0x0
 * 0xBB[2]     OMUX2_DIS        0                  0x0
 * 0xBB[3]     OMUX3_DIS        1                  0x1
 * 0xBB[4]     OMUX4_DIS        1                  0x1
 * 0xBB[5]     OMUX5_DIS        1                  0x1
 * 0xBC[0]     OUT0_DIS         0                  0x0
 * 0xBC[1]     OUT1_DIS         0                  0x0
 * 0xBC[3]     OUT2_DIS         0                  0x0
 * 0xBC[6]     OUT3_DIS         1                  0x1
 * 0xBD[1]     OUT4_DIS         1                  0x1
 * 0xBD[2]     OUT5_DIS         1                  0x1
 * 0xBE[7:0]   PLL_MODE         16                 0x10
 * 0xBF[0]     XOSC_CINT_ENA    0                  0x0
 * 0xC0[5:0]   XOSC_CTRIM_XIN   0                  0x00
 * 0xC1[5:0]   XOSC_CTRIM_XOUT  0                  0x00
 */


enum si5332_outx_mode {
    OUTMODE_OFF = 0, // off
    OUTMODE_CMOS_P = 1, // CMOS on positive output only
    OUTMODE_CMOS_N = 2, // CMOS on negative output only
    OUTMODE_CMSOS = 3, // dual CMOS outputs
    OUTMODE_LVDS25 = 4, // 2.5V/3.3V LVDS
    OUTMODE_LVDS18 = 5, // 1.8V LVDS
    OUTMODE_LVDS25_FAST = 6, // 2.5V/3.3V LVDS fast
    OUTMODE_LVDS18_FAST = 7, // 1.8V LVDS fast
    OUTMODE_HCSL_50_EXT = 8, // HCSL 50 Ω (external termination)
    OUTMODE_HCSL_50_INT = 9, // HCSL 50 Ω (internal termination)
    OUTMODE_HCSL_42_EXT = 10, // HCSL 42.5 Ω (external termination)
    OUTMODE_HCSL_42_INT = 11, // HCSL 42.5 Ω (internal termination)
    OUTMODE_LVPECL = 12, // LVPECL
};


enum si5332_imux_sel {
    IMUX_DISABLE = 0, // PLL reference clock before pre-scaler
    IMUX_XOSC = 1, // PLL reference clock after pre-scaler
    IMUX_IN_2 = 2, // Clock from input buffer 2
    IMUX_IN_3 = 3, // Clock from input buffer 3
};

enum si5332_imux_in_2 {
    IMUX_INX_DISABLED = 0,
    IMUX_INX_DIFF = 1,
    IMUX_INX_CMOS_DC = 2,
    IMUX_INX_CMOS_AC = 3,
};


enum si5332_omuxx_sel0 {
    OUMUXX_SEL0_PLL_REF = 0, // PLL reference clock before pre-scaler
    OUMUXX_SEL0_PLL_REF_PRE = 1, // PLL reference clock after pre-scaler
    OUMUXX_SEL0_IN_2 = 2, // Clock from input buffer 2
    OUMUXX_SEL0_IN_3 = 3, // Clock from input buffer 3
};

enum si5332_omuxx_sel1 {
    OUMUXX_SEL1_HSDIV0 = 0, // HSDIV0
    OUMUXX_SEL1_HSDIV1 = 1, // HSDIV1
    OUMUXX_SEL1_HSDIV2 = 2, // HSDIV2
    OUMUXX_SEL1_HSDIV3 = 3, // HSDIV3
    OUMUXX_SEL1_HSDIV4 = 4, // HSDIV4
    OUMUXX_SEL1_ID0    = 5, // ID0
    OUMUXX_SEL1_ID1    = 6, // ID1
    OUMUXX_SEL1_OMUXS0 = 7, // Clock from omux1_sel0
};


#define MAKE_OUMUXX(s0, s1)  (((s0) << 0) | ((s1) << 4))

static int si5532_get_state(lldev_t dev, subdev_t subdev, lsopaddr_t lsopaddr, const char* hint)
{
    uint8_t state = 0;
    int res = 0;
    const static int LOOPS = 100;

    for(unsigned i = 0; i < LOOPS; ++i)
    {
        usleep(10);

        res = si5332_reg_rd(dev, subdev, lsopaddr, USYS_STAT, &state);

        if(res)
            return res;

        if(state == 0x01 || state == 0x02 || state == 0x89)
            break;
    }

    const int tag = (state == 0x01 || state == 0x02) ? USDR_LOG_DEBUG : (strncasecmp(hint, "BEFORE", 6) == 0 ? USDR_LOG_WARNING : USDR_LOG_ERROR);
    USDR_LOG("5532", tag, "[%s] si5532 state: 0x%02x (%s)", hint, state, state == 0x01 ? "READY" : (state == 0x02 ? "ACTIVE" : "ERROR"));
    if(state == 0x89)
    {
        USDR_LOG("5532", tag, "The device has not detected an input clock source and can't proceed to ACTIVE state");
        return tag == USDR_LOG_ERROR ? -EILSEQ : 0;
    }

    return res;
}

int si5332_init(lldev_t dev, subdev_t subdev, lsopaddr_t lsopaddr, unsigned div, bool ext_in2, bool rv)
{
    int res;
    uint8_t di[9], vddok;
    for (unsigned a = DEVICE_PN_BASE; a <= DESIGN_ID2; a++) {
        res = si5332_reg_rd(dev, subdev, lsopaddr, a, &di[a - DEVICE_PN_BASE]);
        if (res)
            return res;
    }

    res = si5332_reg_rd(dev, subdev, lsopaddr, VDD_OK, &vddok);
    if (res)
        return res;

    USDR_LOG("5332", USDR_LOG_INFO, "Si5332 DEV=%02x%02x%02x OPN=%02x%02x%02x DID=%02x%02x%02x VDD=%02x DIV=%d\n",
             di[0], di[1], di[2], di[3], di[4], di[5], di[6], di[7], di[8], vddok, div);

    if (di[0] == 0xff && di[1] == 0xff && di[2] == 0xff && di[3] == 0xff && di[4] == 0xff && di[5] == 0xff)
        return -ENODEV;

    const uint8_t program_regs_init[] = {
        USYS_CTRL, 0x01, //READY
     //   UDRV_OE_ENA, 0x01,
       // IMUX_SEL, IMUX_XOSC, //IMUX_IN_2, //IMUX_XOSC,
       // IMUX_SEL, IMUX_IN_2, //IMUX_IN_2, //IMUX_XOSC,
       // CLKIN_2_CLK_SEL, 1,

        IMUX_SEL, ext_in2 ? IMUX_IN_2 : IMUX_XOSC,
        CLKIN_2_CLK_SEL, ext_in2 ? IMUX_INX_DIFF /*IMUX_INX_CMOS_AC*/ : IMUX_INX_DISABLED,
        CLKIN_3_CLK_SEL, 0,

        0x3C, 0,
        0x48, 0,
        0x54, 0,
        0x60, 0,

        OMUX0_SEL10, MAKE_OUMUXX(OUMUXX_SEL0_PLL_REF, OUMUXX_SEL1_OMUXS0),
        OMUX1_SEL10, MAKE_OUMUXX(OUMUXX_SEL0_PLL_REF, OUMUXX_SEL1_OMUXS0),
        OMUX2_SEL10, MAKE_OUMUXX(OUMUXX_SEL0_PLL_REF, OUMUXX_SEL1_OMUXS0),
        OMUX3_SEL10, MAKE_OUMUXX(OUMUXX_SEL0_PLL_REF, OUMUXX_SEL1_OMUXS0),
        OMUX4_SEL10, MAKE_OUMUXX(OUMUXX_SEL0_PLL_REF, OUMUXX_SEL1_OMUXS0),
        OMUX5_SEL10, MAKE_OUMUXX(OUMUXX_SEL0_PLL_REF, OUMUXX_SEL1_OMUXS0),

        OUT0_MODE, rv ? OUTMODE_LVPECL : OUTMODE_CMOS_P, // OUTMODE_CMOS_P /*OUTMODE_LVPECL*/, //OUTMODE_CMOS_P, //PLLCLK r3 -- RXCLK r2/r1
        OUT0_DIV, div,
        OUT0_SKEW, 0,
        OUT0_CMOS_INV_Z, 0,
        OUT0_CMOS_SLEW, 3,

        OUT1_MODE, !rv ? OUTMODE_LVPECL : OUTMODE_CMOS_P, //OUTMODE_CMOS_P, //RXCLK r3 -- PLLCLK r2/r1
        OUT1_DIV, 1,
        OUT1_SKEW, 0,
        OUT1_CMOS_INV_Z, 0,
        OUT1_CMOS_SLEW, 3,

        OUT2_MODE, OUTMODE_CMOS_P, //TXCLK
        OUT2_DIV, 1,
        OUT2_SKEW, 0,
        OUT2_CMOS_INV_Z, 0,
        OUT2_CMOS_SLEW, 3,

        OUT3_DIV, 0,
        OUT4_DIV, 0,
        //OUT5_DIV, 0,

        OUT5_MODE, OUTMODE_OFF, // OUTMODE_LVDS18_FAST, //usb or ref
        OUT5_DIV, 1,
        OUT5_SKEW, 0,
        OUT5_CMOS_INV_Z, 0,
        OUT5_CMOS_SLEW, 3,


        OUT3_MODE, OUTMODE_OFF, // MXLO
        OUT4_MODE, OUTMODE_OFF, // GTPREF [                N/A rev 1 ]
       // OUT5_MODE, OUTMODE_OFF, // REFCLK [ USB CLK rev 2, N/A rev 1 ]


      //  OUT4_MODE, OUTMODE_HCSL_50_INT,
      //  OUT4_DIV, div,
      //  OUT4_SKEW, 0,
      //  OUT4_CMOS_INV_Z, 0,
      //  OUT4_CMOS_SLEW, 3,

      //  OUT5_MODE, OUTMODE_CMOS_P,
      //  OUT5_DIV, div,
      //  OUT5_SKEW, 0,
      //  OUT5_CMOS_INV_Z, 0,
      //  OUT5_CMOS_SLEW, 3,

      //  CLKIN_2_CLK_SEL, 0, //CMOS AC

        OUT3210_OE, ~0,
        OUT54_OE, ~0,

        //0xB9, B9_XOSC_DIS | B9_PLL_DIS | B9_PDIV_DIS,
        //0xB9, B9_IBUF0_DIS | B9_PLL_DIS | B9_PDIV_DIS,
        //0xB9, ext_in2 ? (B9_XOSC_DIS | B9_PLL_DIS | B9_PDIV_DIS) : (B9_IBUF0_DIS | B9_PLL_DIS | B9_PDIV_DIS),
        0xB9, ext_in2 ? (B9_XOSC_DIS /*| B9_PLL_DIS | B9_PDIV_DIS*/) : (B9_IBUF0_DIS /*| B9_PLL_DIS | B9_PDIV_DIS*/),

        0xBA, /*BA_HSDIV0_DIS |*/ BA_HSDIV1_DIS | BA_HSDIV2_DIS /*| BA_HSDIV3_DIS*/ | BA_HSDIV4_DIS | BA_ID0_DIS | BA_ID1_DIS,
        //0xBA, BA_HSDIV0_DIS | BA_HSDIV1_DIS | BA_HSDIV2_DIS | BA_HSDIV3_DIS | BA_HSDIV4_DIS | BA_ID0_DIS | BA_ID1_DIS,
        0xBB, /*BB_OMUX3_DIS |*/ BB_OMUX4_DIS | BB_OMUX5_DIS,
        0xBC, /*BC_OUT3_DIS,*/ 0,
        0xBD, BD_OUT4_DIS | BD_OUT5_DIS,

        XOSC_CINT_ENA, 0,
        XOSC_CTRIM_XIN, 0,
        XOSC_CTRIM_XOUT, 0,

        USYS_CTRL, 0x02, //ACTIVE
    };

    res = si5532_get_state(dev, subdev, lsopaddr, "BEFORE INIT");
    if(res)
        return res;

    for (unsigned i = 0; i < (SIZEOF_ARRAY(program_regs_init) / 2); i++) {
        uint8_t addr = program_regs_init[2*i + 0];
        uint8_t val = program_regs_init[2*i + 1];

        res = si5332_reg_wr(dev, subdev, lsopaddr, addr, val);
        if (res)
            return res;
    }

    uint32_t oa;
    lowlevel_reg_rd32(dev, subdev, 0xC, &oa);

    return si5532_get_state(dev, subdev, lsopaddr, "AFTER INIT");;
}

int si5532_set_ext_clock_sw(lldev_t dev, subdev_t subdev, lsopaddr_t lsopaddr, bool set_flag)
{
    const uint8_t program_regs_init[] =
    {
        USYS_CTRL, 0x01, //READY
        IMUX_SEL, set_flag ? IMUX_IN_2 : IMUX_XOSC,
        CLKIN_2_CLK_SEL, set_flag ? IMUX_INX_DIFF /* IMUX_INX_CMOS_AC */ : IMUX_INX_DISABLED,
        0xB9, set_flag ? (B9_XOSC_DIS /*| B9_PLL_DIS | B9_PDIV_DIS*/) : (B9_IBUF0_DIS /*| B9_PLL_DIS | B9_PDIV_DIS*/),
        USYS_CTRL, 0x02, //ACTIVE
    };

    USDR_LOG("5332", USDR_LOG_INFO, "Si5332 ExtClock=%d\n", set_flag);

    int res = 0;

    res = si5532_get_state(dev, subdev, lsopaddr, "BEFORE SET_EX_CLK");
    if(res)
        return res;

    for (unsigned i = 0; i < (SIZEOF_ARRAY(program_regs_init) / 2); i++)
    {
        uint8_t addr = program_regs_init[2*i + 0];
        uint8_t val = program_regs_init[2*i + 1];

        res = si5332_reg_wr(dev, subdev, lsopaddr, addr, val);
        if (res)
            return res;
    }

    return si5532_get_state(dev, subdev, lsopaddr, "AFTER SET_EX_CLK");
}

// si5332 up to 3 unrelated clocks

int si5332_set_layout(lldev_t dev, subdev_t subdev, lsopaddr_t lsopaddr,
                      struct si5332_layout_info* nfo, bool old, unsigned lodiv, unsigned *vcofreq /*, unsigned* altref*/)
{
    unsigned imul = (si5332_Fvco_min + nfo->out - 1) / nfo->out;
    unsigned hsdiv;
    unsigned odiv = 1;
    unsigned vco;

    for (odiv = (imul + si5332_hsdiv_max - 1) / si5332_hsdiv_max; odiv < 64; odiv++) {
        hsdiv = (imul + odiv - 1) / odiv;
        vco = nfo->out * hsdiv * odiv;

        USDR_LOG("5332", USDR_LOG_TRACE, "Check VCO=%u HSDIV=%u ODIV=%u...\n",
                 vco, hsdiv, odiv);

        if (hsdiv < si5332_hsdiv_min)
            return -ERANGE;
        if (vco < si5332_Fvco_max)
            break;
    }
    if (odiv >= 64)
        return -ERANGE;

    unsigned prescaler = 1;
    unsigned pllfreq = nfo->infreq;
    if (pllfreq > 50e6) {
        prescaler++;
        pllfreq /= prescaler;
    }

    unsigned idpa_intg = (uint64_t)128 * vco / pllfreq;
    unsigned idpa_frac = (uint64_t)128 * vco - (uint64_t)idpa_intg * pllfreq;

    // Need to find best approxim of id_frac / pllfreq
    unsigned idpa_den = 32767;
    unsigned idpa_res = (uint64_t)idpa_den * idpa_frac / pllfreq;

    unsigned pll_freq_div = 0;

    bool jdiv = false;
    for (unsigned todiv = 1; todiv < 14; todiv++) {
        int diff = (int)(nfo->out * todiv) - (int)nfo->infreq;

        if (diff > -14 && diff < 14) {
            jdiv = true;
            odiv = todiv;
        }
    }

    if (vcofreq) {
        *vcofreq = vco;
    }
    // TODO alternative PLL ref
    // if (altref) {
    //     pll_freq_div = (vco + 41000000) / 41000000;
    //     *altref = pll_freq_div;
    // }


    USDR_LOG("5332", USDR_LOG_INFO, "VCO=%u IDPA_INTG=%u IDPA_RES=%u HSDIV=%u ODIV=%u JDIV=%d MXLO=%u\n",
             vco, idpa_intg, idpa_res, hsdiv, odiv, jdiv, vco / lodiv);

        // terms of an a + b/c desired divider settingmust be processed into
        //IDPA_INTG, ID-PA_RES, and IDPA_DEN register
        //terms.intg =floor(((a*c+b)*128/c) - 512).
        //res = mod(b*128, c)

    // slew 0 -- fastest ; 3 -- slowest
    const uint8_t program_regs_init[] = {
        USYS_CTRL, 0x01, //READY

        IDPA_INTG_H, idpa_intg,
        IDPA_INTG_L, (idpa_intg >> 8),
        IDPA_RES_H, idpa_res,
        IDPA_RES_L, idpa_res >> 8,
        IDPA_DEN_H, idpa_den,
        IDPA_DEN_L, idpa_den >> 8,

        PDIV_DIV, prescaler, //Prescaler
        PLL_MODE,  (pllfreq > 30e6) ? 11 : 4, // 4 - 500kHz  | 7 - 175khz

        HSDIV0A_DIV, hsdiv,
        HSDIV0B_DIV, hsdiv,

        HSDIV1A_DIV, pll_freq_div,
        HSDIV2A_DIV, pll_freq_div,

        old ? OUT0_CMOS_SLEW : OUT1_CMOS_SLEW, (nfo->out > 110e6) ? 1 : (nfo->out > 50e6) ? 1 : 2,
        OUT2_CMOS_SLEW, (nfo->out > 110e6) ? 1 : (nfo->out > 50e6) ? 1 : 2,

        old ? OUT0_DIV : OUT1_DIV, odiv,
        OUT2_DIV, odiv,

        old ? OMUX0_SEL10 : OMUX1_SEL10, jdiv ? MAKE_OUMUXX(OUMUXX_SEL0_PLL_REF, OUMUXX_SEL1_OMUXS0) :
            MAKE_OUMUXX(OUMUXX_SEL0_PLL_REF, OUMUXX_SEL1_HSDIV0),
        OMUX2_SEL10, jdiv ? MAKE_OUMUXX(OUMUXX_SEL0_PLL_REF, OUMUXX_SEL1_OMUXS0) :
            MAKE_OUMUXX(OUMUXX_SEL0_PLL_REF, OUMUXX_SEL1_HSDIV0),

        HSDIV3A_DIV, lodiv,
        HSDIV3B_DIV, lodiv,

        OMUX3_SEL10, MAKE_OUMUXX(OUMUXX_SEL0_PLL_REF, OUMUXX_SEL1_HSDIV3),
        OUT3_DIV, 1,
        OUT3_MODE, OUTMODE_HCSL_50_INT, //OUTMODE_LVPECL, //OUTMODE_HCSL_50_INT, //OUTMODE_LVDS18_FAST,


        USYS_CTRL, 0x02, //ACTIVE
    };

    int res = si5532_get_state(dev, subdev, lsopaddr, "BEFORE SET_LAYOUT");
    if(res)
        return res;

    for (unsigned i = 0; i < (SIZEOF_ARRAY(program_regs_init) / 2); i++) {
        uint8_t addr = program_regs_init[2*i + 0];
        uint8_t val = program_regs_init[2*i + 1];

        res = si5332_reg_wr(dev, subdev, lsopaddr, addr, val);
        if (res)
            return res;
    }

    return si5532_get_state(dev, subdev, lsopaddr, "AFTER SET_LAYOUT");
}

int si5332_set_port3_en(lldev_t dev, subdev_t subdev, lsopaddr_t lsopaddr, bool loen, bool txen)
{
    const uint8_t program_regs_init[] = {
        // Disable mixer LO
        // OUT3210_OE, ~0,

        OUT3210_OE, (loen ? B6_OUT3_OE : 0) | (txen ? B6_OUT2_OE : 0) |  B6_OUT0_OE | B6_OUT1_OE,

        USYS_CTRL, 0x01, //READY
        0xBA, (!loen ? BA_HSDIV3_DIS : 0) | BA_HSDIV1_DIS | BA_HSDIV2_DIS | BA_HSDIV4_DIS | BA_ID0_DIS | BA_ID1_DIS,
        0xBB, (!loen ? BB_OMUX3_DIS : 0) | (!txen ? BB_OMUX2_DIS : 0) | BB_OMUX4_DIS | BB_OMUX5_DIS,
        0xBC, (!loen ? BC_OUT3_DIS : 0) | (!txen ? BC_OUT2_DIS : 0),
        USYS_CTRL, 0x02, //ACTIVE
    };

    for (unsigned i = 0; i < (SIZEOF_ARRAY(program_regs_init) / 2); i++) {
        uint8_t addr = program_regs_init[2*i + 0];
        uint8_t val = program_regs_init[2*i + 1];

        int res = si5332_reg_wr(dev, subdev, lsopaddr, addr, val);
        if (res)
            return res;
    }

    USDR_LOG("5332", USDR_LOG_INFO, "MXLO_EN=%d TXCLK_EN=%d\n", loen, txen);
    return 0;
}


#if 0
int si5332_set_idfreq(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                      unsigned idfreq)
{
    unsigned vco;



    const uint8_t program_regs_init[] = {
        USYS_CTRL, 0x01, //READY


        OUT3_MODE, idfreq == 0 ? OUTMODE_OFF : OUTMODE_LVDS18_FAST, // MXLO


        USYS_CTRL, 0x02, //ACTIVE
    };


}
#endif
