// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "espi_flash.h"
#include <usdr_logging.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

// Check data sanity before being written
#define DEBUG_CHECK_RAM

enum jedec_cmds {
    ESPI_CMD_QCFR_0 = 0x0B,
    ESPI_CMD_WREN = 0x06,

    ESPI_CMD_QCPP_0 = 0x02,
    ESPI_CMD_SSE = 0x20,
    ESPI_CMD_SE = 0xD8,
    ESPI_CMD_BE = 0xC7,
    ESPI_CMD_RDSR = 0x05,

    ESPI_CMD_RDID_0 = 0x9E,
    ESPI_CMD_RDID_1 = 0x9F,
};

#define MAKE_ESPI_CORE_CMD(cmd, sz, la16, lbe, ab, wb) \
    ((((uint32_t)cmd) << 24) | (((uint32_t)sz) << 16) | \
    (((la16) & 0xfff) << 4) | ((lbe & 1) << 3) | ((ab & 3) << 1) | ((wb & 1) << 0))

enum espi_core_regs {
    ESPI_STAT = 0,
    ESPI_DATA = 1,

    ESPI_CMD = 0,
    ESPI_FADDR = 1,
};

#define LOCAL_BLK_LEN 128
#define LOCAL_MEM_TOP 0
#define WATCHDOG_LIMIT 300000
#define RDSR_BUSY_BIT (1)


static int _espi_flash_wait_done(lldev_t dev, subdev_t subdev, unsigned cfg_base)
{
	unsigned limit = 0;
    uint32_t stat;
	do {
        int res = lowlevel_reg_rd32(dev, subdev, cfg_base + ESPI_STAT, &stat);
		if (res)
			return res;

		if (limit > 1000000)
			return -ETIMEDOUT;

		usleep(1);
		limit++;
    } while ((stat & 1) != 0);

	return 0;
}


static int _espi_flash_read_reg(lldev_t dev, subdev_t subdev, unsigned cfg_base,
                          uint8_t reg, uint8_t sz, uint32_t* out)
{
    int res;
    res = lowlevel_reg_wr32(dev, subdev, cfg_base + ESPI_CMD,
                            MAKE_ESPI_CORE_CMD(reg, sz, 0, 0, 0, 0));
    res = (res) ? res : _espi_flash_wait_done(dev, subdev, cfg_base);
    res = (res) ? res : lowlevel_reg_rd32(dev, subdev, cfg_base + ESPI_DATA, out);
    return res;
}


static int _espi_flash_cmd_rdsr(lldev_t dev, subdev_t subdev, unsigned cfg_base, uint8_t* out)
{
    uint32_t v;
    int res = _espi_flash_read_reg(dev, subdev, cfg_base, ESPI_CMD_RDSR, /*1*/ 4, &v);
    if (res)
        return res;

    *out = (uint8_t)v;
    return 0;
}

#define JEDEC_MACRONIX        0xC2
#define JEDEC_ADESTO          0x1F
#define JEDEC_MICRON          0x20

#define MICRON_SERIAL_NOR     0xBA
#define MICRON_SERIAL_NOR_18  0xBB

#define ADESTO_SPI_QPI        0x42

static int _espi_flash_id_to_str(uint32_t flash_id,
                                 char* outid, size_t maxstr)
{
    uint8_t manufacture_id = flash_id & 0xff;
    uint8_t flash_type = ((flash_id >> 8) & 0xff);
    uint8_t flash_size = ((flash_id >> 16) & 0xff);
    uint32_t cap = 1U << flash_size;

    if ((manufacture_id == JEDEC_MICRON) && (flash_type == MICRON_SERIAL_NOR || flash_type == MICRON_SERIAL_NOR_18)) {
            snprintf(outid, maxstr, "Micron Serial NOR MT25Q %d Mb (%s)",
                     8 * cap / 1024 / 1024,
                     (flash_type == MICRON_SERIAL_NOR) ? "3.3V" : "1.8V");
    } else if (manufacture_id == JEDEC_MACRONIX) {
        snprintf(outid, maxstr, "Macronix MX%02x series %d Mb",
                 flash_type, 8 * cap / 1024 / 1024);
    } else if (manufacture_id == JEDEC_ADESTO && flash_type == ADESTO_SPI_QPI) {
        snprintf(outid, maxstr, "Adesto SPI/QPI series %d Mb",
                 /*flash_type,*/ 8 * cap / 1024 / 1024);
    } else {
        snprintf(outid, maxstr, "Mfg 0x%02x Type 0x%02x %d Mb",
                 manufacture_id, flash_type, 8 * cap / 1024 / 1024);
        return -ENODATA;
    }
    return 0;
}


int espi_flash_get_id(lldev_t dev, subdev_t subdev, unsigned cfg_base, uint32_t *flash_id, char* outid, size_t maxstr)
{
    outid[0] = 0;

    int res = _espi_flash_read_reg(dev, subdev, cfg_base, ESPI_CMD_RDID_1, 4, flash_id);
    if (res || !outid)
        return res;

    return _espi_flash_id_to_str(*flash_id, outid, maxstr);
}


static int _espi_flash_read_to_local(lldev_t dev, subdev_t subdev, unsigned cfg_base, uint8_t cmd, uint8_t sz, uint32_t flash_addr)
{
	int res;
    res = lowlevel_reg_wr32(dev, subdev, cfg_base + ESPI_FADDR, flash_addr);
	if (res)
		return res;

    res = lowlevel_reg_wr32(dev, subdev, cfg_base + ESPI_CMD,
                            MAKE_ESPI_CORE_CMD(cmd, sz, (LOCAL_MEM_TOP >> 4), 1, 2, 0));
	if (res)
		return res;

    res = _espi_flash_wait_done(dev, subdev, cfg_base);
	if (res)
		return res;
	return res;
}


int espi_flash_read(lldev_t dev, subdev_t subdev, unsigned cfg_base,
                    unsigned cfg_mmap_base, uint32_t flash_off, uint32_t size, uint8_t *out)
{
    int res = -EINVAL;
    uint32_t addr = flash_off;
    while (size > 0) {
        uint32_t bsz = (size > LOCAL_BLK_LEN) ? LOCAL_BLK_LEN : size;
        uint32_t iosz = (((bsz + 3) / 4)) * 4;

        res = _espi_flash_read_to_local(dev, subdev, cfg_base, ESPI_CMD_QCFR_0, bsz, addr);
        USDR_LOG("FLSH", USDR_LOG_NOTE, "Flash read: addr=%u size=%d res=%d\n",
                 addr, bsz, res);
        if (res)
            return res;

        res = lowlevel_get_ops(dev)->ls_op(dev, subdev, USDR_LSOP_HWREG, cfg_mmap_base,
                                           iosz, out, 0, NULL);
        if (res)
            return res;

        out += bsz;
        size -= bsz;
        addr += bsz;
    }

    return 0;
}


static int _espi_flash_cmd_wren(lldev_t dev, subdev_t subdev, unsigned cfg_base)
{
    return lowlevel_reg_wr32(dev, subdev, cfg_base + ESPI_CMD,
                             MAKE_ESPI_CORE_CMD(ESPI_CMD_WREN, 0, 0, 0, 0, 0));
}


static int _espi_flash_bulk_erase(lldev_t dev, subdev_t subdev, unsigned cfg_base)
{
    return lowlevel_reg_wr32(dev, subdev, cfg_base + ESPI_CMD,
                             MAKE_ESPI_CORE_CMD(ESPI_CMD_BE, 0, 0, 0, 0, 0));
}


static int _espi_flash_subsector_erase(lldev_t dev, subdev_t subdev, unsigned cfg_base, uint32_t addr)
{
    int res = lowlevel_reg_wr32(dev, subdev, cfg_base + ESPI_FADDR, addr);
    res = (res) ? res : lowlevel_reg_wr32(dev, subdev, cfg_base + ESPI_CMD,
                                          MAKE_ESPI_CORE_CMD(ESPI_CMD_SSE, 0, 0, 0, 2, 0));
    return res;
}


static int _espi_flash_sector_erase(lldev_t dev, subdev_t subdev, unsigned cfg_base, uint32_t addr)
{
    int res = lowlevel_reg_wr32(dev, subdev, cfg_base + ESPI_FADDR, addr);
    res = (res) ? res :  lowlevel_reg_wr32(dev, subdev, cfg_base + ESPI_CMD,
                                           MAKE_ESPI_CORE_CMD(ESPI_CMD_SE, 0, 0, 0, 2, 0));
    return res;
}


static int _espi_flash_wait_ready(lldev_t dev, subdev_t subdev, unsigned cfg_base)
{
	int res;
	uint8_t status;
	unsigned limit = 0;

	do {
        res = _espi_flash_cmd_rdsr(dev, subdev, cfg_base, &status);
		if (res)
			return res;

		if ((status & RDSR_BUSY_BIT) != RDSR_BUSY_BIT)
			break;

		usleep(1);
		limit++;
	} while (limit < WATCHDOG_LIMIT);

	if (limit == WATCHDOG_LIMIT)
		return -ETIMEDOUT;
	return 0;
}


static int _espi_flash_erase(lldev_t dev, subdev_t subdev, unsigned cfg_base, uint32_t addr, uint32_t size)
{
    const unsigned subsector_msk = 0x0fff; // 4KiB
    const unsigned sector_msk    = 0xffff; // 64KiB
	int res;

    if (size <= subsector_msk)
		return -EINVAL;

	do {
		if (addr == 0 && size == ~((uint32_t)0)) {
            USDR_LOG("FLSH", USDR_LOG_NOTE, "Full flash erase!\n");
            res = _espi_flash_cmd_wren(dev, subdev, cfg_base);
			if (res)
				return res;

            res = _espi_flash_bulk_erase(dev, subdev, cfg_base);
			if (res)
				return res;

			size = 0;
        } else if (((addr & sector_msk) == 0) && (size > sector_msk)) {
            res = _espi_flash_cmd_wren(dev, subdev, cfg_base);
			if (res)
				return res;

			//Sector ERASE
            USDR_LOG("FLSH", USDR_LOG_NOTE, "Sector erase %02x\n", addr);
            res = _espi_flash_sector_erase(dev, subdev, cfg_base, addr);
			if (res)
				return res;

            addr += sector_msk + 1;
            size -= sector_msk + 1;

            usleep(10000);
        } else if (((addr & subsector_msk) == 0) && (size > subsector_msk)) {
            res = _espi_flash_cmd_wren(dev, subdev, cfg_base);
			if (res)
				return res;

			//Sub sector ERASE
            USDR_LOG("FLSH", USDR_LOG_NOTE, "SubSector erase %02x\n", addr);
            res = _espi_flash_subsector_erase(dev, subdev, cfg_base, addr);
			if (res)
				return res;

            addr += subsector_msk + 1;
            size -= subsector_msk + 1;

            usleep(1000);
		} else {
			// Granularity less than subsector
			return -EINVAL;
		}

        res = _espi_flash_wait_ready(dev, subdev, cfg_base);
		if (res)
			return res;

	} while (size != 0);

	return 0;
}


static int _espi_flash_write(lldev_t dev, subdev_t subdev, unsigned cfg_base, uint8_t cmd, uint8_t sz,
                       uint32_t flash_addr)
{
    int res = _espi_flash_cmd_wren(dev, subdev, cfg_base);
    res = (res) ? res : lowlevel_reg_wr32(dev, subdev, cfg_base + ESPI_FADDR, flash_addr);
    res = (res) ? res : lowlevel_reg_wr32(dev, subdev, cfg_base + ESPI_CMD,
                            MAKE_ESPI_CORE_CMD(cmd, sz, (LOCAL_MEM_TOP >> 4), 1, 2, 1));
	if (res)
		return res;
	usleep(1000);

    res = (res) ? res : _espi_flash_wait_done(dev, subdev, cfg_base);
    res = (res) ? res : _espi_flash_wait_ready(dev, subdev, cfg_base);
	return res;
}


int espi_flash_erase(lldev_t dev, subdev_t subdev, unsigned cfg_base,
                     uint32_t size, uint32_t flash_off)
{
   return _espi_flash_erase(dev, subdev, cfg_base, flash_off, size);
}


int espi_flash_write(lldev_t dev, subdev_t subdev, unsigned cfg_base, unsigned cfg_mmap_base,
                     const uint8_t* in, uint32_t size, uint32_t flash_off, unsigned flags)
{
    int res;
    if ((flags & ESPI_FLASH_DONT_ERASE) != ESPI_FLASH_DONT_ERASE) {
        res = _espi_flash_erase(dev, subdev, cfg_base, flash_off, size);
        if (res)
            return res;
    }

    if ((flags & ESPI_FLASH_DONT_WRITE_HEADER) == ESPI_FLASH_DONT_WRITE_HEADER) {
        size -= 256;
        flash_off += 256;
        in += 256;
    }

    uint32_t addr = flash_off;
    while (size > 0) {
        uint32_t bsz = (size > LOCAL_BLK_LEN) ? LOCAL_BLK_LEN : size;
        uint32_t iosz = (((bsz + 3) / 4)) * 4;

        res = lowlevel_get_ops(dev)->ls_op(dev, subdev, USDR_LSOP_HWREG, cfg_mmap_base,
                                           0, NULL, iosz, in);
        if (res)
            return res;
#ifdef DEBUG_CHECK_RAM
        uint32_t tmp[64];
        res = lowlevel_get_ops(dev)->ls_op(dev, subdev, USDR_LSOP_HWREG, cfg_mmap_base,
                                           iosz, tmp, 0, NULL);
        if (res)
            return res;

        res = memcmp(tmp, in, iosz);
        assert (res == 0);
#endif

        res = _espi_flash_write(dev, subdev, cfg_base, ESPI_CMD_QCPP_0, bsz, addr);
        USDR_LOG("FLSH", USDR_LOG_NOTE, "Flash write: addr=%u sizez=%d res=%d\n",
                    addr, bsz, res);
		if (res)
			return res;

		in += bsz;
		size -= bsz;
		addr += bsz;
	}

	return res;
}


