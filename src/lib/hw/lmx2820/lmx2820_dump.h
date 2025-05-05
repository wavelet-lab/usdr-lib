#ifndef LMX2820_DUMP_H
#define LMX2820_DUMP_H

#include <stdint.h>

static uint32_t lmx2820_rom_sysref[] =
{
    /*R79 */ 0x4F000E,
    /*R78 */ 0x4E0000,
    /*R69 */ 0x450001,
    /*R67 */ 0x431000,
    /*R66 */ 0x42003F,
    /*R65 */ 0x410008,
    /*R64 */ 0x400284,
    /*R45 */ 0x2D0000,
    /*R44 */ 0x2C0000,
    /*R43 */ 0x2B0000,
    /*R42 */ 0x2A0000,
    /*R39 */ 0x270002, /*****/
    /*R38 */ 0x260000,
    /*R36 */ 0x240020,
    /*R35 */ 0x233100,
    /*R32 */ 0x2016C1,
    /*R22 */ 0x16E2BF, /*****/
    /*R14 */ 0x0E3001,
    /*R13 */ 0x0D0038,
    /*R12 */ 0x0C0408,
    /*R11 */ 0x0B0602,
    /*R2  */ 0x0293E8,
    /*R1  */ 0x01D7A0,
    /*R0  */ 0x006630, /****/
};

/*
21:21:14.518863 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#0: R079 (0x4f) -> 0x000e [0x4f000e]
21:21:14.518874 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#1: R078 (0x4e) -> 0x0000 [0x4e0000]
21:21:14.518881 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#2: R069 (0x45) -> 0x0001 [0x450001]
21:21:14.518888 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#3: R067 (0x43) -> 0x1000 [0x431000]
21:21:14.518897 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#4: R066 (0x42) -> 0x003f [0x42003f]
21:21:14.518904 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#5: R065 (0x41) -> 0x0008 [0x410008]
21:21:14.518914 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#6: R064 (0x40) -> 0x0284 [0x400284]
21:21:14.518923 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#7: R045 (0x2d) -> 0x0000 [0x2d0000]
21:21:14.518928 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#8: R044 (0x2c) -> 0x0000 [0x2c0000]
21:21:14.518935 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#9: R043 (0x2b) -> 0x0000 [0x2b0000]
21:21:14.518941 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#10: R042 (0x2a) -> 0x0000 [0x2a0000]
21:21:14.518948 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#11: R039 (0x27) -> 0x0001 [0x270001]
21:21:14.518955 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#12: R038 (0x26) -> 0x0000 [0x260000]
21:21:14.518962 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#13: R036 (0x24) -> 0x0020 [0x240020]
21:21:14.518969 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#14: R035 (0x23) -> 0x3100 [0x233100]
21:21:14.518978 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#15: R032 (0x20) -> 0x16c1 [0x2016c1]
21:21:14.518983 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#16: R022 (0x16) -> 0x62bf [0x1662bf]
21:21:14.519004 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#17: R014 (0x0e) -> 0x3001 [0x0e3001]
21:21:14.519020 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#18: R013 (0x0d) -> 0x0038 [0x0d0038]
21:21:14.519031 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#19: R012 (0x0c) -> 0x0408 [0x0c0408]
21:21:14.519040 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#20: R011 (0x0b) -> 0x0602 [0x0b0602]
21:21:14.519052 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#21: R002 (0x02) -> 0x93e8 [0x0293e8]
21:21:14.519062 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#22: R001 (0x01) -> 0xd7a0 [0x01d7a0]
21:21:14.519072 DEBUG:   common_print_registers_a8d16:89 [COMN] WRITE#23: R000 (0x00) -> 0x6660 [0x006660]
*/


static uint32_t lmx2820_rom_test[] =
{
    /*R122*/	0x7A0000,
    /*R121*/	0x790000,
    /*R120*/	0x780000,
    /*R119*/	0x770000,
    /*R118*/	0x760000,
    /*R117*/	0x750000,
    /*R116*/	0x740000,
    /*R115*/	0x730000,
    /*R114*/	0x720000,
    /*R113*/	0x710000,
    /*R112*/	0x70FFFF,
    /*R111*/	0x6F0000,
    /*R110*/	0x6E001F,
    /*R109*/	0x6D0000,
    /*R108*/	0x6C0000,
    /*R107*/	0x6B0000,
    /*R106*/	0x6A0000,
    /*R105*/	0x69000A,
    /*R104*/	0x680014,
    /*R103*/	0x670014,
    /*R102*/	0x660028,
    /*R101*/	0x6503E8,
    /*R100*/	0x640533,
    /*R99*/     0x6319B9,
    /*R98*/     0x621C80,
    /*R97*/     0x610000,
    /*R96*/     0x6017F8,
    /*R95*/     0x5F0000,
    /*R94*/     0x5E0000,
    /*R93*/     0x5D1000,
    /*R92*/     0x5C0000,
    /*R91*/     0x5B0000,
    /*R90*/     0x5A0000,
    /*R89*/     0x590000,
    /*R88*/     0x5803FF,
    /*R87*/     0x57FF00,
    /*R86*/     0x560040,
    /*R85*/     0x550000,
    /*R84*/     0x540040,
    /*R83*/     0x530F00,
    /*R82*/     0x520000,
    /*R81*/     0x510000,
    /*R80*/     0x5001C0,
    /*R79*/     0x4F000E,
    /*R78*/     0x4E0000,
    /*R77*/     0x4D0608,
    /*R76*/     0x4C0000,
    /*R75*/     0x4B0000,
    /*R74*/     0x4A0000,
    /*R73*/     0x490000,
    /*R72*/     0x480000,
    /*R71*/     0x470000,
    /*R70*/     0x46000E,
    /*R69*/     0x450011,
    /*R68*/     0x440020,
    /*R67*/     0x431000,
    /*R66*/     0x42003F,
    /*R65*/     0x410000,
    /*R64*/     0x400080,
    /*R63*/     0x3FC350,
    /*R62*/     0x3E0000,
    /*R61*/     0x3D03E8,
    /*R60*/     0x3C01F4,
    /*R59*/     0x3B1388,
    /*R58*/     0x3A0000,
    /*R57*/     0x390001,
    /*R56*/     0x380001,
    /*R55*/     0x370002,
    /*R54*/     0x360000,
    /*R53*/     0x350000,
    /*R52*/     0x340000,
    /*R51*/     0x33203F,
    /*R50*/     0x320080,
    /*R49*/     0x310000,
    /*R48*/     0x304180,
    /*R47*/     0x2F0300,
    /*R46*/     0x2E0300,
    /*R45*/     0x2D51EC,
    /*R44*/     0x2C1EB8,
    /*R43*/     0x2B0003,
    /*R42*/     0x2A0000,
    /*R41*/     0x290000,
    /*R40*/     0x280000,
    /*R39*/     0x270019,
    /*R38*/     0x260000,
    /*R37*/     0x250500,
    /*R36*/     0x240025,
    /*R35*/     0x233100,
    /*R34*/     0x220010,
    /*R33*/     0x210000,
    /*R32*/     0x201AC1,
    /*R31*/     0x1F0401,
    /*R30*/     0x1EB18C,
    /*R29*/     0x1D318C,
    /*R28*/     0x1C0639,
    /*R27*/     0x1B8001,
    /*R26*/     0x1A0DB0,
    /*R25*/     0x190624,
    /*R24*/     0x180E34,
    /*R23*/     0x171102,
    /*R22*/     0x16E2BF,
    /*R21*/     0x151C64,
    /*R20*/     0x14272C,
    /*R19*/     0x132120,
    /*R18*/     0x120000,
    /*R17*/     0x1115C0,
    /*R16*/     0x10171C,
    /*R15*/     0x0F2001,
    /*R14*/     0x0E3002,
    /*R13*/     0x0D0038,
    /*R12*/     0x0C0408,
    /*R11*/     0x0B0612,
    /*R10*/     0x0A0000,
    /*R9*/      0x090005,
    /*R8*/      0x08C802,
    /*R7*/      0x070000,
    /*R6*/      0x060A43,
    /*R5*/      0x050032,
    /*R4*/      0x044204,
    /*R3*/      0x030041,
    /*R2*/      0x0291F4,
    /*R1*/      0x0157A2,
    /*R0*/      0x006670,
};

#endif // LMX2820_DUMP_H
