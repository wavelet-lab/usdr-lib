// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef STREAMS_H
#define STREAMS_H

#include "../../port/usdr_port.h"

enum sample_trivial_types {
    STT_REAL_INTEGER,
    STT_REAL_FLOAT,
    STT_COMPLEX_INTEGER,
    STT_COMPLEX_FLOAT,
    STT_INDEXED,
};

struct sample_format {
    unsigned sample_width_bits; //Individual sample width (for complex tpyes, width of both real and complex)
    unsigned sample_type;
};

// lowlevel sample format for streaming
#define SFMT_I8   "i8"
#define SFMT_I12  "i12"
#define SFMT_I16  "i16"

#define SFMT_CI8  "ci8"
#define SFMT_CI12 "ci12"
#define SFMT_CI16 "ci16"

#define SFMT_F32  "f32"
#define SFMT_CF32 "cf32"

/// unspecified format, i.e. special structure-like format,
/// in that case @ref spburst represent total number of bytes
/// in the structure
#define SFMT_UNS "uns"


struct stream_config {
    const char* sfmt;    //Samples format
    unsigned spburst;    //Samples per burst
    unsigned burstspblk; //Bursts per block; 0 -- auto
    unsigned chmsk;      //Channel mask
};

struct fifo_config {
    unsigned bpb;        // bytes per burst
    unsigned burstspblk; // configure bursts in block; 0 -- do not apply
    unsigned oob_off;    // in stream out of band information offset
    unsigned oob_len;    // in stream OOB length
};

typedef uint64_t stream_time_t;

struct bitsfmt {
    uint8_t bits;
    bool complex;
    uint8_t func[30]; // Special dsp function
};

struct bitsfmt get_bits_fmt(const char* fmt);


struct parsed_data_format {
    const char *host_fmt;
    const char *wire_fmt;
};

int stream_parse_dformat(char* dmft, struct parsed_data_format* ofmt);



#endif
