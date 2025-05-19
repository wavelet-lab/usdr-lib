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
// #define SFMT_I8   "i8"
#define SFMT_I12  "i12"
#define SFMT_I16  "i16"

// #define SFMT_CI8  "ci8"
#define SFMT_CI12 "ci12"
#define SFMT_CI16 "ci16"

#define SFMT_F32  "f32"
#define SFMT_CF32 "cf32"
#define SFMT_CF32_CI12 "cf32@ci12"
#define SFMT_CI16_CI12 "ci16@ci12"

#define SFMT_FFT512_LOGPWR_I16 "cfftlpwri16"

/// unspecified format, i.e. special structure-like format,
/// in that case @ref spburst represent total number of bytes
/// in the structure
#define SFMT_UNS "uns"

#define MAX_CHANNELS 64

#define CHANNEL_NULL 255

enum channel_map {
    CH_NULL = 255,
    CH_ANY = 254,

    CH_SWAP_IQ_FLAG = 128,
};

struct channel_info {
    uint8_t ch_map[MAX_CHANNELS]; // map to physical channels
};
typedef struct channel_info channel_info_t;

struct channel_map_info {
    const char* name;
    unsigned hwidx;
};
typedef struct channel_map_info channel_map_info_t;

int channel_info_remap(unsigned count, const char** names, const channel_map_info_t* table, channel_info_t* out);

struct stream_config {
    const char* sfmt;         // Samples format
    unsigned spburst;         // Samples per burst
    unsigned burstspblk;      // Bursts per block; 0 -- auto
    unsigned chcnt;           // Number of logical channel

    channel_info_t channels;  // Channel routing
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
