// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef USB_URAM_GENERIC_H
#define USB_URAM_GENERIC_H

#include "../usdr_lowlevel.h"
#include <usdr_logging.h>
#include "../device/device_cores.h"

#define I2C_CORE_AUTO_LUTUPD  USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_DI2C_SIMPLE)
#define SPI_CORE_32W          USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_SIMPLE)

const struct lowlevel_plugin *usb_uram_register();

#endif // USB_URAM_GENERIC_H
