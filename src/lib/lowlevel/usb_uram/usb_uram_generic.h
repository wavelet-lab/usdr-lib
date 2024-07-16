// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef USB_URAM_GENERIC_H
#define USB_URAM_GENERIC_H

#include "../usdr_lowlevel.h"
#include <usdr_logging.h>
#include "../device/device_cores.h"
#include "../device/device_bus.h"
#include "../device/device_names.h"


#define I2C_CORE_AUTO_LUTUPD  USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_DI2C_SIMPLE)
#define SPI_CORE_32W          USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_SIMPLE)

#define USB_IO_TIMEOUT 20000
#define WEBUSB_DEV_NAME "webusb"


typedef int (*io_read_fn_t)(lldev_t d, unsigned addr, uint32_t *data, unsigned dwcnt, UNUSED int timeout);
typedef int (*io_write_fn_t)(lldev_t d, unsigned addr, const uint32_t* data, unsigned dwcnt, UNUSED int timeout);
typedef int (*io_read_wait_t)(lldev_t d, unsigned lsop, lsopaddr_t ls_op_addr, size_t meminsz, void* pin);

struct usb_uram_io_ops
{
    io_read_fn_t io_read_fn;
    io_write_fn_t io_write_fn;
    io_read_wait_t io_read_wait;
};

const struct lowlevel_plugin *usb_uram_register();


int usb_uram_reg_out(lldev_t dev, unsigned reg, uint32_t outval);
int usb_uram_reg_in(lldev_t dev, unsigned reg, uint32_t *pinval);
int usb_uram_reg_out_n(lldev_t dev, unsigned reg, const uint32_t *outval, const unsigned dwcnt);
int usb_uram_reg_in_n(lldev_t dev, unsigned reg, uint32_t *pinval, const unsigned dwcnt);
int usb_uram_reg_op(lldev_t dev, unsigned ls_op_addr,
                    uint32_t* ina, size_t meminsz, const uint32_t* outa, size_t memoutsz);

int usb_uram_ls_op(lldev_t dev, subdev_t subdev,
                   unsigned ls_op, lsopaddr_t ls_op_addr,
                   size_t meminsz, void* pin, size_t memoutsz,
                   const void* pout);

int usb_uram_generic_create_and_init(lldev_t dev, unsigned pcount, const char** devparam,
                                     const char** devval);

#endif // USB_URAM_GENERIC_H
