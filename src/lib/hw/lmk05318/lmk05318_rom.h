// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdbool.h>

enum empiric_type
{
    REG_READONLY = 1,
    REG_DEFAULTS = 2,
    REG_OVERRIDE = 4,
    REG_PLL1     = 8,
    REG_PLL2     = 16,
    REG_DPLL     = 32,
    REG_XO       = 64,
    REG_LDO      = 128,
    REG_NVM      = 256,
    REG_UNKNOWN  = 512,
};
typedef enum empiric_type empiric_type_t;

struct empiric
{
    uint32_t reg;
    empiric_type_t type;
};
typedef struct empiric empiric_t;

static const empiric_t lmk05318_rom_dpll_empiric[] =
{
    {0x000d00, REG_READONLY | REG_XO | REG_PLL1 | REG_PLL2}, // [RO] APLL&XO LOSSes
    {0x000e00, REG_READONLY | REG_DPLL},                     // [RO] DPLL LOSSes
    {0x000f00, REG_DEFAULTS},                                // [DEF] INT_MASK
    {0x001000, REG_DEFAULTS},                                // [DEF] INT_MASK
    {0x001100, REG_DEFAULTS | REG_XO | REG_PLL1 | REG_PLL2}, // [DEF] XO/APLL LOL Polarity
    {0x001200, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL LOL Polarity
    {0x001500, REG_DEFAULTS},                                // [DEF] INT_AND_OR
    {0x001600, REG_DEFAULTS},                                // [DEF] STATUS0/1 Polarity
    //{0x001900, REG_OVERRIDE},                                // OUT MUTE ctrls (unmute)
    {0x001a00, REG_DEFAULTS | REG_XO},                       // [DEF] XO hyst timer
    {0x001b00, REG_OVERRIDE | REG_XO},                       // XO SE energy det mode
    {0x001c00, REG_OVERRIDE | REG_XO},                       // XO DIFF energy det mode
    //{0x001d00, REG_OVERRIDE},                                // mute during LOCK ?? 0x17
    {0x001e00, REG_OVERRIDE | REG_LDO},                      // LDO timer scale
    {0x002000, REG_OVERRIDE | REG_LDO | REG_PLL1 | REG_PLL2},// APLL MASH LDO trim
    {0x002300, REG_DEFAULTS | REG_LDO | REG_PLL1 | REG_PLL2},// [DEF] VCO1/2 LDO settings
    {0x002400, REG_DEFAULTS},                                // [DEF] STAT0/1 Driver Type Output
    {0x002500, REG_OVERRIDE},                                // STATUS0 open Drain mode
    {0x002600, REG_OVERRIDE},                                // STATUS1 open Drain mode
    {0x002900, REG_DEFAULTS | REG_NVM},                      // [DEF] SPARE_NVMBASE2
    {0x003000, REG_OVERRIDE},                                // STAT0_SEL
    {0x003100, REG_OVERRIDE},                                // STAT1_SEL
    {0x003200, REG_DEFAULTS},                                // [DEF] DCO freq + CH6/7 PD
    {0x004400, REG_OVERRIDE | REG_PLL1},                     // APLL1 BAW drain
    {0x004500, REG_OVERRIDE | REG_UNKNOWN},                  // unknown
    {0x004800, REG_READONLY},                                // [RO] RO OUTCH active flags
    {0x004900, REG_DEFAULTS},                                // [DEF] REF_BYPASS (route REF to OUTs instead of VCO1)
    {0x004b00, REG_OVERRIDE | REG_PLL1},                     // PLL1 Vtune Monitor bypass + charge pump gain
    {0x004c00, REG_OVERRIDE | REG_PLL1},                     // PLL1 post-div 1
    {0x004d00, REG_OVERRIDE | REG_PLL1},                     // PLL1 loop filter
    {0x004e00, REG_DEFAULTS | REG_PLL1},                     // [DEF] PLL1 bleed resistor
    {0x006500, REG_OVERRIDE | REG_PLL2},                     // PLL2 charge pump
    {0x006633, REG_OVERRIDE | REG_PLL2},                     // PLL2 post-div2 post-div1
    {0x006700, REG_OVERRIDE | REG_PLL2},                     // PLL2 Loop filter
    {0x006800, REG_DEFAULTS | REG_PLL2},                     // [DEF] PLL2 bleed resistor
    {0x006900, REG_DEFAULTS | REG_PLL2},                     // [DEF] PLL2 closed loop wait + VCO wait
    {0x006a00, REG_OVERRIDE | REG_PLL1},                     // PLL1 N Delay div (MSB)
    {0x006b00, REG_OVERRIDE | REG_PLL1},                     // PLL1 N Delay div
    {0x007500, REG_DEFAULTS | REG_PLL1},                     // [DEF] PLL1_FDEV (MSB)
    {0x007600, REG_DEFAULTS | REG_PLL1},                     // [DEF] PLL1_FDEV def
    {0x007700, REG_DEFAULTS | REG_PLL1},                     // [DEF] PLL1_FDEV def
    {0x007800, REG_DEFAULTS | REG_PLL1},                     // [DEF] PLL1_FDEV def
    {0x007900, REG_DEFAULTS | REG_PLL1},                     // [DEF] PLL1_FDEV def
    {0x007a00, REG_DEFAULTS | REG_PLL1},                     // [DEF] PLL1 inc/dec ctrl
    {0x007b00, REG_DEFAULTS | REG_PLL1},                     // [DEF] PLL1_NUM_STAT (MSB)
    {0x007c00, REG_DEFAULTS | REG_PLL1},                     // [DEF] PLL1_NUM_STAT
    {0x007d00, REG_DEFAULTS | REG_PLL1},                     // [DEF] PLL1_NUM_STAT
    {0x007e00, REG_DEFAULTS | REG_PLL1},                     // [DEF] PLL1_NUM_STAT
    {0x007f00, REG_DEFAULTS | REG_PLL1},                     // [DEF] PLL1_NUM_STAT
    {0x008000, REG_DEFAULTS | REG_PLL1},                     // [DEF] PLL1 NUM saturation
    {0x008100, REG_OVERRIDE | REG_PLL1},                     // PLL1 Loop Filter R2
    {0x008200, REG_DEFAULTS | REG_PLL1},                     // [DEF] PLL1 loop filter C1
    {0x008300, REG_OVERRIDE | REG_PLL1},                     // PLL1 Loop Filter R3
    {0x008400, REG_OVERRIDE | REG_PLL1},                     // PLL1 Loop Filter R4
    {0x008500, REG_DEFAULTS | REG_PLL1},                     // [DEF] PLL1 Loop Filter C4
    {0x008600, REG_OVERRIDE | REG_PLL2},                     // Bit 8 of PLL2_NDIV
    {0x00872c, REG_OVERRIDE | REG_PLL2},                     // Bits 7:0 of PLL2 NDIV
    {0x00887c, REG_OVERRIDE | REG_PLL2},                     // PLL2_NUM (MSB)
    {0x0089e0, REG_OVERRIDE | REG_PLL2},                     // PLL2_NUM
    {0x008abf, REG_OVERRIDE | REG_PLL2},                     // PLL2_NUM
    //{0x008b00, REG_DEFAULTS | REG_PLL2},                     // [DEF] SDM Dither Mode + APLL2 SDM Order
    {0x008c00, REG_OVERRIDE | REG_PLL2},                     // PLL2 Loop Filter R2
    {0x008d00, REG_DEFAULTS | REG_PLL2},                     // [DEF] PLL2 Loop Filter C1
    {0x008e00, REG_OVERRIDE | REG_PLL2},                     // PLL2 Loop Filter R3
    {0x008f00, REG_OVERRIDE | REG_PLL2},                     // PLL2 Loop Filter R4
    {0x009000, REG_DEFAULTS | REG_PLL2},                     // [DEF] PLL2 Loop Filter C4/C3
    {0x009100, REG_OVERRIDE | REG_XO},                       // XO Input Wait Timer
    {0x009200, REG_OVERRIDE | REG_UNKNOWN},                  // unknown
    {0x009300, REG_OVERRIDE | REG_UNKNOWN},                  // unknown
    {0x009500, REG_OVERRIDE | REG_UNKNOWN},                  // unknown
    {0x009600, REG_DEFAULTS | REG_PLL1},                     // [DEF] PLL1 Amp Cal up/low threhold def
    {0x009700, REG_OVERRIDE | REG_UNKNOWN},                  // unknown
    {0x009800, REG_OVERRIDE | REG_UNKNOWN},                  // unknown
    {0x009900, REG_OVERRIDE | REG_PLL1},                     // PLL1 Amp Cal up/low threhold
    {0x009a00, REG_OVERRIDE | REG_LDO},                      // LDO trim bits
    {0x009b00, REG_READONLY | REG_NVM},                      // [RO] NVM Stored CRC
    {0x009c00, REG_READONLY | REG_NVM},                      // [RO] NVM Program Count
    {0x009e00, REG_READONLY | REG_NVM},                      // [RO] MUMLCRC
    {0x009f00, REG_DEFAULTS | REG_NVM},                      // [DEF] Bits 12:8 of MEMADR
    {0x00a000, REG_DEFAULTS | REG_NVM},                      // [DEF] Memory Address
    {0x00a100, REG_READONLY | REG_NVM},                      // [RO]EEPROM Read Data
    {0x00a200, REG_DEFAULTS | REG_NVM},                      // [DEF] RAM Read/Write Data
    {0x00a500, REG_DEFAULTS | REG_NVM},                      // [DEF] NVM BASE unlock
    {0x00a700, REG_OVERRIDE | REG_UNKNOWN},                  // unknown
    {0x00b200, REG_OVERRIDE | REG_UNKNOWN},                  // unknown
    {0x00b400, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_TUNING_FREE_RUN (MSB)
    {0x00b500, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_TUNING_FREE_RUN
    {0x00b600, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_TUNING_FREE_RUN
    {0x00b700, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_TUNING_FREE_RUN
    {0x00b800, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_TUNING_FREE_RUN
    {0x00b9f1, REG_OVERRIDE | REG_DPLL},                     // DPLL_REF_HIST_EN=enabled
    {0x00ba01, REG_OVERRIDE | REG_DPLL},                     // DPLL REF Tuning History Timer
    {0x00bb00, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_REF_HISTDLY (MSB)
    {0x00bc00, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_REF_HISTDLY
    {0x00bd00, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_REF_HISTDLY
    {0x00be00, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_REF_HISTDLY
    {0x00bf00, REG_OVERRIDE | REG_UNKNOWN},                  // unknown
    {0x00c000, REG_OVERRIDE | REG_DPLL},                     // PRI/SECREF detectors settings
    {0x00d800, REG_OVERRIDE | REG_UNKNOWN},                  // unknown
    {0x00eb02, REG_OVERRIDE | REG_DPLL},                     // PRIREF_PH_VALID_DET (MSB)
    {0x00ecfa, REG_OVERRIDE | REG_DPLL},                     // PRIREF_PH_VALID_DET
    {0x00edf0, REG_OVERRIDE | REG_DPLL},                     // PRIREF_PH_VALID_DET
    {0x00ee80, REG_OVERRIDE | REG_DPLL},                     // PRIREF_PH_VALID_DET
    {0x00ef00, REG_OVERRIDE | REG_DPLL},                     // SECREF_PH_VALID_DET (MSB)
    {0x00f000, REG_OVERRIDE | REG_DPLL},                     // SECREF_PH_VALID_DET
    {0x00f100, REG_OVERRIDE | REG_DPLL},                     // SECREF_PH_VALID_DET
    {0x00f200, REG_OVERRIDE | REG_DPLL},                     // SECREF_PH_VALID_DET
    {0x00fa00, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_VAL_FL(PL)_EN
    {0x00fd00, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL switchover timer
    {0x00fe00, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL switchover timer
    {0x00ff00, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL switchover timer
    {0x010402, REG_OVERRIDE | REG_DPLL},                     // DPLL_REF_AVOID_SLIP(en) + TDC software ctrl(dis)
    {0x010580, REG_OVERRIDE | REG_DPLL},                     // DPLL Reference Control
    {0x010601, REG_OVERRIDE | REG_DPLL},                     // DPLL Reference Control
    {0x01072a, REG_OVERRIDE | REG_DPLL},                     // DPLL Reference Control
    {0x010805, REG_OVERRIDE | REG_DPLL},                     // DPLL Reference Control
    {0x0109f2, REG_OVERRIDE | REG_DPLL},                     // DPLL Reference Control
    {0x010a00, REG_OVERRIDE | REG_DPLL},                     // DPLL Reference Control
    {0x010ba0, REG_OVERRIDE | REG_DPLL},                     // DPLL Reference Control + DPLL loop filter
    {0x010c00, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x010d00, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x010e02, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x010fa6, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x011000, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x011100, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x011200, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x011316, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x011416, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x011516, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x011600, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x011700, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x011800, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x011900, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x011a00, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x011b00, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x011c1e, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x011d1e, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x011e00, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x011f00, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x012000, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x012100, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x012203, REG_OVERRIDE | REG_DPLL},                     // DPLL Phase Lock Detection
    {0x012322, REG_OVERRIDE | REG_DPLL},                     // DPLL Phase Lock Detection
    {0x012400, REG_OVERRIDE | REG_DPLL},                     // Phase Cancellation for Hitless Switching
    {0x012500, REG_OVERRIDE | REG_DPLL},                     // Phase Cancellation for Hitless Switching
    {0x012600, REG_OVERRIDE | REG_DPLL},                     // Phase Cancellation for Hitless Switching
    {0x012700, REG_OVERRIDE | REG_DPLL},                     // Phase Cancellation for Hitless Switching
    {0x01280a, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x01290a, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x012a0a, REG_OVERRIDE | REG_DPLL},                     // DPLL Loop Filter
    {0x012b00, REG_OVERRIDE | REG_UNKNOWN},                  // unknown
    {0x012c00, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL Phase Lock Detection
    {0x012d1c, REG_OVERRIDE | REG_DPLL},                     // Phase lock declaration threshold
    {0x012e1e, REG_OVERRIDE | REG_DPLL},                     // Phase un-lock declaration threshold
    {0x012f01, REG_OVERRIDE | REG_UNKNOWN},                  // unknown
    {0x013f03, REG_OVERRIDE | REG_DPLL},                     // DPLL Reference Control
    {0x014000, REG_OVERRIDE | REG_DPLL},                     // DPLL DCO Lock Detection
    {0x01410a, REG_OVERRIDE | REG_DPLL},                     // DPLL DCO Lock Detection
    {0x014200, REG_OVERRIDE | REG_DPLL},                     // DPLL DCO Lock Detection
    {0x014300, REG_OVERRIDE | REG_DPLL},                     // DPLL DCO Lock Detection
    {0x014400, REG_OVERRIDE | REG_DPLL},                     // DPLL DCO Lock Detection
    {0x014501, REG_OVERRIDE | REG_DPLL},                     // DPLL DCO Lock Detection
    {0x014606, REG_OVERRIDE | REG_DPLL},                     // DPLL DCO Lock Detection
    {0x014735, REG_OVERRIDE | REG_DPLL},                     // DPLL DCO Lock Detection
    {0x014875, REG_OVERRIDE | REG_DPLL},                     // DPLL DCO Lock Detection
    {0x01490b, REG_OVERRIDE | REG_DPLL},                     // DPLL DCO Lock Detection
    {0x014a00, REG_OVERRIDE | REG_DPLL},                     // DPLL DCO Unlock Detection
    {0x014b64, REG_OVERRIDE | REG_DPLL},                     // DPLL DCO Unlock Detection
    {0x014c00, REG_OVERRIDE | REG_DPLL},                     // DPLL DCO Unlock Detection
    {0x014d00, REG_OVERRIDE | REG_PLL2},                     // PLL2 DEN (MSB)
    {0x014e61, REG_OVERRIDE | REG_PLL2},                     // PLL2 DEN
    {0x014fa8, REG_OVERRIDE | REG_PLL2},                     // PLL2 DEN
    {0x015006, REG_OVERRIDE | REG_DPLL},                     // DPLL DCO Unlock Detection
    {0x015135, REG_OVERRIDE | REG_DPLL},                     // DPLL DCO Unlock Detection
    {0x015275, REG_OVERRIDE | REG_DPLL},                     // DPLL DCO Unlock Detection
    {0x01530b, REG_OVERRIDE | REG_PLL1},                     // PLL1 24-b NUM MSB (not used in DPLL mode)
    {0x015400, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_REF_SYNC_PH_OFFSET
    {0x015500, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_REF_SYNC_PH_OFFSET
    {0x015600, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_REF_SYNC_PH_OFFSET
    {0x015700, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_REF_SYNC_PH_OFFSET
    {0x015800, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_REF_SYNC_PH_OFFSET
    {0x015900, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL REF Zero Delay Mode Phase Offset
    {0x015a00, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL Freq Incr/Decr enable via pin/reg
    {0x015b00, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_FDEV
    {0x015c00, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_FDEV
    {0x015d00, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_FDEV
    {0x015e00, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL_FDEV
    {0x015f00, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL Freq Incr/Decr Numerator Step Word (MSB)
    {0x016000, REG_DEFAULTS | REG_DPLL},                     // [DEF] DPLL Freq Incr/Decr Numerator Step Word
};

static const uint32_t lmk05318_rom_dpll[] =
{
    //0x000000,
    //0x000100,
    //0x000200,
    //0x000300,
    //0x000400,
    //0x000500,  R
    //0x000600,
    //0x000700,
    //0x000800,
    //0x000A00,
    //0x000B00,

    0x000C1B, //ok
    //0x000D00, R
    //0x000E00, R
    //0x000F00, // ok default
    //0x001000, // ok default
    //0x001100, // ok default
    //0x001200, // ok default
    0x001300, // ok
    0x001400, // ok
    //0x001500, // ok default
    //0x001600, // ok default
    0x001700,   // ok, 0xff - mute specifics
    0x001800,   // ok, 0xff - mute specifics
    0x001900,   // ok
    0x001A00, // XO hyst timer
    0x001B00, // XO SE energy det mode
    0x001C00, // XO DIFF energy det mode
    0x001D17, // ************************? DPLL mute on ph lock
    0x001E00, // *********? LDO timer scale
    0x002000, // *********? PLL1/2 MASH LDO trim
    //0x002300, // ok default
    //0x002400, // ok default
    //0x002500, //default STATUS0 open Drain mode
    //0x002600,//default
    0x002700,  // ok
    0x002806,  // ok (?)
    //0x002900, // default SPARE_NVMBASE2
    0x002A10,   // ok (? XO inp buffer=0)
    0x002B40,   // ok (default bits?)
    0x002C00,   //ok
    0x002D07,   //ok (default bits)
    0x002E01,   //ok
    0x002F00,   //ok (counters differ)
    //0x003000,   // ok stats
    //0x003100,   // ok stats
    //0x003200,   //default
    0x003310, //ok
    0x003410, //ok
    0x003513, //ok
    0x003610, //ok
    0x003710, //ok
    0x003809, //ok
    0x003900, //ok
    0x003A0F, //ok
    0x003B00, //ok
    0x003C0F, //ok
    0x003D3E, //ok
    0x003EF9, //ok
    0x003F3E, //ok
    0x004095, //ok
    0x004102, //ok
    0x0042F8, //ok
    0x0043FF, //ok
    0x004400, // ????? APLL1 BAW drain != default TODO
    //0x004500, //default
    0x004607, //ok
    0x00473F, //ok
    //0x004800, R
    //0x004900, //default
    0x004A00, //ok
    0x004B00, //???? !=default
    0x004C00, //APLL1 Post div1???? todo
    0x004D00, //PLL1 loop filter?.. todo
    //0x004E00, ??? unknown
    0x004F00, //ok (default bit)
    0x005000, //ok
    0x00510A, //ok
    0x005200, //ok
    0x00530E, //ok
    0x0054A6, //ok
    0x005500, //ok
    0x005600, //ok
    0x00571E, //ok
    0x005884, //ok
    0x005980, //ok
    0x005A00, //ok
    0x005B14, //ok
    0x005C00, //ok
    0x005D0E, //ok
    0x005EA6, //ok
    0x005F00, //ok
    0x006000, //ok
    0x00611E, //ok
    0x006284, //ok
    0x006380, //ok
    0x006429, //ok
    //0x006500, // != default, PLL2 charge pump
    //0x006633, //PLL2 (off)
    //0x006700, //PLL2 Loop filter sett
    //0x006800, //PLL2 (off) default
    //0x006900, //PLL2
    0x006A00, // PLL1 N Delay div (!=default)
    0x006B00, // PLL1 N Delay div (!=default)
    0x006C00, //ok
    0x006D32, //ok
    0x006E00, //ok
    0x006F00, //ok
    0x007000, //ok
    0x007100, //ok
    0x007200, //ok
    0x007303, //ok
    0x007401, //ok

    //0x007500, //PLL1_FDEV ==default
    //0x007600, //PLL1_FDEV ==default
    //0x007700, //PLL1_FDEV ==default
    //0x007800, //PLL1_FDEV ==default
    //0x007900, //PLL1_FDEV ==default
    //0x007A00, //PLL1_FDEV ==default
    //0x007B00, // PLL1_NUM_STAT_39:32
    //0x007C00, // PLL1_NUM_STAT_31:24
    //0x007D00, // PLL1_NUM_STAT_23:16
    //0x007E00, // PLL1_NUM_STAT_15:8
    //0x007F00, // PLL1_NUM_STAT
    //0x008000, //PLL1 NUM saturation(default)
    0x008100, // PLL1 Loop Filter R2 ??? !=default
    //0x008200, // PLL1 loop filter C1 ==default
    0x008300, // PLL1 Loop Filter R3 ??? != default
    0x008400, // PLL1 Loop Filter R4 ??? != default
    //0x008500, // PLL1 Loop Filter C4 ==default
    //0x008600, // Bit 8 of PLL2_NDIV
    //0x00872C, // Bits 7:0 of PLL2 N Divider
    //0x00887C, // Bits 23:16 of PLL2_NUM
    //0x0089E0, // Bits 15:8 of PLL2_NUM
    //0x008ABF, // PLL2 Fractional Divider Numerator
    //0x008B00, // SDM Dither Mode + APLL2 SDM Order
    //0x008C00, // PLL2 Loop Filter R2
    //0x008D00, // PLL2 Loop Filter C1 == default
    //0x008E00, // PLL2 Loop Filter R3
    //0x008F00, // PLL2 Loop Filter R4
    //0x009000, // PLL2 Loop Filter C4/C3
    0x009100, // XO Input Wait Timer ??? != default
    0x009200, // ??? unknown !=default 0x84
    //0x009300, // ??? unknown ==default
    //0x009500, // ??? unknown ==default
    //0x009600, //APPL1 Amp Cal up/low threhold ==default
    //0x009700, // ??? unknown ==default
    //0x009800, // ??? unknown ==default
    //0x009900, //APPL2 Amp Cal up/low threhold ==default
    0x009A00, // LDO trim bits ??? != default
    //0x009B00, // NVM Stored CRC RO
    //0x009C00, // NVM Program Count RO
    //0x009D00, // NVM misc
    //0x009E00, //MUMLCRC RO
    //0x009F00, // Bits 12:8 of MEMADR ==default
    //0x00A000, // Memory Address ==default
    //0x00A100, // EEPROM Read Data RO
    //0x00A200, // RAM Read/Write Data ==default
    //0x00A400, // NVM Program Unlock
    //0x00A500, // NVM BASE unlock ==default
    //0x00A700, // unknown ==default
    //0x00B200, // unknown ==default
    //0x00B400, // Bits 37:32 of DPLL_TUNING_FREE_RUN ==default
    //0x00B500, // Bits 31:24 of DPLL_TUNING_FREE_RUN ==default
    //0x00B600, // Bits 23:16 of DPLL_TUNING_FREE_RUN ==default
    //0x00B700, // Bits 15:8 of DPLL_TUNING_FREE_RUN ==default
    //0x00B800, // DPLL Free-run tuning word ==default
    0x00B9F1, // DPLL_REF_HIST_EN=enabled !=default ?????
    0x00BA01, // DPLL REF Tuning History Timer !=default ???
    //0x00BB00, //DPLL_REF_HISTDLY ==default
    //0x00BC00, //DPLL_REF_HISTDLY ==default
    //0x00BD00, //DPLL_REF_HISTDLY ==default
    //0x00BE00, //DPLL_REF_HISTDLY ==default
    //0x00BF00, //unknown ==default
    0x00C000,  // PRI/SECREF detectors settings !=default ???

    0x00C119, //ok
    0x00C200, //ok
    0x00C300, //ok
    0x00C400, //ok
    0x00C51D, //ok
    0x00C600, //ok
    0x00C700, //ok
    0x00C800, //ok
    0x00C900, //ok
    0x00CA00, //ok
    0x00CB00, //ok
    0x00CC15, //ok
    0x00CD00, //ok
    0x00CE00, //ok
    0x00CF00, //ok
    0x00D000, //ok
    0x00D100, //ok
    0x00D200, //ok
    0x00D300, //ok
    0x00D400, //ok
    0x00D500, //ok
    0x00D600, //ok
    0x00D700, //ok
    //0x00D800, //unknown
    0x00D900, //ok
    0x00DA00, //ok
    0x00DB00, //ok
    0x00DC00, //ok
    0x00DD00, //ok
    0x00DE00, //ok
    0x00DF00, //ok
    0x00E000, //ok
    0x00E100, //ok
    0x00E200, //ok
    0x00E300, //ok
    0x00E400, //ok
    0x00E500, //ok
    0x00E600, //ok
    0x00E700, //ok
    0x00E800, //ok
    0x00E90F, //ok
    0x00EA00, //ok

    0x00EB02, //PRIREF_PH_VALID_DET1 !=default
    0x00ECFA,
    0x00EDF0,
    0x00EE80, //
    0x00EF00,
    0x00F000,
    0x00F100,
    0x00F200, //SECREF_PH_VALID_DET4 !=default

    0x00F33F, //ok
    0x00F400, //ok
    0x00F921, //ok
    //0x00FA00, //default
    0x00FB03, //ok
    0x00FC29, //ok +++ZDM
    //0x00FD00, default
    //0x00FE00, default
    //0x00FF00, default

    0x010000, //ok
    0x010101, //ok
    0x010200, //ok
    0x010300, //ok

    0x010402, // DPLL_REF_AVOID_SLIP !=default
    0x010580, // DPLL Reference Control
    0x010601, // DPLL Reference Control
    0x01072A, // DPLL Reference Control
    0x010805, // DPLL Reference Control
    0x0109F2, // DPLL Reference Control
    0x010A00, // DPLL Reference Control
    0x010BA0, // DPLL Reference Control + DPLL Loop Filter
    0x010C00, // DPLL Reference Control
    0x010D00, // DPLL Reference Control
    0x010E02, // DPLL Loop Filter
    0x010FA6, // DPLL Loop Filter
    0x011000, // DPLL Loop Filter
    0x011100, // DPLL Loop Filter
    0x011200, // DPLL Loop Filter
    0x011316, // DPLL Loop Filter
    0x011416, // DPLL Loop Filter
    0x011516, // DPLL Loop Filter
    0x011600, // DPLL Loop Filter
    0x011700, // DPLL Loop Filter
    0x011800, // DPLL Loop Filter
    0x011900, // DPLL Loop Filter
    0x011A00, // DPLL Loop Filter
    0x011B00, // DPLL Loop Filter
    0x011C1E, // DPLL Loop Filter
    0x011D1E, // DPLL Loop Filter
    0x011E00, // DPLL Loop Filter
    0x011F00, // DPLL Loop Filter
    0x012000, // DPLL Loop Filter
    0x012100, // DPLL Loop Filter
    0x012203, // DPLL Phase Lock Detection
    0x012322, // DPLL Phase Lock Detection
    0x012400, // Phase Cancellation for Hitless Switching
    0x012500, // Phase Cancellation for Hitless Switching
    0x012600, // Phase Cancellation for Hitless Switching
    0x012700, // Phase Cancellation for Hitless Switching
    0x01280A, // DPLL Loop Filter
    0x01290A, // DPLL Loop Filter
    0x012A0A, // DPLL Loop Filter
    //0x012B00, // unknown
    0x012C00, // DPLL Phase Lock Detection
    0x012D1C, // Phase lock declaration threshold
    0x012E1E, // Phase un-lock declaration threshold
    //0x012F01, // unknown?????????????

    0x01300F, //ok
    0x013104, //ok
    0x013261, //ok
    0x0133F8, //ok
    0x013443, //ok
    0x0135C3, ///////
    0x0136C3, ///////
    0x0137C3, ///////
    0x0138C3, ///////
    0x0139C3, ///////
    0x013AFF, /////// DPLL NUM/DEN (may differ)
    0x013BFF, ///////
    0x013CFF, ///////
    0x013DFF, ///////
    0x013EFF, ///////

    0x013F03, // DPLL Reference Control != default
    0x014000, // DPLL DCO Lock Detection
    0x01410A, // DPLL DCO Lock Detection
    0x014200, // DPLL DCO Lock Detection
    0x014300, // DPLL DCO Lock Detection
    0x014400, // DPLL DCO Lock Detection
    0x014501, // DPLL DCO Lock Detection
    0x014606, // DPLL DCO Lock Detection
    0x014735, // DPLL DCO Lock Detection
    0x014875, // DPLL DCO Lock Detection
    0x01490B, // DPLL DCO Lock Detection
    0x014A00, // DPLL DCO Unlock Detection
    0x014B64, // DPLL DCO Unlock Detection
    0x014C00, // DPLL DCO Unlock Detection

    //0x014D00,
    //0x014E61, /* PLL2 den */
    //0x014FA8,

    0x015006, // DPLL DCO Unlock Detection != default
    0x015135, // DPLL DCO Unlock Detection
    0x015275, // DPLL DCO Unlock Detection

    0x01530B, /* Ok PLL1 num*/

    //0x015400, // DPLL_REF_SYNC_PH_OFFSET ==default
    //0x015500, // DPLL_REF_SYNC_PH_OFFSET ==default
    //0x015600, // DPLL_REF_SYNC_PH_OFFSET ==default
    //0x015700, // DPLL_REF_SYNC_PH_OFFSET ==default
    //0x015800, // DPLL_REF_SYNC_PH_OFFSET ==default
    //0x015900, // DPLL REF Zero Delay Mode Phase Offset ==default
    //0x015A00, // DPLL Freq Incr/Decr enable via pin or reg control ==default
    //0x015B00, // DPLL_FDEV ==default
    //0x015C00, // DPLL_FDEV ==default
    //0x015D00, // DPLL_FDEV ==default
    //0x015E00, // DPLL_FDEV ==default
    //0x015F00, // DPLL Freq Incr/Decr Numerator Step Word ==default
    //0x016000, // DPLL Freq Incr/Decr register control ==default

    //0x016500,
    //0x016F00,  R
    //0x019B00,
};


static const uint32_t lmk05318_rom_dpll____[] =
{
/*
        0x000000,
        0x000100,
        0x000200,
        0x000300,
        0x000400,
        0x000500,  // R
        0x000600,
        0x000700,
        0x000800,
        0x000A00,
        0x000B00,
*/
        0x000C1B,
        0x000D00,
        0x000E00,
        0x000F00,
        0x001000,
        0x001100,
        0x001200,
        0x001300,
        0x001400,
        0x001500,
        0x001600,
        0x001700,
        0x001800,
        0x001900,
        0x001A00,
        0x001B00,
        0x001C00,
        0x001D00,
        0x001E00,
        0x002000,
        0x002300,
        0x002400,
        0x002500,
        0x002600,
        0x002700,
        0x002806,
        0x002900,
        0x002A10,
        0x002B40,
        0x002C00,
        0x002D06,
        0x002E01,
        0x002F00,
        0x003000,
        0x003100,
        0x003200,
        0x003310,
        0x003410,
        0x003513,
        0x003610,
        0x003710,
        0x003809,
        0x003900,
        0x003A0F,
        0x003B00,
        0x003C0F,
        0x003D3E,
        0x003EF9,
        0x003F3E,
        0x004095,
        0x004102,
        0x0042F8,
        0x0043FF,

        0x004400, // APLL1 Charge Pump Current Gain
        0x004500, // ?

        0x004600,
        0x004700,

        0x004800, // ?
        0x004900, // ?

        0x004A00,

        0x004B00, // ?
        0x004C00, // ?
        0x004D00, // ?
        0x004E00, // ?

        0x004F00,
        0x005000,
        0x00510A,
        0x005200,
        0x00530E,
        0x0054A6,
        0x005500,
        0x005600,
        0x00571E,
        0x005884,
        0x005980,
        0x005A00,
        0x005B14,
        0x005C00,
        0x005D0E,
        0x005EA6,
        0x005F00,
        0x006000,
        0x00611E,
        0x006284,
        0x006380,
        0x006429,

        0x006500, // PLL2 Charge Pump Gain
        0x006633, // PLL2 Post-Divider 1/2
        0x006700, // ?
        0x006800, // PLL2 Bleed resistor selection
        0x006900, // Closed Loop Wait Period
        0x006A00, // ?
        0x006B00, // ?

        0x006C00,
        0x006D32,
        0x006E00,
        0x006F00,
        0x007000,
        0x007100,
        0x007200,
        0x007303,
        0x007401,

        0x007500, // ?
        0x007600, // ?
        0x007700, // ?
        0x007800, // ?
        0x007900, // ?
        0x007A00, // ?
        0x007B00, // PLL1_NUM_STAT_39:32
        0x007C00, // PLL1_NUM_STAT_31:24
        0x007D00, // PLL1_NUM_STAT_23:16
        0x007E00, // PLL1_NUM_STAT_15:8
        0x007F00, // PLL1_NUM_STAT
        0x008000, // ?
        0x008100, // PLL1 Loop Filter R2
        0x008200, // ?
        0x008300, // PLL1 Loop Filter R3
        0x008400, // PLL1 Loop Filter R4
        0x008500, // ?
        0x008600, // Bit 8 of PLL2_NDIV
        0x00872C, // Bits 7:0 of PLL2 N Divider
        0x00887C, // Bits 23:16 of PLL2_NUM
        0x0089E0, // Bits 15:8 of PLL2_NUM
        0x008ABF, // PLL2 Fractional Divider Numerator
        0x008B00, // SDM Dither Mode + APLL2 SDM Order
        0x008C00, // PLL2 Loop Filter R2
        0x008D00, // ?
        0x008E00, // PLL2 Loop Filter R3
        0x008F00, // PLL2 Loop Filter R4
        0x009000, // PLL2 Loop Filter C4/C3
        0x009100, // XO Input Wait Timer
        0x009200, // ?
        0x009300, // ?
        0x009500, // ?
        0x009600, // ?
        0x009700, // ?
        0x009800, // ?
        0x009900, // ?
        0x009A00, // ?
        0x009B00, // ?
        0x009C00, // NVM Program Count
     //   0x009D00, // NVM misc
        0x009E00, // ?
        0x009F00, // Bits 12:8 of MEMADR
        0x00A000, // Memory Address
        0x00A100, // EEPROM Read Data
        0x00A200, // RAM Read/Write Data
     //   0x00A400, // NVM Program Unlock
        0x00A500, // ?
        0x00A700, // ?
        0x00B200, // ?
        0x00B400, // Bits 37:32 of DPLL_TUNING_FREE_RUN
        0x00B500, // Bits 31:24 of DPLL_TUNING_FREE_RUN
        0x00B600, // Bits 23:16 of DPLL_TUNING_FREE_RUN
        0x00B700, // Bits 15:8 of DPLL_TUNING_FREE_RUN
        0x00B800, // DPLL Free-run tuning word
        0x00B9F1, // DPLL_REF_HIST_EN++
        0x00BA01, // DPLL REF Tuning History Timer
        0x00BB00, // Bits 30:24 of DPLL_REF_HISTDLY
        0x00BC00, // Bits 23:16 of DPLL_REF_HISTDLY
        0x00BD00, // Bits 15:8 of DPLL_REF_HISTDLY
        0x00BE00, // DPLL REF Tuning History delay
        0x00BF00, // ?
        0x00C000, // PRI/SECREF detectors settings


        0x00C119,
        0x00C200,

        0x00C300, // PRIREF Missing Clock Detection
        0x00C400, // PRIREF Missing Clock Detection
        0x00C51D, // PRIREF Missing Clock Detection
        0x00C600, // SECREF Missing Clock Detection
        0x00C700, // SECREF Missing Clock Detection
        0x00C800, // SECREF Missing Clock Detection
        0x00C900, // PRI/SECREF Window Detection
        0x00CA00, // PRIREF Early Clock Detection
        0x00CB00, // PRIREF Early Clock Detection
        0x00CC15, // PRIREF Early Clock Detection
        0x00CD00, // SECREF Early Clock Detection
        0x00CE00, // SECREF Early Clock Detection
        0x00CF00, // SECREF Early Clock Detection
        0x00D000, // PRIREF Frequency Detection
        0x00D100, // PRIREF Frequency Detection
        0x00D200, // PRIREF Frequency Detection
        0x00D300, // PRIREF Frequency Detection
        0x00D400, // SECREF Frequency Detection
        0x00D500, // SECREF Frequency Detection
        0x00D600, // SECREF Frequency Detection
        0x00D700, // SECREF Frequency Detection
        0x00D800, // ?
        0x00D900, // PRIREF Frequency Detection
        0x00DA00, // PRIREF Frequency Detection
        0x00DB00, // PRIREF Frequency Detection
        0x00DC00, // PRIREF Frequency Detection
        0x00DD00, // PRIREF Frequency Detection
        0x00DE00, // PRIREF Frequency Detection
        0x00DF00, // PRIREF Frequency Detection
        0x00E000, // PRIREF Frequency Detection
        0x00E100, // SECREF Frequency Detection
        0x00E200, // SECREF Frequency Detection
        0x00E300, // SECREF Frequency Detection
        0x00E400, // SECREF Frequency Detection
        0x00E500, // SECREF Frequency Detection
        0x00E600, // SECREF Frequency Detection
        0x00E700, // SECREF Frequency Detection
        0x00E800, // SECREF Frequency Detection

        0x00E919,
        0x00EA00,

        0x00EB02, // PRIREF_PH_VALID_DET1
        0x00ECFA, // PRIREF_PH_VALID_DET2
        0x00EDF0, // PRIREF_PH_VALID_DET3
        0x00EE80, // PRIREF_PH_VALID_DET4
        0x00EF00, // SECREF_PH_VALID_DET1
        0x00F000, // SECREF_PH_VALID_DET2
        0x00F100, // SECREF_PH_VALID_DET3
        0x00F200, // SECREF_PH_VALID_DET4

        0x00F33F,
        0x00F400,
        0x00F921,

        0x00FA00, // ?

        0x00FB03,
        0x00FC29,

        0x00FD00, // DPLL Switchover Timer
        0x00FE00, // DPLL Switchover Timer
        0x00FF00, // DPLL Switchover Timer

        0x010000,
        0x010101,
        0x010200,
        0x010300,

        0x010402, // DPLL_REF_AVOID_SLIP
        0x010580, // DPLL Reference Control
        0x010601, // DPLL Reference Control
        0x01072A, // DPLL Reference Control
        0x010805, // DPLL Reference Control
        0x0109F2, // DPLL Reference Control
        0x010A00, // DPLL Reference Control
        0x010BA0, // DPLL Reference Control + DPLL Loop Filter
        0x010C00, // DPLL Reference Control
        0x010D00, // DPLL Reference Control
        0x010E02, // DPLL Loop Filter
        0x010FA6, // DPLL Loop Filter
        0x011000, // DPLL Loop Filter
        0x011100, // DPLL Loop Filter
        0x011200, // DPLL Loop Filter
        0x011316, // DPLL Loop Filter
        0x011416, // DPLL Loop Filter
        0x011516, // DPLL Loop Filter
        0x011600, // DPLL Loop Filter
        0x011700, // DPLL Loop Filter
        0x011800, // DPLL Loop Filter
        0x011900, // DPLL Loop Filter
        0x011A00, // DPLL Loop Filter
        0x011B00, // DPLL Loop Filter
        0x011C1E, // DPLL Loop Filter
        0x011D1E, // DPLL Loop Filter
        0x011E00, // DPLL Loop Filter
        0x011F00, // DPLL Loop Filter
        0x012000, // DPLL Loop Filter
        0x012100, // DPLL Loop Filter
        0x012203, // DPLL Phase Lock Detection
        0x012322, // DPLL Phase Lock Detection
        0x012400, // Phase Cancellation for Hitless Switching
        0x012500, // Phase Cancellation for Hitless Switching
        0x012600, // Phase Cancellation for Hitless Switching
        0x012700, // Phase Cancellation for Hitless Switching
        0x01280A, // DPLL Loop Filter
        0x01290A, // DPLL Loop Filter
        0x012A0A, // DPLL Loop Filter
        0x012B00, // ?
        0x012C00, // DPLL Phase Lock Detection
        0x012D1C, // Phase lock declaration threshold
        0x012E1E, // Phase un-lock declaration threshold
        0x012F01, // ?

        0x01300F,
        0x013104,
        0x013261,
        0x0133F8,
        0x013443,
        0x0135C3,
        0x0136C3,
        0x0137C3,
        0x0138C3,
        0x0139C3,
        0x013AFF,
        0x013BFF,
        0x013CFF,
        0x013DFF,
        0x013EFF,

        0x013F03, // DPLL Reference Control
        0x014000, // DPLL DCO Lock Detection
        0x01410A, // DPLL DCO Lock Detection
        0x014200, // DPLL DCO Lock Detection
        0x014300, // DPLL DCO Lock Detection
        0x014400, // DPLL DCO Lock Detection
        0x014501, // DPLL DCO Lock Detection
        0x014606, // DPLL DCO Lock Detection
        0x014735, // DPLL DCO Lock Detection
        0x014875, // DPLL DCO Lock Detection
        0x01490B, // DPLL DCO Lock Detection
        0x014A00, // DPLL DCO Unlock Detection
        0x014B64, // DPLL DCO Unlock Detection
        0x014C00, // DPLL DCO Unlock Detection

        0x014D00,
        0x014E61, /* PLL2 den */
        0x014FA8,

        0x015006, // DPLL DCO Unlock Detection
        0x015135, // DPLL DCO Unlock Detection
        0x015275, // DPLL DCO Unlock Detection

        0x01530B, /* PLL1 num*/

        0x015400, // DPLL_REF_SYNC_PH_OFFSET
        0x015500, // DPLL_REF_SYNC_PH_OFFSET
        0x015600, // DPLL_REF_SYNC_PH_OFFSET
        0x015700, // DPLL_REF_SYNC_PH_OFFSET
        0x015800, // DPLL_REF_SYNC_PH_OFFSET
        0x015900, // DPLL REF Zero Delay Mode Phase Offset
        0x015A00, // DPLL Freq Incr/Decr enable via pin or reg control
        0x015B00, // DPLL_FDEV
        0x015C00, // DPLL_FDEV
        0x015D00, // DPLL_FDEV
        0x015E00, // DPLL_FDEV
        0x015F00, // DPLL Freq Incr/Decr Numerator Step Word
        0x016000, // DPLL Freq Incr/Decr register control
        //0x016500, R
        //0x016F00, R
        //0x019B00, R
};



static const uint32_t lmk05318_rom_test[] =
{
    //0x000010,
    //0x00010B,
    //0x000235,
    //0x000332,
    //0x000404,
    //0x00050E,
    //0x000617,
    //0x00078E,
    //0x000802,
    //0x000AC8,
    //0x000B00,
    0x000C1B,
    0x000D08,
    0x000E00,
    0x000F00,
    0x001000,
    0x00111D,
    0x0012FF,
    0x001308,
    0x001420,
    0x001501,
    0x001600,
    0x001755,
    0x001855,
    0x001900,
    0x001A00,
    0x001B00,
    0x001C01,
    0x001D13,
    0x001E40,
    0x002044,
    0x002300,
    0x002403,
    0x002500,
    0x002600,
    0x002703,
    0x002802,
    0x002900,
    0x002A11,
    0x002BC2,
    0x002C00,
    0x002D03,
    0x002E11,
    0x002F07,
    0x003050,
    0x00314A,
    0x003280,
    0x003310,
    0x003410,
    0x003513,
    0x003610,
    0x003710,
    0x003809,
    0x003900,
    0x003A0F,
    0x003B80,
    0x003CE1,
    0x003DBE,
    0x003EE1,
    0x003F3E,
    0x004095,
    0x004102,
    0x0042F8,
    0x0043FF,
    0x004408,
    0x004500,
    0x004607,
    0x00473F,
    0x004833,
    0x004900,
    0x004A00,
    0x004B00,
    0x004C00,
    0x004D0F,
    0x004E00,
    0x004F11,
    0x005080,
    0x00510A,
    0x005200,
    0x00530E,
    0x0054A6,
    0x005500,
    0x005600,
    0x00571E,
    0x005884,
    0x005980,
    0x005A00,
    0x005B14,
    0x005C00,
    0x005D0E,
    0x005EA6,
    0x005F00,
    0x006000,
    0x00611E,
    0x006284,
    0x006380,
    0x006428,
    0x006501,
    0x006611,
    0x00670F,
    0x00681F,
    0x006905,
    0x006A00,
    0x006B64,
    0x006C00,
    0x006D32,
    0x006E00,
    0x006F00,
    0x007000,
    0x007127,
    0x007210,
    0x007303,
    0x007400,
    0x007500,
    0x007600,
    0x007700,
    0x007800,
    0x007900,
    0x007A00,
    0x007B28,
    0x007C00,
    0x007D11,
    0x007E79,
    0x007F7A,
    0x008000,
    0x008101,
    0x008200,
    0x008301,
    0x008401,
    0x008577,
    0x008600,
    0x00872A,
    0x00884E,
    0x0089A4,
    0x008AAC,
    0x008B03,
    0x008C02,
    0x008D00,
    0x008E01,
    0x008F01,
    0x009077,
    0x009101,
    0x009289,
    0x009320,
    0x00950D,
    0x009600,
    0x009701,
    0x00980D,
    0x009929,
    0x009A24,
    0x009B32,
    0x009C01,
    //0x009D00,
    0x009E00,
    0x009F00,
    0x00A0FC,
    0x00A132,
    0x00A200,
    //0x00A400,
    0x00A500,
    0x00A701,
    0x00B200,
    0x00B400,
    0x00B500,
    0x00B600,
    0x00B700,
    0x00B800,
    0x00B905,
    0x00BA08,
    0x00BB00,
    0x00BC00,
    0x00BD00,
    0x00BE2C,
    0x00BF00,
    0x00C050,
    0x00C100,
    0x00C200,
    0x00C300,
    0x00C400,
    0x00C51D,
    0x00C600,
    0x00C700,
    0x00C81D,
    0x00C900,
    0x00CA00,
    0x00CB00,
    0x00CC15,
    0x00CD00,
    0x00CE00,
    0x00CF15,
    0x00D000,
    0x00D114,
    0x00D200,
    0x00D316,
    0x00D400,
    0x00D514,
    0x00D600,
    0x00D716,
    0x00D80F,
    0x00D900,
    0x00DA00,
    0x00DB19,
    0x00DC6E,
    0x00DD00,
    0x00DE03,
    0x00DF0D,
    0x00E047,
    0x00E100,
    0x00E200,
    0x00E319,
    0x00E46E,
    0x00E500,
    0x00E603,
    0x00E70D,
    0x00E847,
    0x00E90A,
    0x00EA0A,
    0x00EB00,
    0x00ECC3,
    0x00ED50,
    0x00EE00,
    0x00EF00,
    0x00F0C3,
    0x00F150,
    0x00F200,
    0x00F300,
    0x00F400,
    0x00F912,
    0x00FA00,
    0x00FB13,
    0x00FC2C,
    0x00FD00,
    0x00FE00,
    0x00FF00,
    0x010000,
    0x010101,
    0x010200,
    0x010301,
    0x010402,
    0x010580,
    0x010600,
    0x010700,
    0x010800,
    0x010900,
    0x010AC8,
    0x010BA0,
    0x010C0C,
    0x010D0A,
    0x010E02,
    0x010F14,
    0x011000,
    0x011100,
    0x011200,
    0x01130E,
    0x01140C,
    0x01150E,
    0x011609,
    0x011708,
    0x011809,
    0x011907,
    0x011A0D,
    0x011B07,
    0x011C1E,
    0x011D1E,
    0x011E02,
    0x011F30,
    0x012000,
    0x0121EE,
    0x012202,
    0x0123CA,
    0x012409,
    0x012501,
    0x012600,
    0x01272C,
    0x012808,
    0x01290C,
    0x012A08,
    0x012B01,
    0x012C00,
    0x012D1C,
    0x012E20,
    0x012F01,
    0x013001,
    0x013100,
    0x013200,
    0x013300,
    0x013410,
    0x0135AA,
    0x0136AA,
    0x0137AA,
    0x0138AA,
    0x0139AA,
    0x013AFF,
    0x013BFF,
    0x013CFF,
    0x013DFF,
    0x013EFF,
    0x013F03,
    0x014000,
    0x01410A,
    0x014200,
    0x014324,
    0x01449F,
    0x014500,
    0x014600,
    0x014798,
    0x014896,
    0x014980,
    0x014A00,
    0x014B64,
    0x014C00,
    0x014D00,
    0x014E61,
    0x014FA8,
    0x015000,
    0x015100,
    0x015200,
    0x015300,
    0x015400,
    0x015500,
    0x015600,
    0x015700,
    0x015800,
    0x015900,
    0x015A01,
    0x015B00,
    0x015C00,
    0x015D00,
    0x015E00,
    0x015F00,
    0x016000,
    0x016528,
    0x016F00,
    0x019B0C,

#if 0
    //0x000010,
    //0x00010B,
    //0x000235,
    //0x000332,
    //0x000404,
    //0x00050E,
    //0x000617,
    //0x00078E,
    //0x000802,
    //0x000AC8,
    //0x000B00,
    0x000C1B,
    0x000D08,
    0x000E00,
    0x000F00,
    0x001000,
    0x00111D,
    0x0012FF,
    0x001308,
    0x001420,
    0x001501,
    0x001600,
    0x001755,
    0x001855,
    0x001900,
    0x001A00,
    0x001B00,
    0x001C01,
    0x001D13,
    0x001E40,
    0x002044,
    0x002300,
    0x002403,
    0x002500,
    0x002600,
    0x002703,
    0x002803,
    0x002900,
    0x002A11,
    0x002BC2,
    0x002C00,
    0x002D03,
    0x002E11,
    0x002F07,
    0x003050,
    0x00314A,
    0x003200,
    0x003310,
    0x003410,
    0x003513,
    0x003610,
    0x003710,
    0x003809,
    0x003900,
    0x003A0F,
    0x003B00,
    0x003C0F,
    0x003D3E,
    0x003EF9,
    0x003F3E,
    0x004095,
    0x004102,
    0x0042F8,
    0x0043FF,
    0x004408,
    0x004500,
    0x004600,
    0x004700,
    0x004833,
    0x004900,
    0x004A00,
    0x004B00,
    0x004C00,
    0x004D0F,
    0x004E00,
    0x004F11,
    0x005080,
    0x00510A,
    0x005200,
    0x00530E,
    0x0054A6,
    0x005500,
    0x005600,
    0x00571E,
    0x005884,
    0x005980,
    0x005A00,
    0x005B14,
    0x005C00,
    0x005D0E,
    0x005EA6,
    0x005F00,
    0x006000,
    0x00611E,
    0x006284,
    0x006380,
    0x006429,
    0x006501,
    0x006622,
    0x00670F,
    0x00681F,
    0x006905,
    0x006A00,
    0x006B64,
    0x006C00,
    0x006D32,
    0x006E00,
    0x006F00,
    0x007000,
    0x007127,
    0x007210,
    0x007303,
    0x007400,
    0x007500,
    0x007600,
    0x007700,
    0x007800,
    0x007900,
    0x007A00,
    0x007B28,
    0x007C00,
    0x007D11,
    0x007E79,
    0x007F7A,
    0x008000,
    0x008101,
    0x008200,
    0x008301,
    0x008401,
    0x008577,
    0x008600,
    0x00872B,
    0x008800,
    0x008928,
    0x008AE5,
    0x008B03,
    0x008C02,
    0x008D00,
    0x008E01,
    0x008F01,
    0x009077,
    0x009101,
    0x009289,
    0x009320,
    0x00950D,
    0x009600,
    0x009701,
    0x00980D,
    0x009929,
    0x009A24,
    0x009B32,
    0x009C01,
    //0x009D00,
    0x009E00,
    0x009F00,
    0x00A0FC,
    0x00A132,
    0x00A200,
    //0x00A400,
    0x00A500,
    0x00A701,
    0x00B200,
    0x00B400,
    0x00B500,
    0x00B600,
    0x00B700,
    0x00B800,
    0x00B905,
    0x00BA08,
    0x00BB00,
    0x00BC00,
    0x00BD00,
    0x00BE2C,
    0x00BF00,
    0x00C050,
    0x00C100,
    0x00C200,
    0x00C300,
    0x00C400,
    0x00C51D,
    0x00C600,
    0x00C700,
    0x00C81D,
    0x00C900,
    0x00CA00,
    0x00CB00,
    0x00CC15,
    0x00CD00,
    0x00CE00,
    0x00CF15,
    0x00D000,
    0x00D114,
    0x00D200,
    0x00D316,
    0x00D400,
    0x00D514,
    0x00D600,
    0x00D716,
    0x00D80F,
    0x00D900,
    0x00DA00,
    0x00DB19,
    0x00DC6E,
    0x00DD00,
    0x00DE03,
    0x00DF0D,
    0x00E047,
    0x00E100,
    0x00E200,
    0x00E319,
    0x00E46E,
    0x00E500,
    0x00E603,
    0x00E70D,
    0x00E847,
    0x00E90A,
    0x00EA0A,
    0x00EB00,
    0x00ECC3,
    0x00ED50,
    0x00EE00,
    0x00EF00,
    0x00F0C3,
    0x00F150,
    0x00F200,
    0x00F300,
    0x00F400,
    0x00F912,
    0x00FA00,
    0x00FB13,
    0x00FC2C,
    0x00FD00,
    0x00FE00,
    0x00FF00,
    0x010000,
    0x010101,
    0x010200,
    0x010301,
    0x010402,
    0x010580,
    0x010600,
    0x010700,
    0x010800,
    0x010900,
    0x010AC8,
    0x010BA0,
    0x010C0C,
    0x010D0A,
    0x010E02,
    0x010F14,
    0x011000,
    0x011100,
    0x011200,
    0x01130E,
    0x01140C,
    0x01150E,
    0x011609,
    0x011708,
    0x011809,
    0x011907,
    0x011A0D,
    0x011B07,
    0x011C1E,
    0x011D1E,
    0x011E02,
    0x011F30,
    0x012000,
    0x0121EE,
    0x012202,
    0x0123CA,
    0x012409,
    0x012501,
    0x012600,
    0x01272C,
    0x012808,
    0x01290C,
    0x012A08,
    0x012B01,
    0x012C00,
    0x012D1C,
    0x012E20,
    0x012F01,
    0x013001,
    0x013100,
    0x013200,
    0x013300,
    0x013410,
    0x0135AA,
    0x0136AA,
    0x0137AA,
    0x0138AA,
    0x0139AA,
    0x013AFF,
    0x013BFF,
    0x013CFF,
    0x013DFF,
    0x013EFF,
    0x013F03,
    0x014000,
    0x01410A,
    0x014200,
    0x014324,
    0x01449F,
    0x014500,
    0x014600,
    0x014798,
    0x014896,
    0x014980,
    0x014A00,
    0x014B64,
    0x014C00,
    0x014D00,
    0x014E3D,
    0x014F09,
    0x015000,
    0x015100,
    0x015200,
    0x015300,
    0x015400,
    0x015500,
    0x015600,
    0x015700,
    0x015800,
    0x015900,
    0x015A02,
    0x015B00,
    0x015C00,
    0x015D00,
    0x015E00,
    0x015F00,
    0x016000,
    0x016528,
    0x016F00,
    0x019B0C,
#endif
};


static const uint32_t lmk05318_rom[] = {
//    0x000010,
//    0x00010B,
//    0x000235,
//    0x000332,
//    0x000404,
//    0x00050E,
//    0x000617,
//    0x00078E,
//    0x000802,
//    0x000AC8,
//    0x000B00,
    0x000C1B,
    0x000D08,
    0x000E00,
    0x000F00,
    0x001000,
    0x00111D,
    0x0012FF,
    0x001308,
    0x001420,
    0x001500,
    0x001600,
    0x001755,
    0x001855,
    0x001900,
    0x001A00,
    0x001B00,
    0x001C01,
    0x001D13,
    0x001E40,
    0x002044,
    0x002300,
    0x002403,
    0x002500,
    0x002600,
    0x002702,
    0x00280C,
    0x002900,
    0x002A01,
    0x002BE2,
    0x002C00,
    0x002D02,
    0x002E11,
    0x002F07,
    0x003050,
    0x00314A,
    0x003200,
    0x003310,
    0x003410,
    0x003504,
    0x003610,
    0x003710,
    0x003804,
    0x00393E,
    0x003A31,
    0x003B3E,
    0x003C31,
    0x003D3E,
    0x003E31,
    0x003F3E,
    0x004000,
    0x004100,
    0x004200,
    0x004331,
    0x004408,
    0x004500,
    0x004600,
    0x004700,
    0x004833,
    0x004900,
    0x004A00,
    0x004B00,
    0x004C00,
    0x004D0F,
    0x004E00,
    0x004F11,
    0x005080,
    0x00510A,
    0x005200,
    0x005307,
    0x00549E,
    0x005500,
    0x005600,
    0x00571E,
    0x005884,
    0x005980,
    0x005A00,
    0x005B14,
    0x005C00,
    0x005D07,
    0x005E9E,
    0x005F00,
    0x006000,
    0x00611E,
    0x006284,
    0x006380,
    0x006429,
    0x006501,
    0x006622,
    0x00670F,
    0x00681F,
    0x006905,
    0x006A00,
    0x006B64,
    0x006C00,
    0x006D60,
    0x006E27,
    0x006F62,
    0x007076,
    0x007127,
    0x007262,
    0x007303,
    0x007401,
    0x007500,
    0x007600,
    0x007700,
    0x007800,
    0x007900,
    0x007A00,
    0x007B28,
    0x007C00,
    0x007D11,
    0x007E79,
    0x007F7A,
    0x008000,
    0x008101,
    0x008200,
    0x008301,
    0x008401,
    0x008577,
    0x008600,
    0x008728,
    0x008800,
    0x00891D,
    0x008A18,
    0x008B03,
    0x008C02,
    0x008D00,
    0x008E01,
    0x008F01,
    0x009077,
    0x009101,
    0x009289,
    0x009320,
    0x00950D,
    0x009600,
    0x009701,
    0x00980D,
    0x009929,
    0x009A24,
    0x009B32,
    0x009C01,
   // 0x009D00,
    0x009E00,
    0x009F00,
    0x00A0FC,
    0x00A132,
    0x00A200,
  // 0x00A400,
    0x00A500,
    0x00A701,
    0x00B200,
    0x00B400,
    0x00B500,
    0x00B600,
    0x00B700,
    0x00B800,
    0x00B9F5,
    0x00BA01,
    0x00BB00,
    0x00BC00,
    0x00BD00,
    0x00BE00,
    0x00BF00,
    0x00C050,
    0x00C110,
    0x00C200,
    0x00C300,
    0x00C400,
    0x00C51D,
    0x00C600,
    0x00C700,
    0x00C81D,
    0x00C900,
    0x00CA00,
    0x00CB00,
    0x00CC15,
    0x00CD00,
    0x00CE00,
    0x00CF15,
    0x00D000,
    0x00D114,
    0x00D200,
    0x00D316,
    0x00D400,
    0x00D514,
    0x00D600,
    0x00D716,
    0x00D80F,
    0x00D900,
    0x00DA00,
    0x00DB19,
    0x00DC6E,
    0x00DD00,
    0x00DE03,
    0x00DF0D,
    0x00E047,
    0x00E100,
    0x00E200,
    0x00E319,
    0x00E46E,
    0x00E500,
    0x00E603,
    0x00E70D,
    0x00E847,
    0x00E90A,
    0x00EA0A,
    0x00EB01,
    0x00EC8C,
    0x00EDBA,
    0x00EE80,
    0x00EF00,
    0x00F0C3,
    0x00F150,
    0x00F200,
    0x00F33F,
    0x00F400,
    0x00F921,
    0x00FA00,
    0x00FB03,
    0x00FC2D,
    0x00FD00,
    0x00FE00,
    0x00FF00,
    0x010000,
    0x010101,
    0x010200,
    0x010300,
    0x010402,
    0x010580,
    0x010601,
    0x01072A,
    0x010805,
    0x0109F2,
    0x010A00,
    0x010BA0,
    0x010C04,
    0x010D00,
    0x010E02,
    0x010F8C,
    0x011000,
    0x011100,
    0x011200,
    0x011316,
    0x011416,
    0x011516,
    0x011600,
    0x011700,
    0x011800,
    0x011900,
    0x011A00,
    0x011B00,
    0x011C1E,
    0x011D1E,
    0x011E00,
    0x011F00,
    0x012000,
    0x012100,
    0x012203,
    0x012322,
    0x012409,
    0x012501,
    0x012600,
    0x01272C,
    0x012809,
    0x012909,
    0x012A09,
    0x012B01,
    0x012C00,
    0x012D1B,
    0x012E1E,
    0x012F01,
    0x01300F,
    0x013104,
    0x013261,
    0x0133F8,
    0x013443,
    0x0135C3,
    0x0136C3,
    0x0137C3,
    0x0138C3,
    0x0139C3,
    0x013AFF,
    0x013BFF,
    0x013CFF,
    0x013DFF,
    0x013EFF,
    0x013F03,
    0x014000,
    0x01410A,
    0x014200,
    0x014300,
    0x014400,
    0x014501,
    0x014606,
    0x014735,
    0x014875,
    0x01490B,
    0x014A00,
    0x014B64,
    0x014C00,
    0x014D00,
    0x014E3D,
    0x014F09,
    0x015000,
    0x015198,
    0x015296,
    0x015300,
    0x015400,
    0x015500,
    0x015600,
    0x015700,
    0x015800,
    0x015900,
    0x015A02,
    0x015B00,
    0x015C00,
    0x015D00,
    0x015E00,
    0x015F00,
    0x016000,
    0x016528,
    0x016F00,
    0x019B0C,
};

static const uint32_t lmk05318_rom_49152_12288_384[] = {
//    0x000010,
//    0x00010B,
//    0x000235,
//    0x000332,
//    0x000404,
//    0x00050E,
//    0x000617,
//    0x00078E,
//    0x000802,
//    0x000AC8,
//    0x000B00,
    0x000C1B,
    0x000D08,
    0x000E00,
    0x000F00,
    0x001000,
    0x00111D,
    0x0012FF,
    0x001308,
    0x001420,
    0x001500,
    0x001600,
    0x001755,
    0x001855,
    0x001900,
    0x001A00,
    0x001B00,
    0x001C01,
    0x001D13,
    0x001E40,
    0x002044,
    0x002300,
    0x002403,
    0x002500,
    0x002600,
    0x002703,
    0x002807,
    0x002900,
    0x002A01,
    0x002BC2,
    0x002C01,
    0x002D02,
    0x002E11,
    0x002F07,
    0x003050,
    0x00314A,
    0x003200,
    0x003380,
    0x003410,
    0x003501,
    0x003690,
    0x003700,
    0x0038FF,
    0x003910,
    0x003A07,
    0x003B90,
    0x003C07,
    0x003D90,
    0x003EFF,
    0x003F90,
    0x004000,
    0x004100,
    0x004200,
    0x004307,
    0x004408,
    0x004500,
    0x004600,
    0x004700,
    0x004833,
    0x004900,
    0x004A00,
    0x004B00,
    0x004C00,
    0x004D0F,
    0x004E00,
    0x004F11,
    0x005080,
    0x00510A,
    0x005200,
    0x005307,
    0x00549E,
    0x005500,
    0x005600,
    0x00571E,
    0x005884,
    0x005980,
    0x005A00,
    0x005B14,
    0x005C00,
    0x005D07,
    0x005E9E,
    0x005F00,
    0x006000,
    0x00611E,
    0x006284,
    0x006380,
    0x006428,
    0x006501,
    0x006655,
    0x00670F,
    0x00681F,
    0x006905,
    0x006A00,
    0x006B64,
    0x006C00,
    0x006D60,
    0x006E07,
    0x006FD0,
    0x007000,
    0x007132,
    0x0072C8,
    0x007303,
    0x007400,
    0x007500,
    0x007600,
    0x007700,
    0x007800,
    0x007900,
    0x007A00,
    0x007B28,
    0x007C00,
    0x007D11,
    0x007E79,
    0x007F7A,
    0x008000,
    0x008101,
    0x008200,
    0x008301,
    0x008401,
    0x008577,
    0x008600,
    0x00872A,
    0x008800,
    0x00891C,
    0x008A86,
    0x008B03,
    0x008C02,
    0x008D00,
    0x008E01,
    0x008F01,
    0x009077,
    0x009101,
    0x009289,
    0x009320,
    0x00950D,
    0x009600,
    0x009701,
    0x00980D,
    0x009929,
    0x009A24,
    0x009B32,
    0x009C01,
//    0x009D00,
    0x009E00,
    0x009F00,
    0x00A0FC,
    0x00A132,
    0x00A200,
//    0x00A400,
    0x00A500,
    0x00A701,
    0x00B200,
    0x00B400,
    0x00B500,
    0x00B600,
    0x00B700,
    0x00B800,
    0x00B9F5,
    0x00BA01,
    0x00BB00,
    0x00BC00,
    0x00BD00,
    0x00BE00,
    0x00BF00,
    0x00C050,
    0x00C100,
    0x00C200,
    0x00C300,
    0x00C400,
    0x00C51D,
    0x00C600,
    0x00C700,
    0x00C81D,
    0x00C900,
    0x00CA00,
    0x00CB00,
    0x00CC15,
    0x00CD00,
    0x00CE00,
    0x00CF15,
    0x00D000,
    0x00D114,
    0x00D200,
    0x00D316,
    0x00D400,
    0x00D514,
    0x00D600,
    0x00D716,
    0x00D80F,
    0x00D900,
    0x00DA00,
    0x00DB19,
    0x00DC6E,
    0x00DD00,
    0x00DE03,
    0x00DF0D,
    0x00E047,
    0x00E100,
    0x00E200,
    0x00E319,
    0x00E46E,
    0x00E500,
    0x00E603,
    0x00E70D,
    0x00E847,
    0x00E90A,
    0x00EA0A,
    0x00EB01,
    0x00EC8C,
    0x00EDBA,
    0x00EE80,
    0x00EF00,
    0x00F0C3,
    0x00F150,
    0x00F200,
    0x00F300,
    0x00F400,
    0x00F921,
    0x00FA00,
    0x00FB03,
    0x00FC2C,
    0x00FD00,
    0x00FE00,
    0x00FF00,
    0x010000,
    0x010101,
    0x010200,
    0x010300,
    0x010402,
    0x010580,
    0x010601,
    0x01072A,
    0x010805,
    0x0109F2,
    0x010A00,
    0x010BA0,
    0x010C04,
    0x010D00,
    0x010E02,
    0x010F8C,
    0x011000,
    0x011100,
    0x011200,
    0x011316,
    0x011416,
    0x011516,
    0x011600,
    0x011700,
    0x011800,
    0x011900,
    0x011A00,
    0x011B00,
    0x011C1E,
    0x011D1E,
    0x011E00,
    0x011F00,
    0x012000,
    0x012100,
    0x012203,
    0x012322,
    0x012409,
    0x012501,
    0x012600,
    0x01272C,
    0x012809,
    0x012909,
    0x012A09,
    0x012B01,
    0x012C00,
    0x012D1B,
    0x012E1E,
    0x012F01,
    0x01300F,
    0x013104,
    0x013261,
    0x0133F8,
    0x013443,
    0x0135C3,
    0x0136C3,
    0x0137C3,
    0x0138C3,
    0x0139C3,
    0x013AFF,
    0x013BFF,
    0x013CFF,
    0x013DFF,
    0x013EFF,
    0x013F03,
    0x014000,
    0x01410A,
    0x014200,
    0x014300,
    0x014400,
    0x014501,
    0x014606,
    0x014735,
    0x014875,
    0x01490B,
    0x014A00,
    0x014B64,
    0x014C00,
    0x014D00,
    0x014E3D,
    0x014F09,
    0x015000,
    0x015198,
    0x015296,
    0x015300,
    0x015400,
    0x015500,
    0x015600,
    0x015700,
    0x015800,
    0x015900,
    0x015A02,
    0x015B00,
    0x015C00,
    0x015D00,
    0x015E00,
    0x015F00,
    0x016000,
    0x016528,
    0x016F00,
    0x019B0C,
};
