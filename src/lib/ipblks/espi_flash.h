// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef ESPI_FLASH_H
#define ESPI_FLASH_H

#include <usdr_port.h>
#include <usdr_lowlevel.h>

enum espi_write_flags {
    ESPI_FLASH_DONT_WRITE_HEADER = 1,
    ESPI_FLASH_DONT_ERASE = 2,
};

int espi_flash_get_id(lldev_t dev, subdev_t subdev, unsigned cfg_base,
                      uint32_t *flash_id, char *outid, size_t maxstr);

int espi_flash_read(lldev_t dev, subdev_t subdev, unsigned cfg_base, unsigned cfg_mmap_base,
                    uint32_t flash_off, uint32_t size, uint8_t* out);

int espi_flash_write(lldev_t dev, subdev_t subdev, unsigned cfg_base, unsigned cfg_mmap_base,
                     const uint8_t *in, uint32_t size, uint32_t flash_off, unsigned flags);

int espi_flash_erase(lldev_t dev, subdev_t subdev, unsigned cfg_base,
                     uint32_t size, uint32_t flash_off);

#endif
