// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <usdr_lowlevel.h>
#include <usdr_logging.h>

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "../lib/ipblks/espi_flash.h"
#include "../lib/ipblks/xlnx_bitstream.h"

#include "../lib/device/device.h"

enum {
    M2PCI_REG_STAT_CTRL = 0,
};

static char outa[16*1024*1024];
static char outb[16*1024*1024];


enum {
    MASTER_IMAGE_OFF = 0x001c0000,
};

static int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, 0, 16 + (bank / 4), data);
}

enum flash_action {
    ACTION_NONE,
    ACTION_READBACK,
    ACTION_WRITE,
    ACTION_INFO,
};

int main(int argc, char** argv)
{
    int res, opt;
    enum flash_action rdwr = ACTION_NONE;
    const char* filename = NULL;
    const char* busname = NULL;
    lldev_t dev;
    bool force = false;
    bool golden = false;
    bool corrupt = false;
    bool verbose = false;
    uint32_t curfwid;
    bool no_device = false;
    uint64_t master_offset = MASTER_IMAGE_OFF;
    uint64_t qspi_base = 10;

    memset(outa, 0xff, SIZEOF_ARRAY(outa));
    memset(outb, 0xff, SIZEOF_ARRAY(outb));

    usdrlog_setlevel(NULL, USDR_LOG_WARNING);
    usdrlog_enablecolorize(NULL);

    while ((opt = getopt(argc, argv, "U:l:i:w:r:FGCv")) != -1) {
        switch (opt) {
        case 'U':
            busname = optarg;
            break;
        case 'l':
            usdrlog_setlevel(NULL, atoi(optarg));
            break;
        case 'r':
            filename = optarg;
            rdwr = ACTION_READBACK;
            break;
        case 'w':
            filename = optarg;
            rdwr = ACTION_WRITE;
            break;
        case 'i':
            filename = optarg;
            rdwr = ACTION_INFO;
            break;
        case 'F':
            force = true;
            break;
        case 'G':
            golden = true;
            break;
        case 'C':
            corrupt = true;
            break;
        case 'v':
            verbose = true;
            break;
        default:
            fprintf(stderr, "Usage: %s [-U device_bus] [-l loglevel] [-r filename | -w filename | -i filename] [-G]\n",
                    argv[0]);
            return 1;
        }
    }

    const char* pnames[] = {
        "bus"
    };
    const char* pvalue[] = {
        busname
    };

    xlnx_image_params_t file;
    xlnx_image_params_t image;
    xlnx_image_params_t image_master;
    bool mp = false;

    memset(&image, 0, sizeof(image));
    memset(&image_master, 0, sizeof(image_master));

    res = lowlevel_create((busname == NULL) ? 0 : 1, pnames, pvalue, &dev, 0, NULL, 0);
    if (res) {
        fprintf(stderr, "Unable to create: errno %d\n", res);
        if (rdwr != ACTION_INFO)
            return 1;
        no_device = true;
    }

    const char* name = (no_device) ? "<no_device>" : lowlevel_get_devname(dev);
    if (!no_device) {
        fprintf(stderr, "Device was created: `%s`!\n", name);
    }

    res = (no_device) ? 0 : dev_gpi_get32(dev, 0, &curfwid);
    if (res) {
        fprintf(stderr, "Unable to get FIRMWARE_ID: errno %d\n", res);
        return 1;
    }

    if (!(no_device)) {
        res = res ? res : usdr_device_vfs_obj_val_get_u64(dev->pdev, "/ll/qspi_flash/master_off", &master_offset);
        res = res ? res : usdr_device_vfs_obj_val_get_u64(dev->pdev, "/ll/qspi_flash/base", &qspi_base);

        if (res) {
            fprintf(stderr, "Unable to get board memory configuration, assuming MASTER_OFF=%x!\n", (unsigned)master_offset);
        }
    }

    usleep(1000);

    uint32_t fid = 0xdeadbeef;
    char fid_str[64] = {0};

    res = (no_device) ? 0 : espi_flash_get_id(dev, 0, qspi_base, &fid, fid_str, sizeof(fid_str));
    if (res) {
        fprintf(stderr, "Failed to get flash ID (%s)!\n", fid_str);
        return 2;
    }
    if (!no_device) {
        fprintf(stderr, "Flash ID id %08x (%s)!\n", fid, fid_str);
    }

    //Check image
    res = (no_device) ? 0 : espi_flash_read(dev, 0, qspi_base, 512, 0, 256, outb);
    if (res) {
        fprintf(stderr, "Failed to read current golden image header! res=%d\n", res);
        return 4;
    }
    res = (no_device) ? 0 : espi_flash_read(dev, 0, qspi_base, 512, master_offset, 256, outb + 256);
    if (res) {
        fprintf(stderr, "Failed to read current master image header! res=%d\n", res);
        return 4;
    }

    res = (no_device) ? 0 : xlnx_btstrm_parse_header((const uint32_t* )outb, 256/4, &image);
    if (res) {
        fprintf(stderr, "It looks like the FPGA G image is corrupted! res=%d\n", res);
        return 4;
    }
    res = (no_device) ? 0 : xlnx_btstrm_parse_header((const uint32_t* )(outb + 256), 256/4, &image_master);
    if (res) {
        fprintf(stderr, "It looks like the FPGA M image is corrupted! res=%d\n", res);
    } else {
        mp = true;
    }

    if (!no_device) {
        fprintf(stderr, "Actual firmware in use:      FirmwareID %08x (%lld)\n",
                curfwid, (long long)get_xilinx_rev_h(curfwid));
        fprintf(stderr, "Golden image: DEVID %08x FirmwareID %08x (%lld)\n",
                image.devid, image.usr_access2, (long long)get_xilinx_rev_h(image.usr_access2));
        fprintf(stderr, "Master image: DEVID %08x FirmwareID %08x (%lld)\n",
                image_master.devid, image_master.usr_access2, (long long)get_xilinx_rev_h(image_master.usr_access2));
    }

    uint32_t off = (golden) ? 0 : master_offset;
    unsigned total_length = SIZEOF_ARRAY(outa);
    if (rdwr == ACTION_WRITE || rdwr == ACTION_INFO) {
        FILE* w = fopen(filename, "rb");
        if (w == NULL) {
            fprintf(stderr, "Unabe to read file '%s': %s\n", filename, strerror(errno));
            return 3;
        }
        res = fseek(w, 0, SEEK_END);
        if (res) {
            fprintf(stderr, "Unabe to seek file '%s': %s\n", filename, strerror(errno));
            return 3;
        }
        total_length = ftell(w);
        res = fseek(w, 0, SEEK_SET);
        if (res) {
            fprintf(stderr, "Unabe to seek file '%s': %s\n", filename, strerror(errno));
            return 3;
        }
        res = fread(outa, 1, total_length, w);
        if ((unsigned)res != total_length) {
            fprintf(stderr, "Unabe to read file '%s': %d read\n", filename, res);
            return 3;
        }
        fclose(w);

        res = xlnx_btstrm_parse_header((const uint32_t* )outa, 256/4, &file);
        if (res) {
            fprintf(stderr, "It looks like the file is corrupted! res=%d\n", res);
            return 4;
        }
        if (verbose) {
            fprintf(stderr, "- GOLDEN: WBSTART=%08x IPROG=%d\n", image.wbstar, image.iprog);
            fprintf(stderr, "- MASTER: WBSTART=%08x IPROG=%d\n", image_master.wbstar, image_master.iprog);
            fprintf(stderr, "- FILE:   WBSTART=%08x IPROG=%d\n", file.wbstar, file.iprog);
            fprintf(stderr, "- DEVICE: OFFSET= %08x\n", (unsigned)master_offset);
        }

        res = (no_device) ? 0 : xlnx_btstrm_iprgcheck(&image, &file, master_offset, golden);
        if (res) {
            fprintf(stderr, "Image check failed! res=%d, file revision=%12ld\n", res, get_xilinx_rev_h(file.usr_access2));
            return 4;
        }

        //round up to 64k sector
        if (total_length & 0xffff) {
            total_length += 65536;
            total_length &= 0xffff0000;
        }

        fprintf(stderr, "File image:   DEVID %08x FirmwareID %08x (%lld)\n",
                file.devid, file.usr_access2, (long long)get_xilinx_rev_h(file.usr_access2));

        if (rdwr == ACTION_INFO) {
            return 0;
        }
        if (golden) {
            fprintf(stderr, "DANGER: You're updating the golden image!\n");
        }
        fprintf(stderr, "Writing %d bytes at %08x\n", total_length, off);

        if (file.usr_access2 == curfwid && image_master.usr_access2 == file.usr_access2 && !force) {
            fprintf(stderr, "Looks like you're using latest firmware already\n");
            return 9;
        }
        if (image.usr_access2 == file.usr_access2 && golden && !force) {
            fprintf(stderr, "Looks like GOLD image %08x is already flashed!\n", file.usr_access2);
            return 9;
        }
        if (image_master.usr_access2 == file.usr_access2 && !golden && !force) {
            fprintf(stderr, "Looks like MASTER image %08x is already flashed!\n", file.usr_access2);
            return 9;
        }

        if (corrupt) {
            memset(outa + 512*1024, -1, 512*1024);
            fprintf(stderr, "CORRUPTING IMAGE!!!\n\n");
        }

        if (golden) {
            fprintf(stderr, "Writing GOLDEN header\n");
            res = espi_flash_write(dev, 0, qspi_base, 512, outa, 4096, off, 0);
            if (res) {
                fprintf(stderr, "Failed to write header! res=%d", res);
                return 4;
            }
            fprintf(stderr, "Writing GOLDEN body\n");
            res = espi_flash_write(dev, 0, qspi_base, 512,
                                             outa + 4096,
                                             total_length - 4096,
                                             off + 4096, 0);
            if (res) {
                fprintf(stderr, "Failed to write! res=%d", res);
                return 4;
            }
        } else {
            res = espi_flash_write(dev, 0, qspi_base, 512, outa, total_length, off,
                                             ESPI_FLASH_DONT_WRITE_HEADER);
            if (res) {
                fprintf(stderr, "Failed to write! res=%d", res);
                return 4;
            }

            res = espi_flash_write(dev, 0, qspi_base, 512, outa, 256, off,
                                             ESPI_FLASH_DONT_ERASE);
            if (res) {
                fprintf(stderr, "Failed to write header! res=%d", res);
                return 4;
            }
        }
    }

    if (rdwr == ACTION_WRITE || rdwr == ACTION_READBACK) {
        fprintf(stderr, "Reading %d bytes!\n", total_length);
        res = espi_flash_read(dev, 0, qspi_base, 512, off, total_length, outb);
        if (res) {
            fprintf(stderr, "Failed to readback! res=%d", res);
            return 4;
        }
    }

    if (rdwr == ACTION_WRITE) {
        int errors = 0;
        for (unsigned off = 0; off < total_length; off += 256) {
            unsigned rem = total_length - off;
            if (rem > 256)
                rem = 256;
            res = memcmp(outa + off, outb + off, rem);
            if (res) {
                fprintf(stderr, "readback data != write data; off = %08x\n", off);
                errors++;
            }
        }
        if (errors == 0) {
            fprintf(stderr, "Write successful!\n");
        } else {
            fprintf(stderr, "Write FAILED; errors: %d!\n", errors);
        }
    } else if (rdwr == ACTION_READBACK) {
        FILE* w = fopen(filename, "wb");
        if (w == NULL) {
            fprintf(stderr, "Unabe to create file '%s': %s\n", filename, strerror(errno));
            return 3;
        }
        fwrite(outb, 1, total_length, w);
        fclose(w);
    }


    lowlevel_destroy(dev);
    return 0;
}
