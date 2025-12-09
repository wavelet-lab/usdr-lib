#include "usb_uram_generic.h"
#include "../ipblks/si2c.h"
#include "../ipblks/spiext.h"

#include <string.h>
#include <stdio.h>
#include "../../device/generic_usdr/generic_regs.h"
#include <assert.h>

usb_uram_generic_t* get_uram_generic(lldev_t dev);

int usb_uram_reg_out(lldev_t dev, unsigned reg, uint32_t outval)
{
    usb_uram_generic_t* gen = get_uram_generic(dev);

    int res = gen->io_ops.io_write_fn(dev, reg, &outval, 1, USB_IO_TIMEOUT);

    USDR_LOG(USBG_LOG_TAG, USDR_LOG_DEBUG, "%s: Write [%04x] = %08x (%d)\n",
             lowlevel_get_devname(dev), reg, outval, res);
    return res;
}

int usb_uram_reg_in(lldev_t dev, unsigned reg, uint32_t *pinval)
{
    uint32_t inval;
    usb_uram_generic_t* gen = get_uram_generic(dev);

    int	res = gen->io_ops.io_read_fn(dev, reg, &inval, 1, USB_IO_TIMEOUT);

    USDR_LOG(USBG_LOG_TAG, USDR_LOG_DEBUG, "%s: Read  [%04x] = %08x (%d)\n",
             lowlevel_get_devname(dev), reg, inval, res);
    *pinval = inval;
    return res;
}

int usb_uram_reg_out_n(lldev_t dev, unsigned reg, const uint32_t *outval, const unsigned dwcnt)
{
    usb_uram_generic_t* gen = get_uram_generic(dev);

    int res = gen->io_ops.io_write_fn(dev, reg, outval, dwcnt, USB_IO_TIMEOUT);

    USDR_LOG(USBG_LOG_TAG, USDR_LOG_DEBUG, "%s: WriteArray [%04x + %d] (%d)\n",
             lowlevel_get_devname(dev), reg, dwcnt, res);
    return res;
}

int usb_uram_reg_in_n(lldev_t dev, unsigned reg, uint32_t *pinval, const unsigned dwcnt)
{
    unsigned off = 0;
    unsigned rem = dwcnt;
    unsigned sz = rem;
    usb_uram_generic_t* gen = get_uram_generic(dev);

    for (; rem != 0; ) {
        if (sz > 256 / 4)
            sz = 256 / 4;

        int	res = gen->io_ops.io_read_fn(dev, reg + off, pinval + off, sz, USB_IO_TIMEOUT);

        USDR_LOG(USBG_LOG_TAG, USDR_LOG_DEBUG, "%s: ReadArray [%04x + %d] (%d)\n",
                 lowlevel_get_devname(dev), reg, sz, res);
        if (res)
            return res;

        off += sz;
        rem -= sz;
    }

    return 0;
}

int usb_uram_reg_op(lldev_t dev, unsigned ls_op_addr,
                    uint32_t* ina, size_t meminsz, const uint32_t* outa, size_t memoutsz)
{
    unsigned i;
    int res;
    usb_uram_generic_t* gen = get_uram_generic(dev);
    device_bus_t* pdb = &gen->db;

    if ((meminsz % 4) || (memoutsz % 4))
        return -EINVAL;

    for (unsigned k = 0; k < pdb->idx_regsps; k++) {
        if (ls_op_addr >= pdb->idxreg_virt_base[k]) {
            // Indexed register operation
            unsigned amax = ((memoutsz > meminsz) ? memoutsz : meminsz) / 4;

            for (i = 0; i < amax; i++) {
                //Write address
                res = usb_uram_reg_out(dev, pdb->idxreg_base[k],
                                       ls_op_addr - pdb->idxreg_virt_base[k] + i);
                if (res)
                    return res;

                if (i < memoutsz / 4) {
                    res = usb_uram_reg_out(dev, pdb->idxreg_base[k] + 1, outa[i]);
                    if (res)
                        return res;
                }

                if (i < meminsz / 4) {
                    res = usb_uram_reg_in(dev, pdb->idxreg_base[k] + 1, &ina[i]);
                    if (res)
                        return res;
                }
            }

            return 0;
        }
    }
#if 1
    // TODO Wrap to 128b
    if (memoutsz > 4) {
        res = usb_uram_reg_out_n(dev, ls_op_addr, outa, memoutsz / 4);
        if (res)
            return res;
    } else if (memoutsz == 4) {
        res = usb_uram_reg_out(dev, ls_op_addr, outa[0]);
        if (res)
            return res;
    }

    if (meminsz > 4) {
        res = usb_uram_reg_in_n(dev, ls_op_addr, ina, meminsz / 4);
        if (res)
            return res;
    } else if (meminsz == 4) {
        res = usb_uram_reg_in(dev, ls_op_addr, ina);
        if (res)
            return res;
    }
#else
    // Normal operation
    for (i = 0; i < memoutsz / 4; i++) {
        res = usb_uram_reg_out(dev, ls_op_addr + i, outa[i]);
        if (res)
            return res;
    }
    for (i = 0; i < meminsz / 4; i++) {
        res = usb_uram_reg_in(dev, ls_op_addr + i, &ina[i]);
        if (res)
            return res;
    }
#endif
    return 0;
}

int usb_uram_ls_op(lldev_t dev, subdev_t subdev,
                   unsigned ls_op, lsopaddr_t ls_op_addr,
                   size_t meminsz, void* pin,
                   size_t memoutsz, const void* pout)
{
    int res;
    usb_uram_generic_t* gen = get_uram_generic(dev);
    device_bus_t* pdb = &gen->db;
    struct i2c_cache* pi2cc = gen->i2cc;

    switch (ls_op) {
    case USDR_LSOP_HWREG:
    {
        uint32_t* ina = (uint32_t*)pin;
        const uint32_t* outa = (const uint32_t*)pout;

        return usb_uram_reg_op(dev, ls_op_addr, ina, meminsz, outa, memoutsz);
    }
    case USDR_LSOP_SPI: {
        if (SPIEXT_LSOP_GET_BUS(ls_op_addr) >= pdb->spi_count)
            return -EINVAL;

        if (pdb->spi_core[ls_op_addr] == SPI_CORE_32W) {
            if (((meminsz != 4) && (meminsz != 0)) || (memoutsz != 4))
                return -EINVAL;

            res = usb_uram_reg_out(dev, pdb->spi_base[SPIEXT_LSOP_GET_BUS(ls_op_addr)],
                                   *(const uint32_t*)pout);
        } else if (pdb->spi_core[ls_op_addr] == SPI_CORE_CFGW_CS8) {
            uint32_t spi_tr[2] = {
                SPIEXT_LSOP_GET_CFG(ls_op_addr),
                spiext_make_data_reg(memoutsz, pout)
            };

            //Select CSn on the BUS
            res = usb_uram_reg_out_n(dev, pdb->spi_base[SPIEXT_LSOP_GET_BUS(ls_op_addr)] - 1,
                                     spi_tr, 2);
        } else {
            return -EINVAL;
        }

        if (res)
            return res;

        res = usb_uram_read_wait(dev, ls_op, ls_op_addr, meminsz, pin);
        return res;
    }
    case USDR_LSOP_I2C_DEV: {
        uint32_t i2ccmd[2], data = 0;
        const uint8_t* dd = (const uint8_t*)pout;
        uint8_t* di = (uint8_t*)pin;
        uint8_t instance_no = LSOP_I2C_INSTANCE(ls_op_addr);
        uint8_t bus_no = LSOP_I2C_BUSNO(ls_op_addr);
        uint8_t i2caddr = LSOP_I2C_ADDR(ls_op_addr);
        unsigned lidx;

        if (instance_no >= pdb->i2c_count)
            return -EINVAL;
        if (pdb->i2c_core[instance_no] != I2C_CORE_AUTO_LUTUPD)
            return -EINVAL;

        if (bus_no > 1)
            return -EINVAL;

        lidx = si2c_update_lut_idx(&pi2cc[4 * instance_no], i2caddr, bus_no);
        i2ccmd[0] = si2c_get_lut(&pi2cc[4 * instance_no]);
        res = si2c_make_ctrl_reg(lidx, dd, memoutsz, meminsz, &i2ccmd[1]);
        if (res)
            return res;

        USDR_LOG(USBG_LOG_TAG, USDR_LOG_DEBUG, "%s: I2C[%d.%d.%02x] LUT:CMD %08x.%08x\n",
                 lowlevel_get_devname(dev), instance_no, bus_no, i2caddr, i2ccmd[0], i2ccmd[1]);

        res = usb_uram_reg_out_n(dev, pdb->i2c_base[instance_no] - 1, i2ccmd, 2);
        if (res)
            return res;

        if (meminsz > 0) {

            res = usb_uram_read_wait(dev, ls_op, instance_no, meminsz, &data);
            if (res)
                return res;

            if (meminsz == 1) {
                di[0] = data;
            } else if (meminsz == 2) {
                di[0] = data;
                di[1] = data >> 8;
            } else if (meminsz == 3) {
                di[0] = data;
                di[1] = data >> 8;
                di[2] = data >> 16;
            } else {
                *(uint32_t*)pin = data;
            }
        }
        return 0;
    }
    case USDR_LSOP_DRP: {
        return device_bus_drp_generic_op(dev, subdev, pdb, ls_op_addr, meminsz, pin, memoutsz, pout);
    }
    case USDR_LSOP_GPI: {
        return device_bus_gpi_generic_op(dev, subdev, pdb, ls_op_addr, meminsz, pin, memoutsz, pout);
    }
    case USDR_LSOP_GPO: {
        return device_bus_gpo_generic_op(dev, subdev, pdb, ls_op_addr, meminsz, pin, memoutsz, pout);
    }
    }
    return -EOPNOTSUPP;
}

int usb_uram_read_wait(lldev_t dev, unsigned lsop, lsopaddr_t ls_op_addr, size_t meminsz, void* pin)
{
    int res;
    usb_uram_generic_t* gen = get_uram_generic(dev);

    unsigned int_number, reg;
    const char* busname;

    switch(lsop)
    {
    case USDR_LSOP_SPI:
        int_number = gen->spi_int_number[ls_op_addr];
        reg = gen->db.spi_core[ls_op_addr];
        busname = "SPI";
        break;
    case USDR_LSOP_I2C_DEV:
        int_number = gen->i2c_int_number[ls_op_addr];
        reg = gen->db.i2c_base[ls_op_addr];
        busname = "I2C";
        break;
    default:
        return -EOPNOTSUPP;
    }

    res = gen->io_ops.io_read_bus_fn(dev, int_number, reg, meminsz, pin);
    if (res)
    {
        USDR_LOG(USBG_LOG_TAG, USDR_LOG_ERROR, "%s: %s%d MSI wait timed out! res=%d\n",
                 lowlevel_get_devname(dev), busname, ls_op_addr, res);
    }

    return res;
}

int usb_uram_generic_create_and_init(lldev_t dev, unsigned pcount, const char** devparam,
                                     const char** devval, device_id_t* pdevid)
{
    int res;
    usb_uram_generic_t* gen = get_uram_generic(dev);
    device_bus_t* pdb = &gen->db;
    const char* devname = lowlevel_get_devname(dev);

    res = usdr_device_create(dev, *pdevid);
    if (res) {
        USDR_LOG(USBG_LOG_TAG, USDR_LOG_ERROR,
                 "Unable to find device spcec for %s, uuid %s! Update software!\n",
                 devname, usdr_device_id_to_str(*pdevid));

        return res;
    }

    res = device_bus_init(dev->pdev, pdb);
    if (res) {
        USDR_LOG(USBG_LOG_TAG, USDR_LOG_ERROR,
                 "Unable to initialize bus parameters for the device %s!\n", devname);

        return res;
    }

    struct device_params {
        const char* path;
        unsigned *store;
        unsigned count;
    } bii[] = {
               { DNLLFP_IRQ(DN_BUS_SPI, "%d"), gen->spi_int_number, pdb->spi_count },
               { DNLLFP_IRQ(DN_BUS_I2C, "%d"), gen->i2c_int_number, pdb->i2c_count },
               //{ DNLLFP_IRQ(DN_SRX, "%d"), dl.stream_int_number, dev->db.srx_count },
               //{ DNLLFP_IRQ(DN_STX, "%d"), dl.stream_int_number + dev->db.srx_count, dev->db.stx_count },
               //{ DNLLFP_NAME(DN_SRX, "%d", DNP_DMACAP), dl.stream_cap, dev->db.srx_count },
               //{ DNLLFP_NAME(DN_STX, "%d", DNP_DMACAP), dl.stream_cap + dev->db.srx_count, dev->db.stx_count },
               };
    uint64_t tmp;
    uint32_t interrupt_count;
    uint32_t interrupt_base;
    uint32_t int_mask = 0;
    for (unsigned i = 0; i < SIZEOF_ARRAY(bii); i++) {
        for (unsigned j = 0; j < bii[i].count; j++) {
            uint64_t tmp;
            char buffer[32];

            snprintf(buffer, sizeof(buffer), bii[i].path, j);
            res = usdr_device_vfs_obj_val_get_u64(dev->pdev, buffer, &tmp);
            if (res) {
                return res;
            }

            bii[i].store[j] = (unsigned)tmp;
            int_mask |= (1u << tmp);
        }
    }

    // TODO move hwid to 0
    // IGPI / HWID
    uint32_t hwid;

    res = usb_uram_reg_in(dev, 16 + (IGPI_HWID / 4), &hwid);
    if (res)
        return res;

    if (hwid & 0x40000000) {

        // INIT Interrupts only if PCIe is down
        res = usb_uram_reg_out(dev, 0xf, int_mask);
        if (res)
            return res;
        // Put stream to 1

        res = usdr_device_vfs_obj_val_get_u64(dev->pdev, DNLL_IRQ_COUNT, &tmp);
        if (res)
            return res;

        interrupt_count = tmp;

        res = usdr_device_vfs_obj_val_get_u64(dev->pdev, DNLLFP_BASE(DNP_IRQ, "0"), &tmp);
        if (res)
            return res;

        interrupt_base = tmp;

        //Do these in case of pure USB only
        const unsigned REG_WR_MBUS2_ADDR= 6;
        const unsigned REG_WR_MBUS2_DATA= 7;
        const unsigned REG_WR_PNTFY_CFG = 8;
        const unsigned REG_WR_PNTFY_ACK = 9;

        res = res ? res : usb_uram_reg_out(dev, REG_WR_PNTFY_CFG, 0xdeadb000);
        res = res ? res : usb_uram_reg_out(dev, REG_WR_PNTFY_CFG, 0xeeadb001);
        res = res ? res : usb_uram_reg_out(dev, REG_WR_PNTFY_CFG, 0xfeadb002);
        res = res ? res : usb_uram_reg_out(dev, REG_WR_PNTFY_CFG, 0xaeadb003);
        res = res ? res : usb_uram_reg_out(dev, REG_WR_PNTFY_CFG, 0xbeadb004);
        res = res ? res : usb_uram_reg_out(dev, REG_WR_PNTFY_CFG, 0xceadb005);
        res = res ? res : usb_uram_reg_out(dev, REG_WR_PNTFY_CFG, 0x9eadb006);
        res = res ? res : usb_uram_reg_out(dev, REG_WR_PNTFY_CFG, 0x8eadb007);

        for (unsigned i = 0; i < interrupt_count; i++) {
            res = res ? res : usb_uram_reg_out(dev, interrupt_base,  i | (i << 8) | (1 << 16) | (7 << 20));
            res = res ? res : usb_uram_reg_out(dev, REG_WR_PNTFY_ACK, 0 | i << 16);
        }

        // IGPO_FRONT activates tran_usb_active to route interrupts to NTFY endpoint
        res = res ? res : usb_uram_reg_out(dev, 0, (15u << 24) | (0x80));
        if (res) {
            USDR_LOG(USBG_LOG_TAG, USDR_LOG_ERROR,
                     "Unable to set stream routing, error %d\n", res);
            return res;
        }

        // Set no limit for RX USB transfers
        res = res ? res : usb_uram_reg_out(dev, REG_WR_MBUS2_ADDR, 0x000000e0);
        res = res ? res : usb_uram_reg_out(dev, REG_WR_MBUS2_DATA, 0xffffffff);
    } else {
        USDR_LOG(USBG_LOG_TAG, USDR_LOG_WARNING, "Omit interrupt initialization on USB+PCIE mode\n");
    }

    // Device initialization
    res = dev->pdev->initialize(dev->pdev, pcount, devparam, devval);
    if (res) {
        USDR_LOG(USBG_LOG_TAG, USDR_LOG_ERROR,
                 "Unable to initialize device, error %d\n", res);
        return res;
    }

    return 0;
}
