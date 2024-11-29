#include "adf4002b.h"


int adf4002b_reg_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr, uint8_t reg,
                     uint32_t value)
{
    return lowlevel_spi_tr32(dev, subdev, ls_op_addr, (value & 0xfffffc) | (reg & 0x3), NULL);
}
