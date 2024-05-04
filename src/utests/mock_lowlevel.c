// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "mock_lowlevel.h"
#include <usdr_logging.h>
#include <stdlib.h>
#include <string.h>

static
int mock_generic_get(UNUSED lldev_t dev,
                     UNUSED int generic_op,
                     UNUSED const char** pout)
{
    return -EOPNOTSUPP;
}

// IO functions
static
int mock_ls_op(UNUSED lldev_t dev, UNUSED subdev_t subdev,
             unsigned ls_op, lsopaddr_t ls_op_addr,
             size_t meminsz, void* pin, size_t memoutsz,
             const void* pout)
{
    struct mock_lowlevel_dev* mld = (struct mock_lowlevel_dev*)dev;
    unsigned i;
    switch (ls_op) {
    case USDR_LSOP_HWREG: {
        uint32_t* ina = (uint32_t*)pin;
        const uint32_t* outa = (const uint32_t*)pout;

        // Normal operation
        for (i = 0; i < memoutsz / 4; i++) {
            USDR_LOG("MOCK", USDR_LOG_TRACE, "Write[%d] <= %08x\n",
                     ls_op_addr + i, outa[i]);
        }
        for (i = 0; i < meminsz / 4; i++) {
            ina[i] = ~0u;
            USDR_LOG("MOCK", USDR_LOG_TRACE, "Read [%d] => %08x\n",
                     ls_op_addr + i, ina[i]);
        }
        return 0;
    }
    case USDR_LSOP_SPI: {
        if (((meminsz != 4) && (meminsz != 0)) || (memoutsz != 4))
            return -EINVAL;
        uint32_t din = ~0u;

        if (mld->mock_func->mock_spi_tr32) {
            int res = mld->mock_func->mock_spi_tr32(ls_op_addr, *(const uint32_t*)pout, &din);
            if (res)
                return res;
        }
        USDR_LOG("MOCK", USDR_LOG_TRACE, "SPI%d %08x => %08x\n",
                 ls_op_addr, *(const uint32_t*)pout, din);

        if (meminsz) {
            *(uint32_t*)pin = din;
        }
        return 0;
    }
    case USDR_LSOP_I2C_DEV: {
        if (memoutsz > 3)
            return -EINVAL;
        if (meminsz > 4)
            return -EINVAL;

        uint32_t i2cout = 0;
        uint32_t i2cin = ~0u;
        memcpy(&i2cout, pout, memoutsz);

        USDR_LOG("PCIE", USDR_LOG_NOTE, "I2C%d: W=%zd R=%zd DW=%08x\n",
                 ls_op_addr, memoutsz, meminsz, i2cout);

        memcpy(pin, &i2cin, meminsz);
        return 0;
    }
    }
    return -EOPNOTSUPP;
}

// Stream functions
static
int mock_stream_initialize(lldev_t dev, subdev_t subdev, lowlevel_stream_params_t* params, stream_t* channel)
{
    return -EOPNOTSUPP;
}
static
int mock_stream_deinitialize(lldev_t dev, subdev_t subdev, stream_t channel)
{
    return -EOPNOTSUPP;
}

// Zero-copy DMA functions
static
int mock_recv_dma_wait(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer, void* oob_ptr, unsigned *oob_size, unsigned timeout)
{
    return -EOPNOTSUPP;
}
static
int mock_recv_dma_release(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer)
{
    return -EOPNOTSUPP;
}

static
int mock_send_dma_get(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer, void* oob_ptr, unsigned *oob_size, unsigned timeout)
{
    return -EOPNOTSUPP;
}

static
int mock_send_dma_commit(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer, unsigned sz, const void* oob_ptr, unsigned oob_size)
{
    return -EOPNOTSUPP;
}

// Normal functions (copy)
static
int mock_recv_buf(lldev_t dev, subdev_t subdev, stream_t channel, void** buffer, unsigned *expected_sz, void* oob_ptr, unsigned *oob_size, unsigned timeout)
{
    return -EOPNOTSUPP;
}
static
int mock_send_buf(lldev_t dev, subdev_t subdev, stream_t channel, void* buffer, unsigned sz, const void* oob_ptr, unsigned oob_size, unsigned timeout)
{
    return -EOPNOTSUPP;
}

// TODO Async operations
static
int mock_await(lldev_t dev, subdev_t subdev, unsigned await_id, unsigned op, void** await_inout_aux_data, unsigned timeout)
{
    return -EOPNOTSUPP;
}

static
int mock_destroy(lldev_t dev)
{
    free(dev);
    return 0;
}

static
struct lowlevel_ops s_mock_ops = {
    mock_generic_get,
    mock_ls_op,
    mock_stream_initialize,
    mock_stream_deinitialize,
    mock_recv_dma_wait,
    mock_recv_dma_release,
    mock_send_dma_get,
    mock_send_dma_commit,
    mock_recv_buf,
    mock_send_buf,
    mock_await,
    mock_destroy,
};

lldev_t mock_lowlevel_create(const struct mock_functions *mf)
{
    struct mock_lowlevel_dev* mld = (struct mock_lowlevel_dev*)malloc(sizeof(struct mock_lowlevel_dev));
    mld->base.ops = &s_mock_ops;
    mld->base.pdev = NULL;
    mld->mock_func = mf;
    return &mld->base;
}
