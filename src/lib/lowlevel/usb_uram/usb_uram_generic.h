// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef USB_URAM_GENERIC_H
#define USB_URAM_GENERIC_H

#include "../usdr_lowlevel.h"
#include <usdr_logging.h>
#include "../device/device_cores.h"
#include "../device/device_bus.h"
#include "../device/device_names.h"
#include "../../ipblks/si2c.h"


#define I2C_CORE_AUTO_LUTUPD  USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_DI2C_SIMPLE)
#define SPI_CORE_32W          USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_SIMPLE)

enum {
    DEV_RX_STREAM_NO = 0,
    DEV_TX_STREAM_NO = 1,
};

enum {
    TXSTRM_META_SZ = 16,

    // TODO Get rid of duplication constant, use DMA caps to calculate actual size
    MAX_TX_BUFFER_SZ = 126976, // (4k * 31)
};

enum {
    MAX_INTERRUPTS = 32,
    MSI_USRB_COUNT = 2,
    TO_IRQ_POLL = 250,
};

#define USB_IO_TIMEOUT 20000
#define WEBUSB_DEV_NAME "webusb"


typedef int (*io_read_fn_t)(lldev_t d, unsigned addr, uint32_t *data, unsigned dwcnt, UNUSED int timeout);
typedef int (*io_write_fn_t)(lldev_t d, unsigned addr, const uint32_t* data, unsigned dwcnt, UNUSED int timeout);
typedef int (*io_read_bus_fn_t)(lldev_t dev, unsigned interrupt_number, unsigned reg, size_t meminsz, void* pin);

struct usb_uram_io_ops
{
    io_read_fn_t io_read_fn;
    io_write_fn_t io_write_fn;
    io_read_bus_fn_t io_read_bus_fn;
};
typedef struct usb_uram_io_ops usb_uram_io_ops_t;

typedef const char* (*get_dev_name_fn_t)(lldev_t dev);
typedef device_id_t (*get_dev_id_fn_t)(lldev_t dev);

struct usb_uram_dev_ops
{
    get_dev_name_fn_t get_dev_name_fn;
    get_dev_id_fn_t get_dev_id_fn;
};
typedef struct usb_uram_dev_ops usb_uram_dev_ops_t;

struct usb_uram_generic
{
    device_bus_t db;

    unsigned spi_int_number[MAX_INTERRUPTS];
    unsigned i2c_int_number[MAX_INTERRUPTS];
    struct i2c_cache i2cc[4 * DBMAX_I2C_BUSES];

    uint32_t ntfy_seqnum_exp;

    usb_uram_io_ops_t io_ops;
    usb_uram_dev_ops_t dev_ops;
};
typedef struct usb_uram_generic usb_uram_generic_t;

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

int usb_uram_read_wait(lldev_t dev, unsigned lsop, lsopaddr_t ls_op_addr, size_t meminsz, void* pin);
int usb_uram_generic_create_and_init(lldev_t dev, unsigned pcount, const char** devparam,
                                     const char** devval);


const struct lowlevel_plugin *usb_uram_register();

#endif // USB_URAM_GENERIC_H
