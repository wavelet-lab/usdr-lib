// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <string.h>
#include "base64.h"

#include "controller.h"

#include "../ipblks/espi_flash.h"
#include "../ipblks/xlnx_bitstream.h"
#include "../device/m2_lm7_1/xsdr_ctrl.h"

// TODO: get rid of this foo
xsdr_dev_t* get_xsdr_dev(pdevice_t udev);

static int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, 0, 16 + (bank / 4), data);
}

static unsigned s_op_flash_length = 0;
static unsigned s_op_flash_offset = 0;
static unsigned s_op_flash_golden = 0;
static unsigned s_op_flash_validated = 0;
static uint8_t  s_op_flash_header[256];

static int check_firmware_header(lldev_t dev, const char* data, bool update_golden)
{
    int res;
    uint32_t rev;
    bool mp;
    xlnx_image_params_t file;
    xlnx_image_params_t image;
    xlnx_image_params_t image_master;
    uint32_t image_master_data[256/4];
    uint32_t image_golden_data[256/4];

    res = espi_flash_read(dev, 0, 10, 512, 0, 256, (uint8_t* )image_golden_data);
    if (res) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "Failed to read current golden image header! res=%d\n", res);
        return -EINVAL;
    }
    res = espi_flash_read(dev, 0, 10, 512, MASTER_IMAGE_OFF, 256, (uint8_t* )image_master_data);
    if (res) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "Failed to read current master image header! res=%d\n", res);
        return -EINVAL;
    }
    res = xlnx_btstrm_parse_header(image_golden_data, 256/4, &image);
    if (res) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "It looks like the FPGA G image is corrupted! res=%d\n", res);
        return -EINVAL;
    }
    res = xlnx_btstrm_parse_header(image_master_data, 256/4, &image_master);
    if (res) {
        USDR_LOG("DSTR", USDR_LOG_WARNING, "It looks like the FPGA M image is corrupted! res=%d\n", res);
    } else {
        mp = true;
    }
    res = xlnx_btstrm_parse_header((const uint32_t *)data, 256/4, &file);
    if (res) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "It looks like the file is corrupted! res=%d\n", res);
        return -EINVAL;
    }
    res = xlnx_btstrm_iprgcheck(&image, &file, MASTER_IMAGE_OFF, update_golden);
    if (res) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "Image check failed! res=%d\n", res);
        return -EINVAL;
    }
    res = dev_gpi_get32(dev, 0, &rev);
    if (res)
        return res;

    USDR_LOG("DSTR", USDR_LOG_INFO, "Writing %d bytes; old(G/M) = %08x/%08x [%d] new = %08x current = %08x!\n",
            s_op_flash_length, image.usr_access2, image_master.usr_access2, mp, file.usr_access2, rev);

    return 0;
}

int xsdr_call(pdm_dev_t dmdev, const struct sdr_call *pcall,
              unsigned outbufsz, char *outbuffer, const char *inbuffer)
{
    int res;
    lldev_t lldev = dmdev->lldev;

    switch (pcall->call_type) {
    case SDR_FLASH_READ: {
        char buf[256];
        int res, k;
        unsigned offset = (pcall->params.parameters_type[SDRC_OFFSET] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_OFFSET] : 0;
        unsigned param = (pcall->params.parameters_type[SDRC_PARAM] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_PARAM] : 0;
        bool read_golden = (param == 1337);
        uint32_t base = (read_golden) ? 0 : MASTER_IMAGE_OFF;

        res = espi_flash_read(lldev, 0, M2PCI_REG_QSPI_FLASH,
                              512, base + offset, 256, (uint8_t*)buf);
        if (res) {
            snprintf(outbuffer, outbufsz, "{\"result\":%d}", res);
            return 0;
        }
        k = snprintf(outbuffer, outbufsz,
                     "{\"result\":0,\"details\":{\"data-length\":256,\"data\":\"");

        // TODO check size
        k += base64_encode(buf, 256, outbuffer + k);
        snprintf(outbuffer + k, outbufsz - k, "\"}}");
        return 0;
    }
    case SDR_FLASH_WRITE_SECTOR: {
        char buf[256 + 3];
        int res, k;
        unsigned offset = (pcall->params.parameters_type[SDRC_OFFSET] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_OFFSET] : 0;
        unsigned checksum = (pcall->params.parameters_type[SDRC_CHECKSUM] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_CHECKSUM] : 0;
        unsigned param = (pcall->params.parameters_type[SDRC_PARAM] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_PARAM] : 0;

        if (pcall->call_data_ptr == 0)
            return -EINVAL;
        if (pcall->call_data_size != 344)
            return -EINVAL;

        res = base64_decode(inbuffer + pcall->call_data_ptr, pcall->call_data_size, buf);
        if (res != 256) {
            USDR_LOG("DSTR", USDR_LOG_ERROR, "Flash sector incorrect length %d\n", res);
            return -EINVAL;
        }

        unsigned vcheck = 0;
        for (k = 0; k < 256; k++) {
            vcheck += ((unsigned char*)buf)[k];
        }

        if (vcheck != checksum) {
            USDR_LOG("DSTR", USDR_LOG_ERROR, "Incorrect checksum; calculated %d != %d provided\n",
                     vcheck, vcheck);
            return -EINVAL;
        }

        if (offset >= s_op_flash_offset + s_op_flash_length) {
            USDR_LOG("DSTR", USDR_LOG_ERROR, "Incorrect state\n");
            return -EINVAL;
        }

        if (s_op_flash_offset == offset) {
            bool update_golden = (param == 1337);

            if (s_op_flash_length == 0)
                return -EINVAL;

            res = check_firmware_header(dmdev->lldev, buf, update_golden);
            if (update_golden) {
                // For golden image write boot sector with trampoline first, then erase the whole flash
                res = (res) ? res : espi_flash_erase(lldev, 0, M2PCI_REG_QSPI_FLASH, 4096, 0);
                res = (res) ? res : espi_flash_write(lldev, 0, M2PCI_REG_QSPI_FLASH, 512,
                                                     (const uint8_t*)buf, 256, 0, ESPI_FLASH_DONT_ERASE);
                res = (res) ? res : espi_flash_erase(lldev, 0, M2PCI_REG_QSPI_FLASH,
                                                     (s_op_flash_length - 4096 + 4095) & 0xfffff000,
                                                     0 + 4096);
            } else {
                res = (res) ? res : espi_flash_erase(lldev, 0, M2PCI_REG_QSPI_FLASH,
                                                     (s_op_flash_length + 4095) & 0xfffff000,
                                                     MASTER_IMAGE_OFF);
            }
            if (res) {
                s_op_flash_offset = 0;
                s_op_flash_length = 0;
                return res;
            }

            USDR_LOG("DSTR", USDR_LOG_INFO, "Flash blanked\n");
            s_op_flash_golden = update_golden;
            s_op_flash_validated = 1;

            memcpy(s_op_flash_header, buf, 256);
        } if (!s_op_flash_validated) {
            return -ENOEXEC;
        }

        if (offset + 256 >= s_op_flash_offset + s_op_flash_length) {
            USDR_LOG("DSTR", USDR_LOG_INFO, "Flash write reached end!\n");
            s_op_flash_offset = s_op_flash_length = 0;
        }

        uint32_t base = (s_op_flash_golden) ? 0 : MASTER_IMAGE_OFF;
        if (offset != 0) {
            res = espi_flash_write(lldev, 0, M2PCI_REG_QSPI_FLASH, 512,
                                   (const uint8_t*)buf, 256, offset + base, ESPI_FLASH_DONT_ERASE);
            if (res)
                return res;
        }

        if (!s_op_flash_golden && s_op_flash_length == 0 && s_op_flash_offset == 0) {
            //Write header of non-golden image
            USDR_LOG("DSTR", USDR_LOG_INFO, "Flash write header of non-golden!\n");

            res = espi_flash_write(lldev, 0, M2PCI_REG_QSPI_FLASH, 512,
                                   s_op_flash_header, 256, 0 + base, ESPI_FLASH_DONT_ERASE);
            if (res)
                return res;
        }

        snprintf(outbuffer, outbufsz, "{\"result\":0}");
        return 0;
    }
    case SDR_FLASH_ERASE: {
        unsigned offset = (pcall->params.parameters_type[SDRC_OFFSET] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_OFFSET] : 0;
        unsigned length = (pcall->params.parameters_type[SDRC_LENGTH] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_LENGTH] : 0;

        s_op_flash_length = 0;
        s_op_flash_offset = 0;
        s_op_flash_golden = 0;
        s_op_flash_validated = 0;

        if (offset + length > 2 * 1024 * 1024)
            return -EINVAL;
        if (length % 256)
            return -EINVAL;
        if (length == 0)
            return -EINVAL;

        s_op_flash_length = length;
        s_op_flash_offset = offset;

        USDR_LOG("DSTR", USDR_LOG_INFO, "Flash erase commited from %d to %d\n",
                 s_op_flash_offset, s_op_flash_length);
        snprintf(outbuffer, outbufsz, "{\"result\":0}");
        return 0;
    }
    case SDR_CALIBRATE: {
        unsigned chans = (pcall->params.parameters_type[SDRC_CHANS] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_CHANS] : 0;
        unsigned param = (pcall->params.parameters_type[SDRC_PARAM] == SDRC_PARAM_TYPE_INT) ?
                    pcall->params.parameters_uint[SDRC_PARAM] : 0;

        res = xsdr_calibrate(get_xsdr_dev(lldev->pdev), chans, param, NULL);
        if (res)
            return res;

        snprintf(outbuffer, outbufsz, "{\"result\":0}");
        return 0;
    }
    default:
        break;
    }
    return -EINVAL;
}
