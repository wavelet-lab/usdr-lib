// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "ext_fe_100_5000.h"
#include "../ipblks/gpio.h"
#include "../ipblks/spiext.h"

#include <usdr_logging.h>
#include <string.h>

#include <def_xra1405.h>
#include <def_ext_fe_100_5000.h>

enum {
    GPIO_SCK = GPIO12,
    GPIO_MOSI = GPIO13,
    GPIO_MISO = GPIO14,

    GPIO_CS0 = GPIO11,

    GPIO_CS1 = GPIO4,
    //GPIO_CS3 = GPIO5,
//    GPIO_CS2 = GPIO6,  in stright order
//    GPIO_LE1 = GPIO7,
    GPIO_CS2 = GPIO7,
    GPIO_LE1 = GPIO6,

    //GPIO_LE2 = GPIO8,

    //GPIO_TP1 = GPIO0,
    //GPIO_TP2 = GPIO1,
    //GPIO_TP3 = GPIO9,
    //GPIO_TP4 = GPIO10,

};

enum {
    PIN_RF_DC_EN = 0,
    PIN_RF_EN = 1,
    PIN_SEL_GAIN_0 = 2,
    PIN_SEL_GAIN_1 = 3,
    PIN_SEL_BAND_0 = 4,
    PIN_SEL_BAND_1 = 5,
    PIN_PD_BAND4 = 6,
    PIN_PD_BAND3 = 7,
    PIN_PD_BAND2 = 8,
    PIN_EN_BAND1 = 9,
    PIN_SEL_IF = 10,
    PIN_LD2 = 14,
    PIN_LD1 = 15
};

// CSn:
// 0: 26Mhz   XRA1405IL24        (16bit)
// 1: 20Mhz   MAX2871       LO1  (32bit)
// 2: 20Mhz   MAX2871       LO2  (32bit)

// LEn:
// 1: 25Mhz   RFSA3713TR7  (Adrr == 0)   (16bit)

enum clk_div {
    //SPI_DIV = 7,
    SPI_DIV = 25,
};

enum spi_sen {
    CS_IDX_EXPANDER = 7,
    // CS_IDX_ATTN = 3,
    // CS_IDX_LO1 = 2,
    CS_IDX_ATTN = 2,
    CS_IDX_LO2 = 3,
    CS_IDX_LO1 = 0
};

enum spi_bytes {
    SPI_BYTES_XRA1405IL24 = 1,
    SPI_BYTES_MAX2871 = 3,
    SPI_BYTES_RFSA3713TR7 = 1,
};

// band selection
enum {
    SB_BAND2 = 0,
    SB_BAND4 = 1,
    SB_BAND3 = 2,
    SB_BAND1 = 3,
};


enum {
    DEV_EXPANDER = 0,
    DEV_LO1 = 1,
    DEV_LO2 = 2,
    DEV_ATTN = 3,
};


#define MAKE_SPI_CFG(b, cs, div) ((((b) & 3) << 14) | (((cs) & 7) << 8) | ((div) & 0xff))
static const uint32_t s_dev_cfg[4] = {
    [DEV_EXPANDER] = MAKE_SPI_CFG(SPI_BYTES_XRA1405IL24, CS_IDX_EXPANDER, SPI_DIV),
    [DEV_LO1]      = MAKE_SPI_CFG(SPI_BYTES_MAX2871, CS_IDX_LO1, SPI_DIV),
    [DEV_LO2]      = MAKE_SPI_CFG(SPI_BYTES_MAX2871, CS_IDX_LO2, SPI_DIV),
    [DEV_ATTN]     = MAKE_SPI_CFG(SPI_BYTES_RFSA3713TR7, CS_IDX_ATTN, SPI_DIV),
};

#define MAKE_SPI_EXT_LSOPADR(cfg, eaddr, baddr) (\
    ((cfg) << 16) | (((eaddr) & 0xff) << 8) | ((baddr) & 0xff))

static int s_spi_ext_op(lldev_t dev, subdev_t subdev,
                        unsigned ls_op, lsopaddr_t ls_op_addr,
                        size_t meminsz, void* pin, size_t memoutsz,
                        const void* pout)
{
    if (memoutsz > 4)
        return -EINVAL;

    USDR_LOG("SEXT", USDR_LOG_INFO, "Switching bus %d to %04x on %d port\n",
             SPIEXT_LSOP_GET_BUS(ls_op_addr),
             SPIEXT_LSOP_GET_CFG(ls_op_addr),
             SPIEXT_LSOP_GET_CSR(ls_op_addr));

    int res = lowlevel_reg_wr32(dev, subdev, SPIEXT_LSOP_GET_CSR(ls_op_addr), SPIEXT_LSOP_GET_CFG(ls_op_addr));
    if (res)
        return res;

    uint32_t spin = 0xcccccccc;
    uint32_t spout = spiext_make_data_reg(memoutsz, pout);
    res = lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                        ls_op, ls_op_addr & 0xff,
                                        sizeof(spin), &spin, sizeof(spout), &spout);
    spiext_parse_data_reg(spin, meminsz, pin);
    return res;
}


static int xra1405il24_reg_wr(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                              uint8_t reg, uint8_t out)
{
    uint8_t data[2] = { out,  reg & 0x7f };
    return s_spi_ext_op(dev, subdev,
                        USDR_LSOP_SPI, ls_op_addr,
                        0, NULL, SIZEOF_ARRAY(data), data);
}

static int xra1405il24_reg_rd(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                              uint8_t reg, uint8_t *out)
{
    uint8_t data[2] = { 0xff, 0x80 | reg };
    uint8_t odata[2] = { 0xff, 0xff };
    int res = s_spi_ext_op(dev, subdev,
                           USDR_LSOP_SPI, ls_op_addr,
                           SIZEOF_ARRAY(odata), odata, SIZEOF_ARRAY(data), data);
    *out = odata[0];
    return res;
}

// ADDR[0] only
static int rfsa3713tr7_reg_wr(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                              uint8_t out)
{
    uint8_t data[2] = { 0, 0 };

    for (unsigned j = 0; j < 8; j++) {
        data[1] |= ((out >> j) & 1) << (7 - j);
    }

    return s_spi_ext_op(dev, subdev,
                        USDR_LSOP_SPI, ls_op_addr,
                        0, NULL, SIZEOF_ARRAY(data), data);
}

#define IF1_FREQ1   1224000
#define IF1_FREQ2   2250000
#define IF2_FREQ    312500

#define F_PFD_KHZ   20000  // For MAX synt Fref = 40MHz TCXO devided by 2
#define F_STEP_KHZ  1      // freq_khz step is 1kHz

static int update_band_sel(ext_fe_100_5000_t* ob);

static int max2871_wr_dat(ext_fe_100_5000_t* ob, unsigned addr, uint32_t out)
{
    return s_spi_ext_op(ob->dev, ob->subdev,
                        USDR_LSOP_SPI, addr,
                        0, NULL, sizeof(out), &out);
}

struct max2871_corr {
    uint32_t reg4;
    uint32_t reg1;
    uint32_t reg0;

    unsigned rfc; // RF attenuator additional value for flatness correction
    unsigned rf_div;
};
typedef struct max2871_corr max2871_corr_t;

static
int max2871_band_sel(unsigned freq_khz, max2871_corr_t* corr)
{
    uint32_t f_vco, modls, intgr, fract; // synth servicing digits
    uint32_t reg4;
    unsigned rfc; // RF attenuator additional value for flatness correction
    unsigned rf_div;

    // select band and appropriate values
    if (freq_khz >= 4800000)      { rfc = 0; rf_div = 1;  reg4 = (0x60850424 | (0<<20) | (3<<3)); } // Att-0dB,    Reg4 APwr=3,DIVA=1,MTLD=1
    else if (freq_khz >= 4500000) { rfc = 5; rf_div = 1;  reg4 = (0x60850424 | (0<<20) | (1<<3)); } // Att-1.75dB, Reg4 APwr=1,DIVA=1,MTLD=1
    else if (freq_khz >= 4000000) { rfc = 7; rf_div = 1;  reg4 = (0x60850424 | (0<<20) | (1<<3)); } // Att-1.75dB, Reg4 APwr=1,DIVA=1,MTLD=1
    else if (freq_khz >= 3500000) { rfc = 3; rf_div = 1;  reg4 = (0x60850424 | (0<<20) | (1<<3)); } // Att-1.25dB, Reg4 APwr=1,DIVA=1,MTLD=1
    else if (freq_khz >= 3000000) { rfc = 0; rf_div = 1;  reg4 = (0x60850424 | (0<<20) | (1<<3)); } // Att-0.75dB, Reg4 APwr=1,DIVA=1,MTLD=1
    else if (freq_khz >= 2500000) { rfc = 4; rf_div = 2;  reg4 = (0x60850424 | (1<<20) | (2<<3)); } // Att-1dB,    Reg4 APwr=2,DIVA=2,MTLD=1
    else if (freq_khz >= 2000000) { rfc = 5; rf_div = 2;  reg4 = (0x60850424 | (1<<20) | (2<<3)); } // Att-1.25dB, Reg4 APwr=2,DIVA=2,MTLD=1
    else if (freq_khz >= 1500000) { rfc = 3; rf_div = 2;  reg4 = (0x60850424 | (1<<20) | (2<<3)); } // Att-0.75dB, Reg4 APwr=2,DIVA=2,MTLD=1
    else if (freq_khz >= 1200000) { rfc = 0; rf_div = 4;  reg4 = (0x60850424 | (2<<20) | (2<<3)); } // Att-0dB,    Reg4 APwr=3,DIVA=4,MTLD=1
    else if (freq_khz >= 750000)  { rfc = 0; rf_div = 4;  reg4 = (0x60850424 | (2<<20) | (3<<3)); } // Att-0dB,    Reg4 APwr=3,DIVA=4,MTLD=1
    else if (freq_khz >= 375000)  { rfc = 0; rf_div = 8;  reg4 = (0x60850424 | (3<<20) | (3<<3)); } // Att-0dB,    Reg4 APwr=3,DIVA=8,MTLD=1
    else if (freq_khz >= 187500)  { rfc = 0; rf_div = 16; reg4 = (0x60850424 | (4<<20) | (3<<3)); } // Att-0dB,    Reg4 APwr=3,DIVA=16,MTLD=1
    else if (freq_khz >= 93750)   { rfc = 0; rf_div = 32; reg4 = (0x60850424 | (5<<20) | (3<<3)); } // Att-0dB,    Reg4 APwr=3,DIVA=32,MTLD=1
    else return -EINVAL;

    modls = F_PFD_KHZ / (rf_div * F_STEP_KHZ);                    // 20000/(2*10)=1000
    f_vco = freq_khz * F_STEP_KHZ * rf_div;                     // 234567*10*2=4691340 [Hz]
    intgr = f_vco / F_PFD_KHZ;                                    // 4691340/20000=234 {234,567}
    fract = ((f_vco - intgr * F_PFD_KHZ) * modls) / F_PFD_KHZ;    // ((4691340-234*20000)*1000)/20000=567

    corr->reg0 = (intgr << 15) | (fract << 3);
    corr->reg1 = 0x20010001 | modls << 3;

    corr->reg4 = reg4;
    corr->rfc = rfc;
    corr->rf_div = rf_div;
    return 0;
}

// LOAD SYNTESIZER CODES FOR MAX2871 in fractional N mode and select output depending of required output power.
static
int max2871_init(ext_fe_100_5000_t* ob, unsigned addr, unsigned long int freq_khz)
{
    max2871_corr_t c;
    int res = 0;

    // check high side band and power off mode
    if ((freq_khz == 0) || (freq_khz > 6000000)) {
        return 0;
    }

    // check low side band, freq below means power down mode
    if (freq_khz == 1) {
        // Power on TG but with fully powered down synth chip to get TCXO only
        res = (res) ? res : max2871_wr_dat(ob, addr, 0x03C00005); // Reg5 power down PLL, ADC, LD=1 (allways)
        res = (res) ? res : max2871_wr_dat(ob, addr, 0x1C9A0CDC); // Reg4 power down LDO,DIV, REF, VCO, OutB, OutA
        res = (res) ? res : max2871_wr_dat(ob, addr, 0x02001F23); // Reg3 power down VAS
        res = (res) ? res : max2871_wr_dat(ob, addr, 0x04011E72); // Reg2 CPout=tri-state, shut down device, MUX=1 to Disable RF driver chip (PCB v3.2 only)
        res = (res) ? res : max2871_wr_dat(ob, addr, 0x200103E9); // Reg1 Does not relate to power down mode, write to complete sequence
        res = (res) ? res : max2871_wr_dat(ob, addr, 0x01238000); // Reg0 Does not relate to power down mode, write to complete sequence
    } else {
        res = (res) ? res : max2871_wr_dat(ob, addr, 0x01400005); // Reg5 Auto-Int-N mode when Fract=0
        res = (res) ? res : max2871_band_sel(freq_khz, &c);
        res = (res) ? res : max2871_wr_dat(ob, addr, c.reg4);
        res = (res) ? res : max2871_wr_dat(ob, addr, 0x00001F23); // Reg3
        res = (res) ? res : max2871_wr_dat(ob, addr, 0x08009E42); // Reg2  Icp = 5.00mA, MUX=0 to Enable RF driver chip (PCB v3.2 only)
        res = (res) ? res : max2871_wr_dat(ob, addr, c.reg1);     // Reg1 Modulus Value = modls
        res = (res) ? res : max2871_wr_dat(ob, addr, c.reg0);     // Reg0 N Value = intgr and Frac Value = fract
    }
    return res;
}

#if 0
// LOAD SYNTESIZER WITH FREQ RELATED CODES ONLY (MAX2871 in fractional N mode)
static
int max2871_update(ext_fe_100_5000_t* ob, unsigned addr, unsigned long int freq_khz)
{
    max2871_corr_t c;
    int res = 0;
    res = (res) ? res : max2871_band_sel(freq_khz, &c);
    res = (res) ? res : max2871_wr_dat(ob, addr, c.reg4);
    res = (res) ? res : max2871_wr_dat(ob, addr, c.reg1);     // Reg1 Modulus Value = modls
    res = (res) ? res : max2871_wr_dat(ob, addr, c.reg0);     // Reg0 N Value = intgr and Frac Value = fract
    return res;
}
#endif

// Check freq and select and return proper band
int tune_rf_path(ext_fe_100_5000_t* ob, uint64_t freq_khz)
{
    int res = 0;
    unsigned tg_freq = freq_khz;
    uint64_t lo2, lo1;

    if (freq_khz > 6000000) {
        return -EINVAL;
    }

    if (freq_khz >= 4625000) {
        // band 4" is 4625~4950 LO1=2375-2700 IF1=2250 LO2=1937.5
        ob->band = (4) - 1;
        ob->ifsel = 1;// switch 1st IF filter 2250 MHz
        lo2 = (IF1_FREQ2 - IF2_FREQ); // 225000 - 31250 = 193750
        lo1 = (tg_freq - IF1_FREQ2);
    } else if (freq_khz >= 3550000) {
        // band 4' is 3550~4625 LO1=2326-3401 IF1=1224 LO2=15365 (911.5)
        ob->band = (4) - 1;
        ob->ifsel = 0;// switch 1st IF filter 1224 MHz
        lo2 = (IF1_FREQ1 + IF2_FREQ); // 122400 + 31250 = 153650
        lo1 = (tg_freq - IF1_FREQ1);
    } else if (freq_khz >= 2650000) {
        // band 3 is 2650~3550 LO1=1426-2326 IF1=1224 LO2=911.5
        ob->band = (3) - 1;
        ob->ifsel = 0;// switch 1st IF filter 1224 MHz
        lo2 = (IF1_FREQ1 - IF2_FREQ); // 122400 - 31250 = 91150
        lo1 = (tg_freq - IF1_FREQ1);
    } else if (freq_khz >= 1950000) {
        // band 2 is 1950~2650 LO1=3174-3874 IF1=1224 LO2=911.5 (!!! IF 312.5 inverted in band 2 !!!)
        ob->band = (2) - 1;
        ob->ifsel = 0;// switch 1st IF filter 1224 MHz
        lo2 = (IF1_FREQ1 - IF2_FREQ); // 122400 - 31250 = 91150
        lo1 = (tg_freq + IF1_FREQ1);
    } else if (freq_khz >= 20000) {
        // band 1 is 20~1950 LO1=2320-4200 IF1=2250 LO2=2562.5
        ob->band = (1) - 1;
        ob->ifsel = 1;// switch 1st IF filter 2250 MHz
        lo2 = (IF1_FREQ2 + IF2_FREQ); // 225000 + 31250 = 256250
        lo1 = (tg_freq + IF1_FREQ2);
    } else {
        return -EINVAL;
    }

    res = (res) ? res : max2871_init(ob, ob->spi_lo2, lo2);
    res = (res) ? res : max2871_init(ob, ob->spi_lo1, lo1);
    res = (res) ? res : update_band_sel(ob); // Update band / ifsel
    return res;
}


int ext_fe_100_5000_rf_manual(ext_fe_100_5000_t* ob, uint64_t lo1_freq_khz, uint64_t lo2_freq_khz, unsigned band, unsigned ifsel)
{
    int res = 0;
    USDR_LOG("SEXT", USDR_LOG_ERROR, "IF1 = %d kHz, IF2 = %d kHz\n", (unsigned)lo1_freq_khz, (unsigned)lo2_freq_khz);

    ob->band = band;
    ob->ifsel = ifsel;
    res = (res) ? res : max2871_init(ob, ob->spi_lo2, lo2_freq_khz);
    res = (res) ? res : max2871_init(ob, ob->spi_lo1, lo1_freq_khz);
    res = (res) ? res : update_band_sel(ob); // Update band / ifsel
    return res;
}


int update_band_sel(ext_fe_100_5000_t* ob)
{
    USDR_LOG("SEXT", USDR_LOG_ERROR, "IFsel = %d, BAND = %d, LNA_GAINsel = %d\n", ob->ifsel, ob->band, ob->gain);
    int res = 0;
    uint8_t bandsel = 0;
    uint8_t pd_bnd4 = true;
    uint8_t pd_bnd3 = true;
    uint8_t pd_bnd2 = true;
    uint8_t en_bnd1 = false;

    switch (ob->band) {
    case 0: bandsel = SB_BAND1; en_bnd1 = true; break;
    case 1: bandsel = SB_BAND2; pd_bnd2 = false; break;
    case 2: bandsel = SB_BAND3; pd_bnd3 = false; break;
    case 3: bandsel = SB_BAND4; pd_bnd4 = false; break;
    default: break;
    }

    uint16_t cmd =
        ((ob->ifsel ? 1 : 0) << PIN_SEL_IF) |
        ((en_bnd1 ? 1 : 0) << PIN_EN_BAND1) |
        ((pd_bnd2 ? 1 : 0) << PIN_PD_BAND2) |
        ((pd_bnd3 ? 1 : 0) << PIN_PD_BAND3) |
        ((pd_bnd4 ? 1 : 0) << PIN_PD_BAND4) |
        ((bandsel) << PIN_SEL_BAND_0) |
        ((ob->gain & 0x3) << PIN_SEL_GAIN_0) |
        0x3;

    res = (res) ? res : xra1405il24_reg_wr(ob->dev, ob->subdev, ob->spi_xra, OCR1, cmd & 0xff);
    res = (res) ? res : xra1405il24_reg_wr(ob->dev, ob->subdev, ob->spi_xra, OCR2, cmd >> 8);
    return res;
}

int ext_fe_100_5000_set_frequency(ext_fe_100_5000_t* ob, uint64_t freq_hz)
{
    uint32_t tg_freq_khz = freq_hz / 1000;

    ob->lofreq_khz = tg_freq_khz;
    return tune_rf_path(ob, ob->lofreq_khz);
}

int ext_fe_100_5000_set_attenuator(ext_fe_100_5000_t* ob, unsigned atten0_25db)
{
    ob->atten = atten0_25db;
    return rfsa3713tr7_reg_wr(ob->dev, ob->subdev, ob->spi_attn, ob->atten);
}

int ext_fe_100_5000_set_lna(ext_fe_100_5000_t* ob, unsigned lna)
{
    ob->gain = lna;
    return update_band_sel(ob);
}

int ext_fe_100_5000_init(lldev_t dev,
                         unsigned subdev,
                         unsigned gpio_base,
                         unsigned spi_cfg_base,
                         unsigned spi_bus,
                         const char *params,
                         const char *compat,
                         ext_fe_100_5000_t* ob)
{
    int res = 0;
    uint8_t vals[2] = { 0xcc, 0xcc };

    if (strcmp(compat, "lsdr") != 0) {
        return -ENODEV;
    }

    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_SCK, GPIO_CFG_ALT0);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_MOSI, GPIO_CFG_ALT0);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_MISO, GPIO_CFG_ALT0);

    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_CS0, GPIO_CFG_ALT0);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_CS1, GPIO_CFG_ALT0);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_CS2, GPIO_CFG_ALT0);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_LE1, GPIO_CFG_ALT0);
    if (res)
        return res;

    ob->dev = dev;
    ob->subdev = subdev;
    ob->gpio_base = gpio_base;
    ob->spi_cfg_base = spi_cfg_base;
    ob->spi_bus = spi_bus;

    // Check expander
    ob->spi_xra = MAKE_SPI_EXT_LSOPADR(s_dev_cfg[DEV_EXPANDER], spi_cfg_base, spi_bus);
    ob->spi_lo1 = MAKE_SPI_EXT_LSOPADR(s_dev_cfg[DEV_LO1], spi_cfg_base, spi_bus);
    ob->spi_lo2 = MAKE_SPI_EXT_LSOPADR(s_dev_cfg[DEV_LO2], spi_cfg_base, spi_bus);
    ob->spi_attn = MAKE_SPI_EXT_LSOPADR(s_dev_cfg[DEV_ATTN], spi_cfg_base, spi_bus);

    res = (res) ? res : xra1405il24_reg_rd(dev, subdev, ob->spi_xra, GCR1, &vals[0]);

    res = (res) ? res : xra1405il24_reg_wr(dev, subdev, ob->spi_xra, GCR1, 3);    // Do not touch DCEN, RFEN
    res = (res) ? res : xra1405il24_reg_wr(dev, subdev, ob->spi_xra, GCR2, 0xC0);

    res = (res) ? res : xra1405il24_reg_wr(dev, subdev, ob->spi_xra, OCR1, 0);
    res = (res) ? res : xra1405il24_reg_wr(dev, subdev, ob->spi_xra, OCR2, 0);

    res = (res) ? res : xra1405il24_reg_rd(dev, subdev, ob->spi_xra, GCR1, &vals[0]);
    res = (res) ? res : xra1405il24_reg_rd(dev, subdev, ob->spi_xra, GCR2, &vals[1]);

    if (res)
        return res;

    USDR_LOG("SEXT", USDR_LOG_ERROR, "GCR1 = %02x  GCR2 = %02x\n", vals[0], vals[1]);
    if (vals[0] != 0x03 || vals[1] != 0xC0) {
        return -ENODEV;
    }

    ob->gain = 0;
    ob->band = 0;
    ob->ifsel = 0;
    ob->atten = 0;
    ob->lofreq_khz = 900000;

    tune_rf_path(ob, ob->lofreq_khz);

    res = (res) ? res : xra1405il24_reg_rd(dev, subdev, ob->spi_xra, GSR2, &vals[0]);
    USDR_LOG("SEXT", USDR_LOG_ERROR, "GCR2 = %02x\n", vals[0]);

    return res;
}



int ext_fe_100_5000_cmd_wr(ext_fe_100_5000_t* ob, uint32_t addr, uint32_t reg)
{
    int res;

    switch (addr) {
    case FE_FREQ: res = ext_fe_100_5000_set_frequency(ob, (uint64_t)reg * 1000); break;
    case FE_ATTN: res = ext_fe_100_5000_set_attenuator(ob, reg); break;
    case FE_PRESEL: res = ext_fe_100_5000_set_lna(ob, reg); break;
    default:
        return -EINVAL;
    }

    return res;
}

int ext_fe_100_5000_cmd_rd(ext_fe_100_5000_t* ob, uint32_t addr, uint32_t* preg)
{
    uint32_t ret = ~0u;
    switch (addr) {
    case FE_FREQ: ret = ob->lofreq_khz; break;
    case FE_ATTN: ret = ob->atten; break;
    case FE_PRESEL: ret = ob->gain; break;
    default:
        return -EINVAL;
    }

    *preg = ret;
    return 0;
}
