// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef USDR_LOWLEVEL_H
#define USDR_LOWLEVEL_H

#include <stdint.h>
#include <stddef.h>
#include <usdr_port.h>

enum lowlevel_ls_ops {
    USDR_LSOP_HWREG = 0, // Write followed by read
    USDR_LSOP_SPI = 1,
    USDR_LSOP_I2C_DEV = 2, // Address format [8 bit instance_no][8 bit bus_no][16 bit i2c_address]
    USDR_LSOP_URAM = 3,  // Read followed by write
    USDR_LSOP_DRP = 4, // Xilinx DRP port

    USDR_LSOP_CUSTOM_CMD = 65536, //Custom commands
};

#define MAKE_LSOP_I2C_ADDR(i, b, a)  (((i) & 0xff) << 24) | (((b) & 0xff) << 16) | (((a) & 0xffff) << 0)
#define LSOP_I2C_INSTANCE(ls)        (((ls) >> 24) & 0xff)
#define LSOP_I2C_BUSNO(ls)           (((ls) >> 16) & 0xff)
#define LSOP_I2C_ADDR(ls)            (((ls) >> 0)  & 0xffff)

enum sdr_type {
    SDR_NONE = 0,
    SDR_USDR = 1,
    SDR_XSDR = 2,
    SDR_LIME = 3,
};
typedef enum sdr_type sdr_type_t;

// Types of operations
// - High speed stream operations
// - Low speed paramter rw
// - Generic/Dictionary request
// - Async interface

struct device;
struct lowlevel_ops;
struct lowlevel_dev;

typedef uint64_t subdev_t;
typedef struct lowlevel_dev* lldev_t;
typedef unsigned lsopaddr_t;
typedef unsigned stream_t;

typedef struct device device_t;

enum lowlevel_generic_ops {
    LLGO_DEVICE_NAME,
    LLGO_DEVICE_UUID,
    LLGO_DEVICE_SDR_TYPE,
};

enum llstream_flags {
    LLSF_EXACT_VALUES = 1, //Fail if requested values can't be satisfied; otherwise use closest
    LLSF_NEED_FDPOLL = 2,
    LLSF_EXT_STAT = 4, // Deprectaed, DON'T USE IT
};

struct lowlevel_stream_params {
    unsigned flags;
    unsigned streamno;
    unsigned block_size;
    unsigned buffer_count;
    // Aux information of wire format

    /// Number of harware of channels streaming. For virtual streams ==0
    unsigned channels;

    /// Bits per sample in case of complex samples it's doubled (e.g. for ci16 == 32).
    /// It corresponds number of bits for all channels (might be useful for compressed format)
    unsigned bits_per_sym;
    int underlying_fd; ///< FD used for select/poll/epoll calls to get rid of blocking dma_wait/dma_get operations. Multiple streams may share same fd

    size_t out_mtu_size; ///< Maximum transfer size for single transfer (return
};
typedef struct lowlevel_stream_params lowlevel_stream_params_t;

struct lowlevel_ops {
    int (*generic_get)(lldev_t dev, int generic_op, const char** pout);

    // IO functions
    int (*ls_op)(lldev_t dev, subdev_t subdev,
                 unsigned ls_op, lsopaddr_t ls_op_addr,
                 size_t meminsz, void* pin, size_t memoutsz,
                 const void* pout);

    // Stream functions
    int (*stream_initialize)(lldev_t dev, subdev_t subdev, lowlevel_stream_params_t* params, stream_t* channel);
    int (*stream_deinitialize)(lldev_t dev, subdev_t subdev, stream_t channel);

    // Zero-copy Ring DMA functions
    int (*recv_dma_wait)(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer, void* oob_ptr, unsigned *oob_size, unsigned timeout);
    int (*recv_dma_release)(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer);

    int (*send_dma_get)(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer, void* oob_ptr, unsigned *oob_size, unsigned timeout);
    int (*send_dma_commit)(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer, unsigned sz, const void* oob_ptr, unsigned oob_size);

    // Normal functions (copy)
    int (*recv_buf)(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer, unsigned *expected_sz, void* oob_ptr, unsigned *oob_size, unsigned timeout);
    int (*send_buf)(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer, unsigned sz, const void* oob_ptr, unsigned oob_size, unsigned timeout);

    // TODO Async operations
    int (*await)(lldev_t dev, subdev_t subdev, unsigned await_id, unsigned op, void** await_inout_aux_data, unsigned timeout);

    int (*destroy)(lldev_t dev);
};
typedef struct lowlevel_ops lowlevel_ops_t;

lowlevel_ops_t* lowlevel_get_ops(lldev_t dev);
const char* lowlevel_get_devname(lldev_t dev);
const uint8_t* lowlevel_get_uuid(lldev_t dev);

static inline int lowlevel_ls_op(lldev_t dev, subdev_t subdev,
                                 unsigned ls_op, lsopaddr_t ls_op_addr,
                                 size_t meminsz, void* pin, size_t memoutsz,
                                 const void* pout) {
    return lowlevel_get_ops(dev)->ls_op(dev, subdev, ls_op, ls_op_addr,
                                        meminsz, pin, memoutsz, pout);
}

static inline int lowlevel_reg_wr16(lldev_t dev, subdev_t subdev,
                                    lsopaddr_t ls_op_addr, uint16_t out) {
    return lowlevel_get_ops(dev)->ls_op(dev, subdev, USDR_LSOP_HWREG, ls_op_addr,
                                        0, NULL, 2, &out);
}

static inline int lowlevel_reg_wr32(lldev_t dev, subdev_t subdev,
                                    lsopaddr_t ls_op_addr, uint32_t out) {
    return lowlevel_get_ops(dev)->ls_op(dev, subdev, USDR_LSOP_HWREG, ls_op_addr,
                                        0, NULL, 4, &out);
}

static inline int lowlevel_reg_wrndw(lldev_t dev, subdev_t subdev,
                                    lsopaddr_t ls_op_addr, const uint32_t* out, unsigned ndw) {
    return lowlevel_get_ops(dev)->ls_op(dev, subdev, USDR_LSOP_HWREG, ls_op_addr,
                                        0, NULL, 4 * ndw, out);
}

static inline int lowlevel_reg_rd32(lldev_t dev, subdev_t subdev,
                                    lsopaddr_t ls_op_addr, uint32_t *pout) {
    return lowlevel_get_ops(dev)->ls_op(dev, subdev, USDR_LSOP_HWREG, ls_op_addr,
                                        4, pout, 0, NULL);
}

static inline int lowlevel_reg_rd16(lldev_t dev, subdev_t subdev,
                                    lsopaddr_t ls_op_addr, uint16_t *pout) {
    return lowlevel_get_ops(dev)->ls_op(dev, subdev, USDR_LSOP_HWREG, ls_op_addr,
                                        2, pout, 0, NULL);
}

static inline int lowlevel_reg_rdndw(lldev_t dev, subdev_t subdev,
                                    lsopaddr_t ls_op_addr, uint32_t *pout, unsigned ndw) {
    return lowlevel_get_ops(dev)->ls_op(dev, subdev, USDR_LSOP_HWREG, ls_op_addr,
                                        4 * ndw, pout, 0, NULL);
}

static inline int lowlevel_spi_tr32(lldev_t dev, subdev_t subdev,
                                    lsopaddr_t ls_op_addr, uint32_t tout, uint32_t* tin) {
    return lowlevel_get_ops(dev)->ls_op(dev, subdev, USDR_LSOP_SPI, ls_op_addr,
                                        (tin) ? 4 : 0, tin, 4, &tout);
}

static inline int lowlevel_drp_wr16(lldev_t dev, subdev_t subdev, unsigned port,
                                    uint16_t regaddr, uint16_t out) {
    return lowlevel_get_ops(dev)->ls_op(dev, subdev, USDR_LSOP_DRP, (port << 16) | regaddr,
                                        0, NULL, 2, &out);
}

static inline int lowlevel_drp_rd16(lldev_t dev, subdev_t subdev, unsigned port,
                                    uint16_t regaddr, uint16_t *pout) {
    return lowlevel_get_ops(dev)->ls_op(dev, subdev, USDR_LSOP_DRP, (port << 16) | regaddr,
                                        2, pout, 0, NULL);
}

static inline int lowlevel_destroy(lldev_t dev) {
    return lowlevel_get_ops(dev)->destroy(dev);
}

enum lowlevel_plugin_info {
    LLPI_NAME_STR,
    LLPI_DESCRIPTION_STR,
    LLPI_VERSION_STR,
};

// Standard llparameters name
#define LL_DEVICE_PARAM  "device"
#define LL_DEVICE_SERIAL "serial"


struct lowlevel_plugin {
    const char* (*info_str)(unsigned iparam);
    // Outbuf, parameters are separated through TAB
    int (*discovery)(unsigned pcount, const char** filterparams, const char** filtervals, unsigned maxbuf, char* outbuf);
    int (*create)(unsigned pcount, const char** devparam, const char** devval, lldev_t* odev, unsigned vidpid, void* webops, uintptr_t param);
};

//int lowlevel_info(const char* driver, unsigned iparam, size_t osz, char* obuffer);

// devparam and devval filtering parameters
// devices are separated through VTAB \v; individual parameters separated through TAB \t
int lowlevel_discovery(unsigned pcount, const char** devparam, const char **devval,
                       unsigned maxbuf, char* buf);
int lowlevel_create(unsigned pcount, const char** devparam, const char **devval, lldev_t* odev, unsigned vidpid, void* webops, uintptr_t param);


device_t* lowlevel_get_device(lldev_t obj);

// Basic object
struct lowlevel_dev {
    lowlevel_ops_t* ops;
    device_t* pdev;
};
typedef struct lowlevel_dev lowlevel_dev_t;


#endif
