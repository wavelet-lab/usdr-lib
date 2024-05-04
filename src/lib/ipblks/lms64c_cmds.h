// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMS64C_CMD_H
#define LMS64C_CMD_H

enum eCMD_LMS
{
    CMD_GET_INFO = 0x00,
    CMD_LMS6002_RST = 0x10,
    ///Writes data to SI5356 synthesizer via I2C
    CMD_SI5356_WR = 0x11,
    ///Reads data from SI5356 synthesizer via I2C
    CMD_SI5356_RD = 0x12,
    ///Writes data to SI5351 synthesizer via I2C
    CMD_SI5351_WR = 0x13,
    ///Reads data from SI5351 synthesizer via I2C
    CMD_SI5351_RD = 0x14,
    ///PanelBus DVI (HDMI) Transmitter control
    CMD_TFP410_WR = 0x15,
    ///PanelBus DVI (HDMI) Transmitter control
    CMD_TFP410_RD = 0x16,
    ///Sets new LMS7002M chip’s RESET pin level (0, 1, pulse)
    CMD_LMS7002_RST = 0x20,
    ///Writes data to LMS7002M chip via SPI
    CMD_LMS7002_WR = 0x21,
    ///Reads data from LMS7002M chip via SPI
    CMD_LMS7002_RD = 0x22,
    ///Writes data to LMS6002 chip via SPI
    CMD_LMS6002_WR = 0x23,
    ///Reads data from LMS6002 chip via SPI
    CMD_LMS6002_RD = 0x24,

    CMD_LMS_LNA = 0x2A,
    CMD_LMS_PA = 0x2B,

    CMD_PROG_MCU = 0x2C,
    ///Writes data to ADF4002 chip via SPI
    CMD_ADF4002_WR = 0x31,

    CMD_USB_FIFO_RST = 0x40,
    CMD_PE636040_WR = 0x41,
    CMD_PE636040_RD = 0x42,

    CMD_GPIO_DIR_WR = 0x4F,
    CMD_GPIO_DIR_RD = 0x50,
    CMD_GPIO_WR = 0x51,
    CMD_GPIO_RD = 0x52,

    CMD_ALTERA_FPGA_GW_WR = 0x53,
    CMD_ALTERA_FPGA_GW_RD = 0x54,

    CMD_BRDSPI_WR = 0x55,//16 bit spi for stream, dataspark control
    CMD_BRDSPI_RD = 0x56,//16 bit spi for stream, dataspark control
    CMD_BRDSPI8_WR = 0x57, //8 + 8 bit spi for stream, dataspark control
    CMD_BRDSPI8_RD = 0x58, //8 + 8 bit spi for stream, dataspark control

    CMD_BRDCONF_WR = 0x5D, //write config data to board
    CMD_BRDCONF_RD = 0x5E, //read config data from board

    CMD_ANALOG_VAL_WR = 0x61, //write analog value
    CMD_ANALOG_VAL_RD = 0x62, //read analog value

    CMD_MYRIAD_RST = 0x80,
    CMD_MYRIAD_WR = 0x81,
    CMD_MYRIAD_RD = 0x82,
    CMD_MEMORY_WR = 0x8C,
    CMD_MEMORY_RD = 0x8D
};

enum eCMD_STATUS
{
    STATUS_UNDEFINED,
    STATUS_COMPLETED_CMD,
    STATUS_UNKNOWN_CMD,
    STATUS_BUSY_CMD,
    STATUS_MANY_BLOCKS_CMD,
    STATUS_ERROR_CMD,
    STATUS_WRONG_ORDER_CMD,
    STATUS_RESOURCE_DENIED_CMD,
    STATUS_COUNT
};

enum eLMS_DEV
{
    LMS_DEV_UNKNOWN                 = 0,
    LMS_DEV_EVB6                    = 1,
    LMS_DEV_DIGIGREEN               = 2,
    LMS_DEV_DIGIRED                 = 3, //2x USB3, LMS6002,.
    LMS_DEV_EVB7                    = 4,
    LMS_DEV_ZIPPER                  = 5, //MyRiad bridge to FMC, HSMC bridge
    LMS_DEV_SOCKETBOARD             = 6,
    LMS_DEV_EVB7V2                  = 7,
    LMS_DEV_STREAM                  = 8, //Altera Cyclone IV, USB3, 2x 128 MB RAM, RFDIO, FMC
    LMS_DEV_NOVENA                  = 9, //Freescale iMX6 CPU
    LMS_DEV_DATASPARK               = 10, //Altera Cyclone V, 2x 256 MB RAM, 2x FMC (HPC, LPC), USB3
    LMS_DEV_RFSPARK                 = 11, //LMS7002 EVB
    LMS_DEV_LMS6002USB              = 12, //LM6002-USB (USB stick: FX3, FPGA, LMS6002, RaspberryPi con)
    LMS_DEV_RFESPARK                = 13, //LMS7002 EVB
    LMS_DEV_LIMESDR                 = 14, //LimeSDR-USB, 32bit FX3, 2xRAM, LMS7
    LMS_DEV_LIMESDR_PCIE            = 15,
    LMS_DEV_LIMESDR_QPCIE           = 16, //2x LMS, 14 bit ADC and DAC
    LMS_DEV_LIMESDRMINI             = 17, //FTDI + MAX10 + LMS
    LMS_DEV_USTREAM                 = 18, //with expansion booards (uMyriad)
    LMS_DEV_LIMESDR_SONY_PA         = 19, //stand alone board with Sony PAs, tuners
    LMS_DEV_LIMESDR_USB_SP          = 20,
    LMS_DEV_LMS7002M_ULTIMATE_EVB   = 21,
    LMS_DEV_LIMENET_MICRO           = 22, //Raspberry Pi CM3(L), Ethernet, MAX10, LMS7002,.
    LMS_DEV_LIMESDR_CORE_SDR        = 23, //LMS7002, Intel Cyclone 4, RAM, GNSS
    LMS_DEV_LIMESDR_CORE_HE         = 24, //PA board
    LMS_DEV_LIMESDRMINI_V2          = 25, //FTDI + ECP5 + LMS
    LMS_DEV_USDR_LMS6               = 26, //USDR board + LMS6002

    LMS_DEV_COUNT
};

#endif
