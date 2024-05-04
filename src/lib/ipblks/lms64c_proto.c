// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <string.h>
#include <endian.h>
#include "lms64c_cmds.h"
#include "lms64c_proto.h"

static
unsigned _lms64c_get_block_count(unsigned cmd)
{
    switch (cmd ) {
    case CMD_PROG_MCU:
    case CMD_GET_INFO:
    case CMD_SI5351_RD:
    case CMD_SI5356_RD:
        return 1;

    case CMD_SI5351_WR:
    case CMD_SI5356_WR:
        return 2;

    case CMD_LMS7002_RD:
    case CMD_BRDSPI_RD:
    case CMD_BRDSPI8_RD:
        return 2;

    case CMD_ADF4002_WR:
        return 3;

    case CMD_LMS7002_WR:
    case CMD_BRDSPI_WR:
    case CMD_ANALOG_VAL_WR:
        return 4;

    default:
        return 1;
    }
}

static
unsigned _lms64c_get_maxdata(unsigned cmd)
{
    switch (cmd) {
    case CMD_LMS7002_RD:
    case CMD_BRDSPI_RD:
        return LMS64C_DATA_LENGTH / 2;
    case CMD_ANALOG_VAL_RD:
        return LMS64C_DATA_LENGTH / 4;
    default:
        return LMS64C_DATA_LENGTH;
    }
}

enum {
    MOD_NONE = 0,
    MOD_INT16 = 1,
    MOD_INT32 = 2,
    MOD_INT32_16 = 3, // 16bit data written into 32bit space
};

static
unsigned _lms64c_get_modifier(unsigned cmd)
{
    switch (cmd) {
    case CMD_LMS7002_WR:
    case CMD_BRDSPI_WR:
        return MOD_INT32;
    case CMD_LMS7002_RD:
    case CMD_BRDSPI_RD:
        return MOD_INT16;
    }

    return MOD_NONE;
}

static
unsigned _lms64c_get_modifier_rep(unsigned cmd)
{
    switch (cmd) {
    case CMD_LMS7002_RD:
    case CMD_BRDSPI_RD:
        return MOD_INT32_16;
    }

    return MOD_NONE;
}


int lms64c_fill_packet(uint8_t cmd, uint8_t status, uint8_t id, const uint8_t* data, unsigned length, proto_lms64c_t* out, unsigned out_cnt)
{
    unsigned blk_ratio = _lms64c_get_block_count(cmd);
    unsigned maxdata = _lms64c_get_maxdata(cmd);
    unsigned mod = _lms64c_get_modifier(cmd);
    unsigned remaining = length;
    unsigned k, off;

    for (k = 0, off = 0; (k < out_cnt) && (remaining != 0); k++) {
        unsigned this_pkt_data = remaining > maxdata ? maxdata : remaining;

        out[k].cmd = cmd;
        out[k].status = status;
        out[k].blockCount = this_pkt_data / blk_ratio;
        out[k].periphID = id;
        memset(out[k].reserved, 0, 4);

        if (data == NULL) {
        } else if (mod == MOD_INT32) {
            const uint32_t* d = (const uint32_t*)&data[off];
            for (unsigned i = 0; i < this_pkt_data; i += 4) {
                *(uint32_t*)&out[k].data[i] = htobe32(d[i >> 2]);
            }
        } else if (mod == MOD_INT16) {
            const uint16_t* d = (const uint16_t*)&data[off];
            for (unsigned i = 0; i < this_pkt_data; i += 2) {
                // TODO: Endianess
                out[k].data[i + 0] = d[i >> 1] >> 8;
                out[k].data[i + 1] = d[i >> 1] & 0xFF;
            }
        } else {
            memcpy(out[k].data, &data[off], this_pkt_data);
        }
        memset(out[k].data + this_pkt_data, 0, LMS64C_DATA_LENGTH - this_pkt_data);

        remaining -= this_pkt_data;
        off += this_pkt_data;
    }

    return (remaining == 0) ? k : -1;
}

int lms64c_parse_packet(uint8_t cmd, const proto_lms64c_t* in, unsigned in_cnt, uint8_t* data, unsigned length)
{
    unsigned i, off;
    unsigned remaining = length;
    unsigned mod = _lms64c_get_modifier_rep(cmd);

    for (i = 0, off = 0; (i < in_cnt) && (remaining != 0); i++) {
        unsigned bsz = (remaining > LMS64C_DATA_LENGTH) ? LMS64C_DATA_LENGTH : remaining;

        if (mod == MOD_INT32_16) {
            // TODO: Endianess
            const uint16_t* d = (const uint16_t*)in[i].data;
            for (unsigned j = 0; j < bsz; j += 2) {
                data[off + j + 0] = d[j + 1] >> 8;
                data[off + j + 1] = d[j + 1] & 0xFF;
            }
        } else {
            memcpy(&data[off], in[i].data, bsz);
        }

        off += bsz;
        remaining -= bsz;
    }

    return (remaining == 0) ? 0 : -1;
}


int lms64c_fill_get_fpga_info(proto_lms64c_t* out)
{
    const uint16_t addrs[] = {0x0000, 0x0001, 0x0002, 0x0003};
    return lms64c_fill_packet(CMD_BRDSPI_RD, 0, 0, (const uint8_t*)addrs, sizeof(addrs), out, 1);
}

int lms64c_fill_get_board_info(proto_lms64c_t* out)
{
    const uint8_t addrs[] = { 0x00 };
    return lms64c_fill_packet(CMD_GET_INFO, 0, 0, addrs, sizeof(addrs), out, 1);
}







