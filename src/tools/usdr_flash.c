// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <usdr_lowlevel.h>
#include <usdr_logging.h>

#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

#include "../lib/ipblks/espi_flash.h"
#include "../lib/ipblks/xlnx_bitstream.h"

#include "../lib/device/device.h"
#include "cli_parse.h"

enum {
    M2PCI_REG_STAT_CTRL = 0,
};

static char outa[16*1024*1024];
static char outb[16*1024*1024];
static lldev_t device_to_destroy;

static void destroy_device(void)
{
    if (device_to_destroy != NULL) {
        lowlevel_destroy(device_to_destroy);
        device_to_destroy = NULL;
    }
}

static bool flash_region_is_erased(const void* data, size_t size)
{
    const unsigned char* bytes = data;

    for (size_t i = 0; i < size; i++) {
        if (bytes[i] != 0xff)
            return false;
    }

    return true;
}

#define FLASH_CLI_OPTIONS(X) \
    X(OPT_DEVICE, "device", 'U', required_argument, "DEVICE", "Device bus or connection string") \
    X(OPT_LOG_LEVEL, "log-level", 'l', required_argument, "LEVEL", "Logging level") \
    X(OPT_INFO, "info", 'i', required_argument, "FILE", "Inspect and validate an image file") \
    X(OPT_WRITE, "write", 'w', required_argument, "FILE", "Write an image to flash") \
    X(OPT_READ, "read", 'r', required_argument, "FILE", "Read flash contents into a file") \
    X(OPT_FORCE, "force", 'F', no_argument, NULL, "Write even if the same firmware is already present") \
    X(OPT_GOLDEN, "golden", 'G', no_argument, NULL, "Operate on the golden image partition") \
    X(OPT_CORRUPT, "corrupt", 'C', no_argument, NULL, "Intentionally corrupt the image before writing") \
    X(OPT_VERBOSE, "verbose", 'v', no_argument, NULL, "Print detailed image information") \
    X(OPT_SKIP_CRC, "skip-crc", 'k', no_argument, NULL, "Skip full image CRC validation") \
    X(OPT_ERASE_MASTER, "erase-master", 'E', no_argument, NULL, "Erase the master image header") \
    X(OPT_READBACK_SIZE, "readback-size", 'S', required_argument, "BYTES", "Number of bytes to read back") \
    X(OPT_HELP, "help", 'h', no_argument, NULL, "Show this help and exit")

CLI_DEFINE_OPTIONS(flash, FLASH_CLI_OPTIONS)

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
    ACTION_ERASE_MASTER,
};

int main(int argc, char** argv)
{
    int res, opt;
    enum flash_action rdwr = ACTION_NONE;
    const char* filename = NULL;
    const char* busname = NULL;
    lldev_t dev = NULL;
    bool force = false;
    bool golden = false;
    bool corrupt = false;
    bool verbose = false;
    uint32_t curfwid;
    bool no_device = false;
    bool crc_check = true;
    uint64_t master_offset = MASTER_IMAGE_OFF;
    uint64_t qspi_base = 10;
    unsigned readback_size = 0;

    if (atexit(destroy_device) != 0) {
        fprintf(stderr, "Unable to register device cleanup\n");
        return 1;
    }

    memset(outa, 0xff, SIZEOF_ARRAY(outa));
    memset(outb, 0xff, SIZEOF_ARRAY(outb));

    usdrlog_setlevel(NULL, USDR_LOG_WARNING);
    usdrlog_enablecolorize(NULL);

    char short_options[3 * SIZEOF_ARRAY(flash_long_options)];
    res = cli_build_short_options(flash_long_options, short_options, sizeof(short_options));
    if (res) {
        fprintf(stderr, "Unable to build short option list: %s\n", strerror(-res));
        return 1;
    }

    while ((opt = getopt_long(argc, argv, short_options, flash_long_options, NULL)) != -1) {
        switch (opt) {
        case OPT_READBACK_SIZE:
            readback_size = cli_parse_unsigned_or_exit("readback-size", optarg);
            if (readback_size > SIZEOF_ARRAY(outb)) {
                fprintf(stderr, "Invalid readback size '%s' (maximum is %zu bytes)\n",
                        optarg, sizeof(outb));
                return 1;
            }
            break;
        case OPT_DEVICE:
            busname = optarg;
            break;
        case OPT_LOG_LEVEL:
            usdrlog_setlevel(NULL, cli_parse_int_or_exit("log-level", optarg));
            break;
        case OPT_READ:
            filename = optarg;
            rdwr = ACTION_READBACK;
            break;
        case OPT_WRITE:
            filename = optarg;
            rdwr = ACTION_WRITE;
            break;
        case OPT_INFO:
            filename = optarg;
            rdwr = ACTION_INFO;
            break;
        case OPT_FORCE:
            force = true;
            break;
        case OPT_GOLDEN:
            golden = true;
            break;
        case OPT_CORRUPT:
            corrupt = true;
            break;
        case OPT_VERBOSE:
            verbose = true;
            break;
        case OPT_SKIP_CRC:
            crc_check = false;
            break;
        case OPT_ERASE_MASTER:
            rdwr = ACTION_ERASE_MASTER;
            break;
        case OPT_HELP:
            cli_print_usage(stdout, argv[0], "[OPTIONS]",
                            flash_long_options, flash_options_help);
            return 0;
        default:
            cli_print_usage(stderr, argv[0], "[OPTIONS]",
                            flash_long_options, flash_options_help);
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
    } else {
        device_to_destroy = dev;
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
    uint64_t flash_capacity = 0;

    res = (no_device) ? 0 : espi_flash_get_id(dev, 0, qspi_base, &fid, fid_str, sizeof(fid_str));
    if (res) {
        fprintf(stderr, "Failed to get flash ID (%s)!\n", fid_str);
        return 2;
    }
    if (!no_device) {
        fprintf(stderr, "Flash ID id %08x (%s)!\n", fid, fid_str);
        res = espi_flash_get_capacity(fid, &flash_capacity);
        if (res || flash_capacity > (uint64_t)UINT32_MAX + 1) {
            fprintf(stderr, "Unsupported flash capacity code 0x%02x!\n",
                    (fid >> 16) & 0xff);
            return 2;
        }
        if (master_offset > UINT32_MAX || master_offset >= flash_capacity) {
            fprintf(stderr, "Invalid master image offset 0x%llx for %llu-byte flash!\n",
                    (unsigned long long)master_offset, (unsigned long long)flash_capacity);
            return 2;
        }
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

    res = (no_device) ? 0 : xlnx_btstrm_parse_header_ex((const uint32_t* )outb, 256/4, &image, XLNX_BSTRM_ALLOW_CROP);
    if (res) {
        fprintf(stderr, "It looks like the FPGA G image is corrupted! res=%d\n", res);
        return 4;
    }
    bool master_is_erased = !no_device && flash_region_is_erased(outb + 256, 256);
    res = (no_device || master_is_erased) ? 0 :
            xlnx_btstrm_parse_header_ex((const uint32_t* )(outb + 256), 256/4,
                                        &image_master, XLNX_BSTRM_ALLOW_CROP);
    if (master_is_erased) {
        fprintf(stderr, "Master image is not programmed.\n");
    } else if (res) {
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
    if (rdwr == ACTION_READBACK && readback_size) {
        total_length = readback_size;
    }
    if (rdwr == ACTION_READBACK && !no_device) {
        uint64_t partition_end = golden ? master_offset : flash_capacity;
        uint64_t available = partition_end - off;
        if (!readback_size && available < total_length)
            total_length = (unsigned)available;
        if ((uint64_t)total_length > available) {
            fprintf(stderr, "Readback range exceeds flash capacity!\n");
            return 3;
        }
    }
    if (rdwr == ACTION_WRITE || rdwr == ACTION_INFO) {
        FILE* w = fopen(filename, "rb");
        if (w == NULL) {
            fprintf(stderr, "Unable to read file '%s': %s\n", filename, strerror(errno));
            return 3;
        }
        res = fseek(w, 0, SEEK_END);
        if (res) {
            fprintf(stderr, "Unable to seek file '%s': %s\n", filename, strerror(errno));
            fclose(w);
            return 3;
        }
        long file_length = ftell(w);
        if (file_length < 0 || (unsigned long)file_length > sizeof(outa)) {
            fprintf(stderr, "File '%s' is too large (maximum is %zu bytes)\n",
                    filename, sizeof(outa));
            fclose(w);
            return 3;
        }
        total_length = (unsigned)file_length;
        res = fseek(w, 0, SEEK_SET);
        if (res) {
            fprintf(stderr, "Unable to seek file '%s': %s\n", filename, strerror(errno));
            fclose(w);
            return 3;
        }
        res = fread(outa, 1, total_length, w);
        if ((unsigned)res != total_length) {
            fprintf(stderr, "Unable to read file '%s': %d read\n", filename, res);
            fclose(w);
            return 3;
        }
        fclose(w);

        // res = xlnx_btstrm_parse_header((const uint32_t* )outa, 256/4, &file);
        res = xlnx_btstrm_parse_header_ex((const uint32_t* )outa,
                                          crc_check ? (total_length / 4) : (256 / 4),
                                          &file,
                                          crc_check ? XLNX_BSTRM_PARSE_F_CRC_CHECK : XLNX_BSTRM_ALLOW_CROP);
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

        if (!no_device) {
            uint64_t partition_end = golden ? master_offset : flash_capacity;
            if ((uint64_t)off + total_length > partition_end) {
                fprintf(stderr, "Image (%u bytes) does not fit in the %s partition!\n",
                        total_length, golden ? "golden" : "master");
                return 4;
            }
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

    if (rdwr == ACTION_ERASE_MASTER) {
        char reply[100];
        char* term;

        if (!mp) {
            fprintf(stderr, "Master image is not detected!\n\n");
        }

        fprintf(stderr, " ===========================================\n");
        fprintf(stderr, " == YOU'RE GOING TO BLANK MASTER FIRMWARE ==\n");
        fprintf(stderr, " ===========================================\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Type YES if you know what're doing: ");

        if (fgets(reply, sizeof(reply), stdin) == NULL)
            return 0;

        term = strstr(reply, "\n");
        if (term) {
            *term = 0;
        }

        if (strcmp(reply, "YES") != 0)
            return 0;

        if (image.wbstar == 0) {
            fprintf(stderr, "No Master support detected: WBSTAR is 0!\n");
            return 7;
        }
        if (image.wbstar != master_offset) {
            fprintf(stderr, "Master trampoline mismatches: WBSTAR is %08x != %08x in the software!\n",
                    image.wbstar, (unsigned)master_offset);
            return 8;
        }

        fprintf(stderr, "Blanking flash starting from %08x...\n", image.wbstar);
        res = espi_flash_erase(dev, 0, qspi_base, 65536, master_offset);
        if (res) {
            fprintf(stderr, "Failed to blank flash header! res=%d", res);
            return 4;
        }
    }

    if (rdwr == ACTION_WRITE || rdwr == ACTION_READBACK) {
        fprintf(stderr, "Reading %d bytes!\n", total_length);
        res = espi_flash_read(dev, 0, qspi_base, 512, off, total_length, outb);
        if (res) {
            fprintf(stderr, "Failed to readback! res=%d", res);
            return 4;
        }
        if (rdwr == ACTION_READBACK && !readback_size) {
            unsigned image_words;
            xlnx_image_params_t readback_image;

            res = xlnx_btstrm_parse_image_ex((const uint32_t*)outb, total_length / 4,
                                              &readback_image,
                                              XLNX_BSTRM_PARSE_F_CRC_CHECK |
                                              XLNX_BSTRM_ALLOW_ERASED_TAIL,
                                              &image_words);
            if (res == 0) {
                total_length = image_words * 4;
                fprintf(stderr, "Detected image size: %u bytes.\n", total_length);
            } else {
                fprintf(stderr,
                        "Unable to determine image size (res=%d); saving the full partition.\n",
                        res);
            }
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
            return 4;
        }
    } else if (rdwr == ACTION_READBACK) {
        FILE* w = fopen(filename, "wb");
        if (w == NULL) {
            fprintf(stderr, "Unable to create file '%s': %s\n", filename, strerror(errno));
            return 3;
        }
        size_t written = fwrite(outb, 1, total_length, w);
        int write_error = ferror(w);
        int close_error = fclose(w);
        if (written != total_length || write_error || close_error != 0) {
            fprintf(stderr, "Unable to write file '%s': %s\n", filename, strerror(errno));
            return 3;
        }
    }


    destroy_device();
    return 0;
}
