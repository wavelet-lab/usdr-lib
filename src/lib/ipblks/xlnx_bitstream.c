// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "xlnx_bitstream.h"
#include <usdr_logging.h>
#include <string.h>
#include <errno.h>

enum {
    W_DUMMY = 0xffffffff,
    W_BYTEORD = 0x000000bb,
    W_BUSWIDTH = 0x11220044,
    W_SYNCWORD = 0xAA995566,
    W_NOP = 0x20000000,
};

enum {
    XLNX_REG_CRC = 0x00,
    XLNX_REG_FAR = 0x01,
    XLNX_REG_FDRI = 0x02,
    XLNX_REG_FDRO = 0x03,
    XLNX_REG_CMD = 0x04,
    XLNX_REG_CTL0 = 0x05,
    XLNX_REG_MASK = 0x06,
    XLNX_REG_STAT = 0x07,
    XLNX_REG_LOUT = 0x08,
    XLNX_REG_COR0 = 0x09,
    XLNX_REG_MFWR = 0x0a,
    XLNX_REG_CBC = 0x0b,
    XLNX_REG_IDCODE = 0x0c,
    XLNX_REG_AXSS = 0x0d,
    XLNX_REG_COR1 = 0x0e,
    XLNX_REG_CSOB = 0x0f,
    XLNX_REG_WBSTAR = 0x10,
    XLNX_REG_TIMER = 0x11,
    XLNX_REG_UNK12 = 0x12,
    XLNX_REG_RBCRC_SW = 0x13,
    XLNX_REG_UNK14 = 0x14,
    XLNX_REG_UNK15 = 0x15,
    XLNX_REG_BOOTSTS = 0x16,
    XLNX_REG_UNK17 = 0x17,
    XLNX_REG_CTL1 = 0x18,
    XLNX_REG_UNK19 = 0x19,
    XLNX_REG_UNK1A = 0x1a,
    XLNX_REG_UNK1B = 0x1b,
    XLNX_REG_UNK1C = 0x1c,
    XLNX_REG_UNK1D = 0x1d,
    XLNX_REG_UNK1E = 0x1e,
    XLNX_REG_BSPI = 0x1f
};

enum {
    XLNX_CMD_RCRC = 0x07,
    XLNX_CMD_IPROG = 0x0f,
};

#define CRC32C_REFLECTED_POLY 0x82F63B78u
static inline uint32_t xlnx_btstrm_crc32_pushbit(uint32_t crc, uint32_t bit)
{
    uint32_t mix = (crc ^ bit) & 1;
    crc >>= 1;
    if (mix) {
        crc ^= CRC32C_REFLECTED_POLY;
    }

    return crc;
}

static uint32_t xlnx_btstrm_crc32_regw(uint32_t crc, uint16_t reg, uint32_t data)
{
    // Not sure which ones to skip, but so far it works well
    switch (reg) {
    case XLNX_REG_BOOTSTS:
    case XLNX_REG_CSOB:
    case XLNX_REG_UNK12:
    case XLNX_REG_UNK14:
    case XLNX_REG_UNK15:
        return crc;
    }

    for (unsigned i = 0; i < 32; i++) {
        crc = xlnx_btstrm_crc32_pushbit(crc, (data >> i) & 1);
    }

    for (unsigned i = 0; i < 5; i++) {
        crc = xlnx_btstrm_crc32_pushbit(crc, (reg >> i) & 1);
    }

    return crc;
}

int xlnx_btstrm_parse_header_ex(const uint32_t* mem,
                                unsigned len,
                                xlnx_image_params_t* stat,
                                unsigned flags)
{
    uint32_t w;
    unsigned ptr = 0;
    bool devid_found = false;
    bool wbstar_found = false;
    uint32_t stream_crc = 0;
    uint32_t crc_word_cnt = 0;

    memset(stat, 0, sizeof(*stat));

    // Sync FSM
    bool bo_seen = false;
    bool bw_seen = false;
    for (; ptr < len; ptr++) {
        w = be32toh(mem[ptr]);
        switch (w) {
        case W_DUMMY:
            continue;
        case W_BYTEORD:
            bo_seen = true;
            continue;
        case W_BUSWIDTH:
            bw_seen = true;
            continue;
        case W_SYNCWORD:
            ptr++;
            goto next;
        default:
            // Unrecognized symbol
            USDR_LOG("BSTR", USDR_LOG_DEBUG, "Unrecognised WORD: n=%d w=%08x\n", ptr, w);
            return -EINVAL;
        }
    }
next:
    if (!bo_seen || !bw_seen)
        return -EINVAL;

    uint16_t last_reg = XLNX_REG_CRC;
    for (; ptr < len; ptr++) {
        w = be32toh(mem[ptr]);
        uint8_t ptype = (w >> 29) & 0x7;
        uint8_t op;
        uint16_t reg;
        uint32_t count;

        if (ptype == 1) {
            op = (w >> 27) & 0x3;
            reg = (w >> 13) & 0x3fff;
            count = w & 0x7ff;
            last_reg = reg;
        } else if (ptype == 2) {
            op = (w >> 27) & 0x3;
            reg = last_reg;
            count = w & 0x7ffffff;
        } else {
            USDR_LOG("BSTR", USDR_LOG_DEBUG, "Unrecognised WORD: n=%d w=%08x\n", ptr, w);
            return -EINVAL;
        }

        if (op != 2 || count == 0) {
            continue;
        }

        for (unsigned i = 0; i < count; i++) {
            if (++ptr >= len) {
                if (flags & XLNX_BSTRM_ALLOW_CROP)
                    break;

                return -EINVAL;
            }

            w = be32toh(mem[ptr]);
            if (reg == XLNX_REG_IDCODE) {
                stat->devid = w;
                devid_found = true;
            } else if (reg == XLNX_REG_WBSTAR) {
                stat->wbstar = w;
                wbstar_found = true;
            } else if (reg == XLNX_REG_CMD) {
                if (w == XLNX_CMD_IPROG)
                    stat->iprog = true;
            } else if (reg == XLNX_REG_AXSS) {
                stat->usr_access2 = w;
            }

            if (count == 1 && ptype == 1 && reg != 1 && reg != XLNX_REG_CMD) {
                USDR_LOG("BSTR", USDR_LOG_NOTE, "Register %x: %x\n", reg, w);
            }

            if (flags & XLNX_BSTRM_PARSE_F_CRC_CHECK) {
                if (reg == XLNX_REG_CRC) {
                    crc_word_cnt++;

                    USDR_LOG("BSTR", USDR_LOG_NOTE, "CRC BLOCK=%d BIN=%8x STR=%8x\n", crc_word_cnt, w, stream_crc);
                    if (w != stream_crc) {
                        USDR_LOG("BSTR", USDR_LOG_ERROR, "Bitstream CRC mismatch: block=%d stream=%08x infile=%08x\n",
                                 crc_word_cnt, stream_crc, w);
                        return -EBADMSG;
                    }

                    stream_crc = 0;
                } else if (reg == XLNX_REG_CMD && w == XLNX_CMD_RCRC) {
                    stream_crc = 0;
                } else {
                    stream_crc = xlnx_btstrm_crc32_regw(stream_crc, reg, w);
                }
            }
        }
    }

    if (flags & XLNX_BSTRM_PARSE_F_CRC_CHECK) {
        if (crc_word_cnt == 0) {
            USDR_LOG("BSTR", USDR_LOG_ERROR,  "Bitstream no CRC blocks found!\n");
            return -EBADMSG;
        }
        USDR_LOG("BSTR", USDR_LOG_INFO, "Bitstream CRC %d block(s) validated\n", crc_word_cnt);
    }
    return devid_found && wbstar_found ? 0 : -ENOENT;
}

int xlnx_btstrm_parse_header(const uint32_t* mem, unsigned len, xlnx_image_params_t* stat)
{
    return xlnx_btstrm_parse_header_ex(mem, len, stat, 0);
}

int xlnx_btstrm_iprgcheck(
        const xlnx_image_params_t* internal_golden,
        const xlnx_image_params_t* newimg,
        unsigned wbstar,
        bool golden_image)
{
    if (internal_golden->devid != newimg->devid) {
        USDR_LOG("BSTR", USDR_LOG_ERROR, "FPGA Devid mismatch: FPGA=%08x in new image %08x\n",
                internal_golden->devid, newimg->devid);
        return -EINVAL;
    }
    if (newimg->wbstar == wbstar && !golden_image) {
        USDR_LOG("BSTR", USDR_LOG_ERROR, "The new image is the golden image, but requested master!\n");
        return -EINVAL;
    }
    if (golden_image && (newimg->wbstar != wbstar || !newimg->iprog)) {
        USDR_LOG("BSTR", USDR_LOG_ERROR, "You requested to update the golden image but image provided is not!\n");
        return -EINVAL;
    }
    if (!golden_image && (newimg->wbstar != 0)) {
        USDR_LOG("BSTR", USDR_LOG_ERROR, "You requested to update master image but golden image was provided!\n");
        return -EINVAL;
    }
    if (!golden_image && (internal_golden->wbstar != wbstar)) {
        USDR_LOG("BSTR", USDR_LOG_ERROR, "The FPGA golden image isn't aligned with the master image! Update the golden first\n");
        return -EINVAL;
    }
    return 0;
}
