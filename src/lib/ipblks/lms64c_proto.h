// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMS64C_PROTO_H
#define LMS64C_PROTO_H

#include <stdint.h>

enum PROTO_LMS64C {
    LMS64C_PKT_LENGTH = 64,
    LMS64C_DATA_LENGTH = 56,
};

struct proto_lms64c {
    unsigned char cmd;
    unsigned char status;
    unsigned char blockCount;
    unsigned char periphID;
    unsigned char reserved[4];
    unsigned char data[LMS64C_DATA_LENGTH];
};
typedef struct proto_lms64c proto_lms64c_t;


enum {
    LMS_RST_DEACTIVATE = 0,
    LMS_RST_ACTIVATE = 1,
    LMS_RST_PULSE = 2,
};

int lms64c_fill_packet(uint8_t cmd, uint8_t status, uint8_t id, const uint8_t* data, unsigned length, proto_lms64c_t* out, unsigned out_cnt);
int lms64c_parse_packet(uint8_t cmd, const proto_lms64c_t* in, unsigned in_cnt, uint8_t* data, unsigned length);


int lms64c_fill_get_fpga_info(proto_lms64c_t* out);
int lms64c_fill_get_board_info(proto_lms64c_t* out);



#endif
