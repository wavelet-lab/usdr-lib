// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#define _GNU_SOURCE
#include <stdint.h>
#include <inttypes.h>

#include <dm_dev.h>
#include <dm_rate.h>
#include <dm_stream.h>
#include "../ipblks/streams/streams.h"

#include <usdr_logging.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <string.h>

#include "../common/ring_buffer.h"
#include "sincos_functions.h"
#include "fast_math.h"

#define LOG_TAG "LML7"

#define MAX_CHS       2
#define MAX_PATTERN   16384

static unsigned num_rx = 2;

static uint16_t s_pattern_r0[2 * MAX_PATTERN];
static uint16_t s_pattern_r1[2 * MAX_PATTERN];

static uint16_t s_pattern_t0[2 * MAX_PATTERN];
static uint16_t s_pattern_t1[2 * MAX_PATTERN];

static size_t d_off = 0;
static uint16_t* d_pattern_r0 = NULL;
static uint16_t* d_pattern_r1 = NULL;

static const uint32_t s_dtest[] = {
    // Single
    0x00000000,
    0xFFFFFFFF,
    0xAAAAAAAA,
    0x55555555,
    0xA5A5A5A5,
    0x5A5A5A5A,
    0x11111111,
    0x44444444,

    // Dual
    0x0000FFFF,
    0x0000AAAA,
    0x00005555,
    0x0000A55A,
    0xAAAAFFFF,
    0xFFFFAAAA,
    0xFFFF5555,
    0xFFFFA55A,
    0xAAAA5555,
};

#define DUAL_TESTS  SIZEOF_ARRAY(s_dtest)

static void fill_test(uint16_t* ptr, unsigned test_no)
{
    if (test_no >= DUAL_TESTS)
        return;

    uint32_t* pc = (uint32_t*)ptr;
    uint32_t c = s_dtest[test_no];
    for (unsigned i = 0; i < MAX_PATTERN; i++) {
        pc[i] = c;
    }
}

static uint32_t s_sync = 0;
static uint32_t s_sync_errs = 0;
static uint32_t s_off = 0;
static uint32_t s_resync = 0;
static uint32_t s_sbit_err = 0;
static uint32_t s_mbit_err = 0;
static uint32_t s_bpos_err[16];

struct bit_error_stat {
    unsigned bit_errors[16];     // Bit error counts
    unsigned bit_pos_errors[16]; // Individual bit pos errors
};
typedef struct bit_error_stat bit_error_stat_t;

__attribute__((optimize("-O3")))
static int bitcount(unsigned v)
{
    unsigned c = 0;
    for (unsigned i = 32; i != 0; i--) {
        if (v & 1)
            c++;
        v >>= 1;
    }
    return c;
}

static void bit_error_init(bit_error_stat_t* p)
{
    for (unsigned i = 0; i < 16; i++) {
        p->bit_errors[i] = 0;
        p->bit_pos_errors[i] = 0;
    }
}

__attribute__((optimize("-O3")))
static unsigned bit_error_check(bit_error_stat_t* p, uint16_t a, uint16_t b)
{
    unsigned diff = (a ^ b);
    if (diff == 0) {
        return 0;
    }

    unsigned bcnt = bitcount(diff);
    p->bit_errors[bcnt]++;

    for (unsigned i = 0; i != 16; i++) {
        if (diff & (1 << i)) {
            p->bit_pos_errors[i]++;
        }
    }
    return bcnt;
}

static void check_test(const uint16_t* data, uint16_t mask, unsigned count, unsigned test_no)
{
    if (test_no >= DUAL_TESTS)
        return;

    const uint32_t* pc = (const uint32_t*)data;
    uint32_t c = s_dtest[test_no];
    uint16_t p0 = c >> 16;
    uint16_t p1 = c;
    unsigned j = s_off;
    unsigned l = 0;
    unsigned errs = 0;

    if (!s_sync) {
        for (j = 0; j < count; j++) {
            if (data[j] == p0) {
                s_sync = 0;
                goto synced;
            }
            s_sync_errs ++;
        }
        return;
    }

synced:

    for (; j < count; j++, l++) {
        uint16_t p = ((l % 2) ? p1 : p0) & mask;
        unsigned diff = (p ^ data[j]);
        if (diff == 0) {
            errs = 0;
            continue;
        }

        errs++;
        if (bitcount(diff) > 1) {
            s_mbit_err++;
        } else {
            s_sbit_err++;
        }

        for (unsigned i = 0; i != 16; i--) {
            if (diff & (1 << i)) {
                s_bpos_err[i]++;
            }
        }
    }

    s_off = l % 2;
}

enum LML_MODE {
    LML_MODE_NORMAL = 0,
    LML_MODE_LOOPBACK = 1,
    LML_MODE_LFSR = 2,
    LML_MODE_LOOPBACK_GEN = 3,
};

static pdm_dev_t dev = NULL;
static int mode = 0;
static bool mimo_mode = true;
static const char* fmt_rx = "ci16";
static const char* fmt_tx = "ci16";

static unsigned samples_rx = MAX_PATTERN;
static unsigned samples_tx = MAX_PATTERN;
static pusdr_dms_t usds_rx = NULL;
static pusdr_dms_t usds_tx = NULL;
static pusdr_dms_t strms[2] = { NULL, NULL };
static FILE* files_r[2] = { NULL, NULL };

static float specific_rate = 10.0e6;
static unsigned maximum_iterations = 10;
static bool memcached = false; // Store data in RAM before doing processing

static void dev_exit()
{
    exit(EXIT_FAILURE);
}

static void dev_set_rate(unsigned rate)
{
    int res;
    unsigned lmode = mode;

    for (unsigned k = 0; ; k ++) {
        res = !dev ? 0 : usdr_dmr_rate_set(dev, NULL, rate);
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to set device rate: errno %d", res);

            if (k == 10)
                dev_exit();
        } else {
            break;
        }
    }

    if (lmode == LML_MODE_LOOPBACK_GEN) {
        lmode = LML_MODE_LOOPBACK;
    }

    res = !dev ? 0 : usdr_dme_set_uint(dev, "/debug/hw/lms7002m/0/rxlml", lmode);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to set device mode: errno %d", res);
        dev_exit();
    }


}

static void dev_set_vio(unsigned mv)
{
    int res = !dev ? 0 : usdr_dme_set_uint(dev, "/dm/sdr/0/vio", mv);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to set device LML VIO: errno %d", res);
        dev_exit();
    }
}

static void dev_set_rx_dly(unsigned type, unsigned val)
{
    unsigned mode = ((type) << 8) | val;
    int res = !dev ? 0 : usdr_dme_set_uint(dev, "/dm/sdr/0/phy_rx_dly", mode);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to set device LML RX DLY: errno %d", res);
        dev_exit();
    }
}

static void dev_set_rx_phase(unsigned val)
{
    unsigned mode = val + 1;
    int res = !dev ? 0 : usdr_dme_set_uint(dev, "/dm/sdr/0/rx/phase_ovr", mode);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to set device LML RX DLY: errno %d", res);
        dev_exit();
    }

}

static void dev_set_tx_phase_rc(unsigned val)
{
    int res = !dev ? 0 : usdr_dme_set_uint(dev, "/dm/sdr/0/tx/phase_ovr_rc", val);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to set device LML TX VAL IQ: errno %d", res);
        dev_exit();
    }
}

static void dev_set_tx_phase(unsigned val, unsigned val_iq)
{
    unsigned mode = val + 1;
    unsigned mode_iq = val_iq + 1;
    int res = !dev ? 0 : usdr_dme_set_uint(dev, "/dm/sdr/0/tx/phase_ovr", mode);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to set device LML TX: errno %d", res);
        dev_exit();
    }

    res = !dev ? 0 : usdr_dme_set_uint(dev, "/dm/sdr/0/tx/phase_ovr_iq", mode_iq);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to set device LML TX IQ: errno %d", res);
        dev_exit();
    }
}

static void dev_set_rx_lfsr_hw_checker(bool en)
{
    int res = !dev ? 0 : usdr_dme_set_uint(dev, "/dm/sdr/0/phy_rx_lfsr", en);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to set HW LFSR Checker: errno %d", res);
        dev_exit();
    }
}

static void dev_set_tx_lfsr_hw_generator(bool en)
{
    int res = !dev ? 0 : usdr_dme_set_uint(dev, "/dm/sdr/0/phy_tx_lfsr", en);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to set HW LFSR Generator: errno %d", res);
        dev_exit();
    }
}

static void dev_get_rx_lfsr_hw_checker(unsigned errs[4])
{
    uint64_t v = 0;
    int res = !dev ? 0 : usdr_dme_get_uint(dev, "/dm/sdr/0/phy_rx_lfsr", &v);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to get HW LFSR BER: errno %d", res);
        dev_exit();
    }

    for (unsigned j = 0; j < 4; j++) {
        errs[j] = (v >> (j * 16)) & 0xffff;
    }
}

static void dev_deinit()
{
    if (dev == NULL) return;
    int res;

    if (usds_rx) {
        res = usdr_dms_op(usds_rx, USDR_DMS_STOP, 0);
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to stop RX data stream: errno %d", res);
            dev_exit();
        }
    }

    if (usds_tx) {
        res = usdr_dms_op(usds_tx, USDR_DMS_STOP, 0);
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to stop TX data stream: errno %d", res);
            dev_exit();
        }
    }

    if (strms[1]) usdr_dms_destroy(strms[1]);
    if (strms[0]) usdr_dms_destroy(strms[0]);

    usds_rx = usds_tx = NULL;
    strms[0] = strms[1] = NULL;
}

static void dev_init()
{
    if (dev == NULL) return;

    const char* synctype = "all";
    int res;
    bool tx = (mode == LML_MODE_LOOPBACK);

    usdr_channel_info_t chans_rx;
    usdr_channel_info_t chans_tx;

    chans_rx.count = 2;
    chans_rx.flags = 0;
    chans_rx.phys_names = NULL;
    chans_rx.phys_nums = NULL;

    chans_tx.count = 2;
    chans_tx.flags = 0;
    chans_tx.phys_names = NULL;
    chans_tx.phys_nums = NULL;

    res = usdr_dms_create_ex2(dev, "/ll/srx/0", fmt_rx, &chans_rx, samples_rx, 0, NULL, &usds_rx);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to initialize RX data stream: errno %d", res);
        dev_exit();
    }

    if (tx) {
        res = usdr_dms_create_ex2(dev, "/ll/stx/0", fmt_tx, &chans_tx, samples_tx, 0, NULL, &usds_tx);
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to initialize TX data stream: errno %d", res);
            dev_exit();
        }
    } else {
        usds_tx = NULL;
    }

    strms[1] = usds_tx;
    strms[0] = usds_rx;
    res = usdr_dms_sync(dev, "off", 2, strms);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to sync data streams: errno %d", res);
        dev_exit();
    }

    //Start RX streaming
    res = usdr_dms_op(usds_rx, USDR_DMS_START, 0);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to start RX data stream: errno %d", res);
        dev_exit();
    }

    //Start TX streaming
    if (tx) {
        res = usdr_dms_op(usds_tx, USDR_DMS_START, 0);
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to start TX data stream: errno %d", res);
            dev_exit();
        }
    }

    //Sync TX&RX data streams
    res = usdr_dms_sync(dev, synctype, 2, strms);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to sync data streams: errno %d", res);
        dev_exit();
    }
}

void bits16_to_str(uint16_t v, char str[17])
{
    for (unsigned h = 0; h < 16; h++) {
        str[h] = '0' + (((v << h) & 0x8000) ? 1 : 0);
    }
    str[16] = 0;
}

void bits32_to_str(uint32_t v, char str[33])
{
    for (unsigned h = 0; h < 32; h++) {
        str[h] = '0' + (((v << h) & 0x80000000) ? 1 : 0);
    }
    str[32] = 0;
}



static uint32_t lfsr24_next(uint32_t lfsr)
{
    // Feedback taps: 24, 23, 22, 17 (zero-indexed: 23, 22, 21, 16)
    uint32_t bit = ((lfsr >> 23) ^ (lfsr >> 22) ^ (lfsr >> 21) ^ (lfsr >> 16)) & 1;
    lfsr = ((lfsr << 1) | bit);
    return lfsr;
}

static uint32_t lfsr16_next(uint32_t lfsr) {
    // Feedback taps at 16, 14, 13, 11 (zero-indexed: 15, 13, 12, 10)
    uint32_t bit = ((lfsr >> 15) ^ (lfsr >> 13) ^ (lfsr >> 12) ^ (lfsr >> 10)) & 1;
    lfsr = ((lfsr << 1) | bit);
    return lfsr;
}

__attribute__((optimize("-O3")))
static uint32_t lfsr15_next(uint32_t lfsr) {
    // Feedback taps at 15 and 14 (zero-indexed: 14, 13)
    uint32_t bit = ((lfsr >> 14) ^ (lfsr >> 13)) & 1;
    lfsr = ((lfsr << 1) | bit);
    return lfsr;
}

static uint32_t lfsr14_next(uint32_t lfsr) {
    // Feedback taps at 14, 13, 11, 10 (zero-indexed: 13, 12, 10, 9)
    uint32_t bit = ((lfsr >> 13) ^ (lfsr >> 12) ^ (lfsr >> 10) ^ (lfsr >> 9)) & 1;
    lfsr = ((lfsr << 1) | bit);
    return lfsr;
}

static uint32_t lfsr12_next(uint32_t lfsr) {
    // Feedback taps at 12, 11, 10, 4 (zero-indexed: 11, 10, 9, 3)
    uint32_t bit = ((lfsr >> 11) ^ (lfsr >> 10) ^ (lfsr >> 9) ^ (lfsr >> 3)) & 1;
    lfsr = ((lfsr << 1) | bit);
    return lfsr;
}

static uint32_t lfsr8_next(uint32_t lfsr) {
    // Feedback taps at 8, 6, 5, 4 (zero-indexed: 7, 5, 4, 3)
    uint32_t bit = ((lfsr >> 7) ^ (lfsr >> 5) ^ (lfsr >> 4) ^ (lfsr >> 3)) & 1;
    lfsr = ((lfsr << 1) | bit);
    return lfsr;
}

unsigned lfsr_off = 4;
unsigned lfsr_mask_ch = 0xffe;

unsigned lfsr_g[2] = { 0, 0 };
unsigned lfsr_r[2] = { 0, 0 };
unsigned lfsr_xorm[2] = { 0, 1 };

struct lfsr_stream {
    unsigned off;
    unsigned mask_ch;
    unsigned mask_lfsr;

    unsigned cnt_burst_good;
    unsigned cnt_sync;  // Number of sync
    unsigned cnt_good;
    unsigned cnt_resync;
    unsigned xor_stage;

    unsigned state; // current state
    unsigned next;  // prediction for next

    bit_error_stat_t berr;
};
typedef struct lfsr_stream lfsr_stream_t;

void lfsr_init(lfsr_stream_t* d, unsigned off, unsigned m_check)
{
    d->off = off;
    d->mask_ch = m_check;
    d->mask_lfsr = m_check | 1;

    d->cnt_burst_good = 0;
    d->cnt_resync = 0;
    d->cnt_sync = 0;
    d->xor_stage = 0;

    d->state = 0;
    d->next = 0;

    bit_error_init(&d->berr);
}

enum {
    FLAGS_ERRORS = 1,
    FLAGS_VERBOSE = 2,
};

__attribute__((optimize("-O3")))
static void dump_lfsr_complex(const char* prefix, lfsr_stream_t* d, const uint16_t* pattern, unsigned sps_count, unsigned flags)
{
    char str[2][17];
    char str_f[2][33];
    bool show_error = (flags & FLAGS_ERRORS) == FLAGS_ERRORS;
    bool verbose = (flags & FLAGS_VERBOSE) == FLAGS_VERBOSE;

    for (unsigned p = 0; p < sps_count; p++) {
        unsigned a[2];
        bool match[2];

        for (unsigned j = 0; j < 2; j++) {
            a[j] = pattern[2 * p + j] >> d[j].off;

            if (d[j].cnt_burst_good == 0) {
                // Resync
                d[j].state = a[j];
                d[j].cnt_burst_good++;
            } else {

                d[j].state <<= 1;

                if ((d[j].state & d[j].mask_ch) == (a[j] & d[j].mask_ch)) {
                    d[j].state |= a[j];
                    d[j].cnt_burst_good++;
                    d[j].cnt_sync++;
                } else if (d[j].cnt_sync < 20) {
                    // Bit error burins sync
                    d[j].state |= (a[j] & 1);
                    d[j].cnt_sync++;
                } else {
                    unsigned bcnt = bit_error_check(&d[j].berr, a[j] & d->mask_lfsr, d[j].state & d->mask_lfsr);
                    if (bcnt >= 6) {
                        // Looks like we lost LFSR sync
                        d[j].state = 0;
                        d[j].cnt_resync++;
                        d[j].cnt_burst_good = 0;
                        d[j].cnt_sync = 0;
                    } else {
                        // Recover single bits error to continue checking BER
                        d[j].state = lfsr15_next(d[j].state >> 1) ^ d[j].xor_stage;
                    }
                }
            }

            match[j] = d[j].next == d[j].state;
            d[j].next = lfsr15_next(d[j].state) ^ d[j].xor_stage;
        }

        if ((show_error || verbose) && (!match[0] || !match[1] || verbose)) {
            for (unsigned j = 0; j < 2; j++) {
                bits16_to_str(a[j], str[j]);
                bits32_to_str(d[j].state, str_f[j]);
            }

            fprintf(stderr, "%s%3d: [%04x %04x]: %s %s -- [%08x %08x]: %s %s [%08x %08x] %c%c\n", prefix, p, a[0], a[1], str[0], str[1],
                    d[0].state, d[1].state, str_f[0], str_f[1], d[0].next, d[1].next,
                    match[0] ? '+' : '-', match[1] ? '+' : '-');
        }

    }
}

static void do_receive_rst()
{
    if (d_pattern_r0 == NULL) {
        d_pattern_r0 = malloc(2 * sizeof(uint16_t) * samples_rx * maximum_iterations);
    }
    if (d_pattern_r1 == NULL) {
        d_pattern_r1 = malloc(2 * sizeof(uint16_t) * samples_rx * maximum_iterations);
    }

    if (d_pattern_r0 == NULL || d_pattern_r1 == NULL) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "NOT ENOUGH MEMORY\n");
        dev_exit();
    }

    d_off = 0;
}
static int do_receive(bool do_rcv, void* buffers[MAX_CHS])
{
    usdr_dms_recv_nfo_t rxstat;
    int res = 0;
    buffers[0] = memcached ? d_pattern_r0 + 2 * d_off : s_pattern_r0;
    buffers[1] = memcached ? d_pattern_r1 + 2 * d_off : s_pattern_r1;

    d_off += samples_rx;

    if (!do_rcv)
        return 0;

    res = usdr_dms_recv(strms[0], buffers, 2250, &rxstat);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "RX error, unable to recv data: errno %d", res);
        return res;
    }

    fprintf(stderr, ".");
    for (unsigned i = 0; i < num_rx; i++) {
        if (files_r[i]) {
            fwrite(buffers[i], 2 * samples_rx, 1, files_r[i]);
        }
    }


    return 0;
}

enum {
    IDX_AI,
    IDX_AQ,
    IDX_BI,
    IDX_BQ,
};
#define TOTAL_STREAMS 4


void do_lfsr_test(lfsr_stream_t str[TOTAL_STREAMS])
{
    int res;
    unsigned flags = 0; //FLAGS_VERBOSE;
    for (unsigned i = 0; i < TOTAL_STREAMS; i++) {
        lfsr_init(&str[i], 4, 0xffe);
        str[i].xor_stage = i & 1; // LML interface invert bit in LFSR stream for Q components
    }

    for (unsigned k = 0; k < (memcached ? 2 : 1); k++) {
        if (k == 1) {
            fprintf(stderr, "\nChecking...");
        }
        do_receive_rst();

        for (unsigned cnt = 0; cnt < maximum_iterations; cnt++) {
            bool do_rcv = !memcached || (k == 0);
            bool do_chk = !memcached || (k == 1);
            void* buffers[MAX_CHS];

            res = do_receive(do_rcv, buffers);
            if (res) {
                return;
            }

            // Do inplace checks (for low samplerate)
            if (do_chk) {
                dump_lfsr_complex("A", &str[0], buffers[0], samples_rx, flags);
                dump_lfsr_complex("B", &str[2], buffers[1], samples_rx, flags);
            }
        }
    }


    fprintf(stderr, "\n==============================================================\n");
    //Total statisics
    const char* ch_names[TOTAL_STREAMS] = { "AI", "AQ", "BI", "BQ" };
    for (unsigned i = 0; i < TOTAL_STREAMS; i++) {
        unsigned rest = 0;
        for (unsigned k = 5; k < 16; k++) {
            rest += str[i].berr.bit_errors[k];
        }
        fprintf(stderr, " %s: Resync %8d [%8d %8d %8d %8d -- %8d]\n",
                ch_names[i], str[i].cnt_resync,
                str[i].berr.bit_errors[1], str[i].berr.bit_errors[2], str[i].berr.bit_errors[3], str[i].berr.bit_errors[4], rest);
        for (unsigned j = 0; j < 16; j++) {
            if (str[i].berr.bit_pos_errors[j]) {
                fprintf(stderr, " %s:      BITp[%2d]: %8d\n", ch_names[i], j, str[i].berr.bit_pos_errors[j]);
            }
        }
    }
}

#define MAX_TAPS 64

void do_lfsr_hwtx_test(unsigned str[TOTAL_STREAMS])
{
    dev_set_tx_lfsr_hw_generator(false);
    usleep(1);
    dev_set_tx_lfsr_hw_generator(true);
    dev_set_rx_lfsr_hw_checker(true);
    usleep(1000 * maximum_iterations);
    dev_get_rx_lfsr_hw_checker(str);
}

void lfsr_test_hw_tx()
{
    mode = LML_MODE_LOOPBACK_GEN;
    float rate = specific_rate;
    unsigned str[MAX_TAPS][TOTAL_STREAMS];

    dev_set_rate(rate);
    dev_init();

    for (unsigned k = 0; k < MAX_TAPS; k++) {
        //dev_init();
        //dev_set_tx_phase(k, 0); // CLK
        //dev_set_rate(rate);

        dev_set_tx_phase_rc(k);
        do_lfsr_hwtx_test(str[k]);
    }

    dev_deinit();

    for (unsigned j = 0; j < 4; j++) {
        fprintf(stderr, "TXCH%d: ", j);

        for (unsigned k = 0; k < MAX_TAPS; k++) {
            unsigned ecnt = str[k][j];

            fprintf(stderr, "%c", (ecnt == 0) ? ' ' : (ecnt < 1000) ? 'x' : 'X');
        }

        fprintf(stderr, "\n");
    }
}


void lfsr_test_shw_tx()
{
    mode = LML_MODE_LOOPBACK_GEN;
    float rate = specific_rate;
    dev_set_rate(rate);

    unsigned str[MAX_TAPS][MAX_TAPS][TOTAL_STREAMS];

    for (unsigned l = 0; l < MAX_TAPS; l++) {
        for (unsigned k = 0; k < MAX_TAPS; k++) {
            dev_init();

            dev_set_tx_phase(k, l); // CLK
            dev_set_rate(rate);

            do_lfsr_hwtx_test(str[l][k]);

            dev_deinit();
        }
    }

    for (unsigned l = 0; l < MAX_TAPS; l++) {
        for (unsigned j = 0; j < 4; j++) {
            fprintf(stderr, "TX_IQ%d_CH%d: ", l, j);

            for (unsigned k = 0; k < MAX_TAPS; k++) {
                unsigned ecnt = str[l][k][j];

                fprintf(stderr, "%c", (ecnt == 0) ? ' ' : (ecnt < 1000) ? 'x' : 'X');
            }
            fprintf(stderr, "\n");
        }
    }

}


void do_lfsr_hw_test(unsigned str[TOTAL_STREAMS])
{
    dev_set_rx_lfsr_hw_checker(true);
    usleep(1000 * maximum_iterations);
    dev_get_rx_lfsr_hw_checker(str);
}

void lfsr_test_hw()
{
    mode = LML_MODE_LFSR;

    float rate = specific_rate;
    dev_set_rate(rate);
    unsigned str[MAX_TAPS][TOTAL_STREAMS];

    for (unsigned k = 0; k < MAX_TAPS; k++) {
        dev_init();

        dev_set_rx_phase(k); // CLK
        dev_set_rate(rate);

        do_lfsr_hw_test(str[k]);

        dev_deinit();
    }

    for (unsigned j = 0; j < 4; j++) {
        fprintf(stderr, "CH%d: ", j);

        for (unsigned k = 0; k < MAX_TAPS; k++) {
            unsigned ecnt = str[k][j];

            fprintf(stderr, "%c", (ecnt == 0) ? ' ' : (ecnt < 1000) ? 'x' : 'X');
        }

        fprintf(stderr, "\n");
    }


}

void lfsr_tests(bool hwgen)
{
    mode = hwgen ? LML_MODE_LOOPBACK_GEN : LML_MODE_LFSR ;
    lfsr_stream_t str[MAX_TAPS][TOTAL_STREAMS];

    dev_set_vio(2300);
    float rate = specific_rate;
    dev_set_rate(rate);

    // looks like MCLK comes later, we need to delay all the data
    for (unsigned k = 0; k < MAX_TAPS; k++) {
        dev_init();

#if 0
        dev_set_rx_dly(0, 0); // CLK
        dev_set_rx_dly(1, k); // FRAME
        for (unsigned h = 0; h < 12; h++) {
            dev_set_rx_dly(h + 2, k); // D0
        }
#endif
        if (hwgen) {
            dev_set_tx_lfsr_hw_generator(true);
            dev_set_tx_phase(k, k); // CLK
        } else {
            dev_set_rx_phase(k); // CLK
        }
        dev_set_rate(rate);


//#endif
        do_lfsr_test(str[k]);

        dev_deinit();
    }

    fprintf(stderr, "\nX_BITXX: ");
    for (unsigned k = 0; k < MAX_TAPS; k++) {
        fprintf(stderr, (MAX_TAPS <= 32) ? "%6d" : "%3d", k);
    }
    fprintf(stderr, "\nX_BITXX: ");
    for (unsigned k = 0; k < MAX_TAPS; k++) {
        float period = 1e9 / specific_rate / 2;
        float off = 0.078 * k;
        unsigned p = 1000 * off / period;

        fprintf(stderr, (MAX_TAPS <= 32) ? "%6d" : "%3d", p);
    }

    // Individual bit errors first
    // Total bit error distribution
    for (unsigned h = 0; h < 2; h++) {
        for (unsigned j = 0; j < 4; j++) {
            for (unsigned b = 0; b < 12; b++) {
                fprintf(stderr, "\n%d_BIT%c%2d: ", j, h == 0 ? 'p' : 't', b);
                for (unsigned k = 0; k < MAX_TAPS; k++) {
                    unsigned ecnt = (h == 0) ? str[k][j].berr.bit_pos_errors[b] : str[k][j].berr.bit_errors[b + 1];

                    if (MAX_TAPS <= 32) {
                        if (ecnt <= 999999) {
                            fprintf(stderr, "%6d", ecnt);
                        } else {
                            fprintf(stderr, (k % 2) ? "xxxxxx" : "XXXXXX");
                        }
                    } else if (MAX_TAPS <= 64) {
                        if (ecnt <= 999) {
                            fprintf(stderr, "%3d", ecnt);
                        } else {
                            fprintf(stderr, (k % 2) ? "xxx" : "XXX");
                        }
                    } else {
                        if (ecnt <= 9) {
                            fprintf(stderr, "%1d", ecnt);
                        } else {
                            fprintf(stderr, (k % 2) ? "x" : "X");
                        }
                    }
                }
            }
        }
        fprintf(stderr, "\n================== distribution of total bit errors ==============================");
    }
    fprintf(stderr, "\n");

}


int main(int argc, char** argv)
{
    bool dry_run = false;
    float sbit_err_p = 0.0; // Emulation
    float mbit_err_p = 0.0; // Emulation

    unsigned specific_test = 0;

    const char* device_name = NULL;
    unsigned loglevel = USDR_LOG_INFO;

    unsigned statistics = 0;

    bool hwtxrx_tests = false;
    bool shwtx_tests = false;
    bool hwtx_tests = false;
    bool hw_tests = false;
    bool dump_rx = false;
    int opt, res;

    while ((opt = getopt(argc, argv, "Mi:r:j:D:t:l:dowWZz")) != -1) {
        switch (opt) {
        case 'M':
            memcached = true;
            break;
        case 'i':
            maximum_iterations = atoi(optarg);
            break;
        case 'r':
            specific_rate = atof(optarg);
            break;
        case 'j':
            statistics = atoi(optarg);
            break;
        case 'D':
            device_name = optarg;
            break;
        case 't':
            specific_test = atoi(optarg);
            break;
        case 'l':
            loglevel = atof(optarg);
            usdrlog_setlevel(NULL, loglevel);
            break;
        case 'd':
            dry_run = true;
            break;
        case 'o':
            dump_rx = true;
            break;
        case 'z':
            hwtxrx_tests = true;
            break;
        case 'W':
            hwtx_tests = true;
            break;
        case 'w':
            hw_tests = true;
            break;
        case 'Z':
            shwtx_tests = true;
            break;
        default:
            exit(EXIT_FAILURE);
        }
    }

    usdrlog_setlevel(NULL, loglevel);
    // Check if is tty
    usdrlog_enablecolorize(NULL);

    //Open device & create dev handle
    res = dry_run ? 0 : usdr_dmd_create_string(device_name, &dev);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to create device: errno %d", res);
        return 1;
    }

    if (dump_rx) {
        for (unsigned i = 0; i < num_rx; i++) {
            files_r[i] = fopen(i == 0 ? "out_lml7_0.dat" : "out_lml7_1.dat", "wb+c");
            if (!files_r[i]) {
                USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to create RX storage data file!\n");
                return 3;
            }
        }
    }


    // Tests
    if (shwtx_tests) {
        lfsr_test_shw_tx();
    } else if (hwtx_tests) {
        lfsr_test_hw_tx();
    } else if (hw_tests) {
        lfsr_test_hw();
    } else {
        lfsr_tests(hwtxrx_tests);
    }

    res = dry_run ? 0 : usdr_dmd_close(dev);
    return res;
}








