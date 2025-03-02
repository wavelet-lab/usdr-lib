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

#define LOG_TAG "DMCR"

char buffer[65536*2];
static volatile bool s_stop = false;

void on_stop(UNUSED int signo)
{
    if (s_stop)
        exit(1);

    s_stop = true;
}

static unsigned s_rx_blksampl = 0;
static unsigned s_tx_blksampl = 0;

static unsigned s_rx_blksz = 0;
static unsigned s_tx_blksz = 0;
static bool thread_stop = false;

static unsigned rx_bufcnt = 0;
static unsigned tx_bufcnt = 0;

#define MAX_CHS 64

static FILE* s_out_file[MAX_CHS];
static FILE* s_in_file[MAX_CHS];
static ring_buffer_t* rbuff[MAX_CHS];
static ring_buffer_t* tbuff[MAX_CHS];

struct rx_thread_input_s
{
    unsigned chan;
};
typedef struct rx_thread_input_s rx_thread_input_t;

struct tx_thread_input_s
{
    unsigned chan;
    unsigned samplerate;
    unsigned samples_count;
    float gain;
    double start_phase;
    double delta_phase;
};
typedef struct tx_thread_input_s tx_thread_input_t;

static rx_thread_input_t rx_thread_inputs[MAX_CHS];
static tx_thread_input_t tx_thread_inputs[MAX_CHS];

static bool tx_file_cycle = false;

typedef void* (*fn_rxtx_thread_t)(void* obj);

struct tx_header
{
    uint32_t len;
    uint32_t flags;
};
typedef struct tx_header tx_header_t;

enum tx_header_flags
{
    TXF_NONE = 0,
    TXF_READ_FILE_EOF = 1,
    TXF_READ_FILE_ERROR = 2,
};

/*
 *  Thread function - write RX stream to file
 */
void* disk_write_thread(void* obj)
{
    rx_thread_input_t* inp = (rx_thread_input_t*)obj;
    const unsigned i = inp->chan;

    while (!s_stop && !thread_stop) {
        unsigned idx = ring_buffer_cwait(rbuff[i], 100000);
        if (idx == IDX_TIMEDOUT)
            continue;

        char* data = ring_buffer_at(rbuff[i], idx);
        size_t res = fwrite(data, s_rx_blksz, 1, s_out_file[i]);
        if (res != 1) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Can't write %d bytes! error=%zd", s_rx_blksz, res);
            break;
        }

        ring_buffer_cpost(rbuff[i]);
    }

    return NULL;
}

/*
 *  Thread function - read data from file to TX stream
 */
void* disk_read_thread(void* obj)
{
    tx_thread_input_t* inp = (tx_thread_input_t*)obj;
    const unsigned i = inp->chan;
    bool interrupt = false;

    while (!s_stop && !thread_stop && !interrupt) {

        unsigned idx = ring_buffer_pwait(tbuff[i], 100000);
        if (idx == IDX_TIMEDOUT)
            continue;

        char* data = ring_buffer_at(tbuff[i], idx);
        tx_header_t* hdr = (tx_header_t*)data;

        hdr->len = fread(data + sizeof(tx_header_t), sizeof(char), s_tx_blksz, s_in_file[i]);
        hdr->flags = TXF_NONE;

        if(ferror(s_in_file[i]))
        {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "TX thread[%u]: can't read %u bytes! res=%u error=%d", i, s_tx_blksz, hdr->len, errno);
            hdr->flags |= TXF_READ_FILE_ERROR;
            interrupt = true;
        }

        if(feof(s_in_file[i]))
        {
            if(tx_file_cycle)
            {
                rewind(s_in_file[i]);

                if(hdr->len == 0)
                    hdr->len = fread(data + sizeof(tx_header_t), sizeof(char), s_tx_blksz, s_in_file[i]);

                if(hdr->len == 0)
                {
                    USDR_LOG(LOG_TAG, USDR_LOG_WARNING, "TX thread[%u]: can't loop empty file", i);
                    hdr->flags |= TXF_READ_FILE_EOF;
                    interrupt = true;
                }
            }
            else
            {
                hdr->flags |= TXF_READ_FILE_EOF;
                interrupt = true;
            }
        }

        USDR_LOG(LOG_TAG, USDR_LOG_DEBUG, "TX thread[%u]: read %u bytes from file to TX", i, hdr->len);
        ring_buffer_ppost(tbuff[i]);
    }

    return NULL;
}

static const int16_t lut_sincos60_25000[] = {
    0, 25000,
    -21651, 12500,
    -21651, -12500,
    0, -25000,
    21651, -12500,
    21651, 12500,

    0, 25000,
    -21651, 12500,
    -21651, -12500,
    0, -25000,
    21651, -12500,
    21651, 12500,
};

void* freq_gen_thread_ci16_lut(void* obj)
{
    tx_thread_input_t* inp = (tx_thread_input_t*)obj;
    const unsigned p = inp->chan;
    const unsigned tx_get_samples = inp->samples_count;

    int phase = 0;
    int lut_sz = 6;
    int k = 0;

    while (!s_stop && !thread_stop) {
        unsigned idx = ring_buffer_pwait(tbuff[p], 100000);
        if (idx == IDX_TIMEDOUT)
            continue;

        char* data = ring_buffer_at(tbuff[p], idx);
        tx_header_t* hdr = (tx_header_t*)data;
        hdr->len = tx_get_samples * sizeof(uint16_t) * 2;
        hdr->flags = TXF_NONE;
        int16_t *iqp = (int16_t *)(data + sizeof(tx_header_t));
#ifdef RAMP_PATTERN
        memset(iqp, k, hdr->len);
#else
        const int16_t *lut = lut_sincos60_25000 + phase * 2;

        unsigned i = 0;
        for (; i < tx_get_samples - lut_sz; i += lut_sz) {
            memcpy(iqp + 2 * i, lut, lut_sz * sizeof(uint16_t) * 2);
        }

        for (; i < tx_get_samples; i++) {
            memcpy(iqp + 2 * i, lut + 2 * (i % lut_sz), sizeof(uint16_t) * 2);
        }

        phase = (phase + i) % lut_sz;
#endif
        ring_buffer_ppost(tbuff[p]);
        k++;
    }

    return NULL;
}


#define USE_WVLT_SINCOS
#define MAX_TXGEN_CI16_AMPL 32760

/*
 *   Thread function - Sine generator to TX stream (ci16)
 */
void* freq_gen_thread_ci16(void* obj)
{
    tx_thread_input_t* inp = (tx_thread_input_t*)obj;
    const unsigned p = inp->chan;
    const unsigned tx_get_samples = inp->samples_count;
    const int16_t gain = DBFS_TO_AMPLITUDE(inp->gain, MAX_TXGEN_CI16_AMPL);

#ifdef USE_WVLT_SINCOS
    USDR_LOG(LOG_TAG, USDR_LOG_WARNING, "Using TX ci16 sinus generator with USE_WVLT_SINCOS opt @ ch#%d F:%.6f MHz GAIN:(%.2fdBFS = %d)",
             p, (double)inp->samplerate * inp->delta_phase / 1000000.f, inp->gain, gain);
    int32_t phase             = WVLT_CONVPHASE_F32_I32(inp->start_phase);
    const int32_t phase_delta = WVLT_CONVPHASE_F32_I32(inp->delta_phase);
#else
    double phase             = inp->start_phase;
    const double phase_delta = inp->delta_phase;
#endif

    while (!s_stop && !thread_stop) {
        unsigned idx = ring_buffer_pwait(tbuff[p], 100000);
        if (idx == IDX_TIMEDOUT)
            continue;

        char* data = ring_buffer_at(tbuff[p], idx);

        tx_header_t* hdr = (tx_header_t*)data;
        hdr->len = tx_get_samples * sizeof(uint16_t) * 2;
        hdr->flags = TXF_NONE;

        int16_t *iqp = (int16_t *)(data + sizeof(tx_header_t));

#ifdef USE_WVLT_SINCOS
        wvlt_sincos_i16_interleaved_ctrl(&phase, phase_delta, gain, true/*invert sin*/, false/*invert cos*/, iqp, tx_get_samples);
#else
        for (unsigned i = 0; i < tx_get_samples; i++) {
            float ii, qq;
            sincosf(2 * M_PI * phase, &ii, &qq);
            iqp[2 * i + 0] = -gain * (ii) + 0.5;
            iqp[2 * i + 1] =  gain * (qq) + 0.5;

            phase += phase_delta;
            if (phase > 1.0)
                phase -= 1.0;

        }
#endif //USE_WVLT_SINCOS

        ring_buffer_ppost(tbuff[p]);
    }

    return NULL;
}

/*
 *   Thread function - Sine generator to TX stream (cf32)
 */
void* freq_gen_thread_cf32(void* obj)
{
    tx_thread_input_t* inp = (tx_thread_input_t*)obj;
    const unsigned p = inp->chan;
    double phase = inp->start_phase;
    const unsigned tx_get_samples = inp->samples_count;

    while (!s_stop && !thread_stop) {
        unsigned idx = ring_buffer_pwait(tbuff[p], 100000);
        if (idx == IDX_TIMEDOUT)
            continue;

        char* data = ring_buffer_at(tbuff[p], idx);

        tx_header_t* hdr = (tx_header_t*)data;
        hdr->len = tx_get_samples * sizeof(float) * 2;
        hdr->flags = TXF_NONE;

        float *iqp = (float *)(data + sizeof(tx_header_t));

        for (unsigned i = 0; i < tx_get_samples; i++) {
            //float ii, qq; (but we use trick to not do inversion, however, it leads to incorrect phase)
            sincosf(2 * M_PI * phase, &iqp[2 * i + 1], &iqp[2 * i + 0]);
            phase += inp->delta_phase;
            if (phase > 1.0)
                phase -= 1.0;

        }

        ring_buffer_ppost(tbuff[p]);
    }

    return NULL;
}

/*
 * Get data from the temperature sensor & print it to log
 */
bool print_device_temperature(pdm_dev_t dev)
{
    uint64_t temp;
    int res = usdr_dme_get_uint(dev, "/dm/sensor/temp", &temp);

    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to get device temperature: errno %d", res);
        return false;
    } else if (temp > 65535) {
        USDR_LOG(LOG_TAG, USDR_LOG_WARNING, "The temperature sensor doen't seem to be supported by your hardware - or your device has already melted)");
    } else {
        USDR_LOG(LOG_TAG, USDR_LOG_INFO, "Temp = %.1f C", temp / 256.0);
    }
    return true;
}

enum {
    DD_RX_FREQ,
    DD_TX_FREQ,
    DD_TDD_FREQ,
    DD_RX_BANDWIDTH,
    DD_TX_BANDWIDTH,
    DD_RX_GAIN_LNA, // Before mixer
    DD_RX_GAIN_VGA, // After mixer
    DD_RX_GAIN_PGA, // After LPF
    DD_TX_GAIN,
    DD_TX_PATH,
    DD_RX_PATH,
};

/*
 * Utility Usage info
 */
static void usage(int severity, const char* me)
{
    USDR_LOG(LOG_TAG, severity, "Usage: %s \n"
                                "\t[-D device_parameters] \n"
                                "\t[-f RX_filename [./out.data]] \n"
                                "\t[-I TX_filename(s) (optionally colon-separated list)] \n"
                                "\t[-o <flag: cycle TX from file>] \n"
                                "\t[-c count [128]] \n"
                                "\t[-r samplerate [50e6]] \n"
                                "\t[-F format [ci16] | cf32] \n"
                                "\t[-C chmsk [autodetect]] \n"
                                "\t[-S RX buffer size (in samples) [4096]] \n"
                                "\t[-O TX buffer size (in samples) [4096]] \n"
                                "\t[-t <flag: TX only mode>] \n"
                                "\t[-T <flag: TX+RX mode>] \n"
                                "\t[-N <flag: No TX timestamps>] \n"
                                "\t[-q TDD_FREQ [910e6]] \n"
                                "\t[-e RX_FREQ [900e6]] \n"
                                "\t[-E TX_FREQ [920e6]] \n"
                                "\t[-w RX_BANDWIDTH [1e6]] \n"
                                "\t[-W TX_BANDWIDTH [1e6]] \n"
                                "\t[-y RX_GAIN_LNA [15]] \n"
                                "\t[-Y TX_GAIN [0]] \n"
                                "\t[-p RX_PATH ([rx_auto]|rxl|rxw|rxh|adc|rxl_lb|rxw_lb|rxh_lb)] \n"
                                "\t[-P TX_PATH ([tx_auto]|txb1|txb2|txw|txh)] \n"
                                "\t[-u RX_GAIN_PGA [15]] \n"
                                "\t[-U RX_GAIN_VGA [15]] \n"
                                "\t[-a Reference clock path [internal]] \n"
                                "\t[-x Reference clock frequency [internal clock freq]] \n"
                                "\t[-B Calibration freq [0]] \n"
                                "\t[-s Sync type [all]] \n"
                                "\t[-Q <flag: Discover and exit>] \n"
                                "\t[-R RX_LML_MODE [0]] \n"
                                "\t[-A Antenna configuration [0]] \n"
                                "\t[-H comma-separated list of sin generator start phases (FP values)] \n"
                                "\t[-d comma-separated list of sin generator phase deltas (FP values)] \n"
                                "\t[-g comma-separated list of sin generator gains (FP values, dBFS -100..0)] \n"
                                "\t[-X <flag: Skip initialization>] \n"
                                "\t[-z <flag: Continue on error>] \n"
                                "\t[-l loglevel [3(INFO)]] \n"
                                "\t[-G calibration [algo#]] \n"
                                "\t[-h <flag: This help>]",
             me);
}

/*
 * Get packet from the circular buffer & TX
 * Returns true on success, false on error or EOF
 */
static bool do_transmit(pusdr_dms_t strm, uint64_t* ts, const usdr_dms_nfo_t* nfo, bool nots, unsigned iteration, unsigned* olen, usdr_dms_send_stat_t* st)
{
    int res = 0;
    void* buffers[MAX_CHS];
    unsigned len = nfo->pktbszie;
    bool is_eof = false;
    bool is_error = false;

    for (unsigned b = 0; b < tx_bufcnt; b++)
    {
        unsigned idx = ring_buffer_cwait(tbuff[b], 1000000);
        if (idx == IDX_TIMEDOUT) {
            USDR_LOG(LOG_TAG, USDR_LOG_WARNING, "TX Cbuffer[%d] timed out!", b);
        }

        char* buf = ring_buffer_at(tbuff[b], idx);

        tx_header_t* tx_hdr = (tx_header_t*)buf;

        len = (len > tx_hdr->len) ? tx_hdr->len : len;

        is_eof |= (tx_hdr->flags & TXF_READ_FILE_EOF);
        if(is_eof)
            USDR_LOG(LOG_TAG, USDR_LOG_INFO, "Got EOF reading file [%d]", b);

        is_error |= (tx_hdr->flags & TXF_READ_FILE_ERROR);
        if(is_error)
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Error reading file [%d]", b);

        buffers[b] = (void*)(buf + sizeof(tx_header_t));
    }

    unsigned sample_bitsize = (nfo->pktbszie * 8) / nfo->pktsyms;
    unsigned sample_cnt = (len * 8) / sample_bitsize;
    USDR_LOG(LOG_TAG, USDR_LOG_DEBUG, "*** pktbszie=%u pktsyms=%u", nfo->pktbszie, nfo->pktsyms);
    USDR_LOG(LOG_TAG, USDR_LOG_DEBUG, "*** sample_bitsize=%u sample_cnt=%u", sample_bitsize, sample_cnt);

    *olen = sample_cnt;

    if(sample_cnt)
    {
        //Core TX function - transmit data from tx provider thread via circular buffer
        res = usdr_dms_send_stat(strm, (const void**)buffers, sample_cnt, nots ? UINT64_MAX : *ts, 32250, st);
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "TX error, unable to send data: errno %d, i = %d", res, iteration);
            return false;
        }
    }

    *ts += sample_cnt;

    for (unsigned b = 0; b < tx_bufcnt; b++) {
        ring_buffer_cpost(tbuff[b]);
    }

    if(is_eof || is_error)
    {
        USDR_LOG(LOG_TAG, USDR_LOG_INFO, "Exiting TX loop: eof=%d err=%d", is_eof, is_error);
        return false;
    }

    return true;
}

/*
 * RX packet & put it to the circular buffer
 * Returns true on success, false on error
 */
static bool do_receive(pusdr_dms_t strm, unsigned iteration, usdr_dms_recv_nfo_t* rxstat)
{
    void* buffers[MAX_CHS];
    int res = 0;

    for (unsigned b = 0; b < rx_bufcnt; b++)
    {
        unsigned idx = ring_buffer_pwait(rbuff[b], 1000000);
        if (idx == IDX_TIMEDOUT) {
            USDR_LOG(LOG_TAG, USDR_LOG_WARNING, "RX Pbuffer[%d] timed out!", b);
        }
        buffers[b] = ring_buffer_at(rbuff[b], idx);
    }

    //Core RX function - read data to buffers...
    res = usdr_dms_recv(strm, buffers, 2250, rxstat);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "RX error, unable to recv data: errno %d, i = %d", res, iteration);
        return false;
    }

    //...then put them to the circular buffer for rx consumer thread
    for (unsigned b = 0; b < rx_bufcnt; b++) {
        ring_buffer_ppost(rbuff[b]);
    }
    return true;
}

int main(UNUSED int argc, UNUSED char** argv)
{
    int res;
    pdm_dev_t dev;
    const char* device_name = NULL;
    unsigned rate = 50 * 1000 * 1000;
    usdr_dms_nfo_t snfo_rx;
    usdr_dms_nfo_t snfo_tx;
    pusdr_dms_t usds_rx = NULL;
    pusdr_dms_t usds_tx = NULL;
    pusdr_dms_t strms[2] = { NULL, NULL };
    pthread_t wthread[MAX_CHS];
    pthread_t rthread[MAX_CHS];
    unsigned count = 128;
    bool explicit_count = false;

    const char* filename_rx = "out.data";
    const char* filename_tx[MAX_CHS];
    memset(filename_tx, 0, sizeof(filename_tx[0]) * MAX_CHS);

    int opt;
    unsigned chmsk = 0x1;
    bool chmsk_alter = false;
    const char* fmt = "ci16";
    unsigned samples_rx = 4096;
    unsigned samples_tx = 4096;
    unsigned loglevel = USDR_LOG_INFO;
    int noinit = 0;
    unsigned dotx = 0; //means "do TX" - enables TX if dotx==1
    unsigned dorx = 1; //means "do RX" - enables RX if dorx==1
    unsigned rxflags =  DMS_FLAG_NEED_TX_STAT;
    uint64_t temp[2];
    const char* synctype = "all";
    bool listdevs = false;
    unsigned start_tx_delay = 0;
    bool nots = false;
    unsigned antennacfg = 0;
    unsigned lmlcfg = 0;
    unsigned devices = 1;
    const char* refclkpath = NULL;
    uint32_t cal_freq = 0;
    bool stop_on_error = true;
    bool tx_from_file = false;
    uint64_t fref = 0;
    unsigned statistics = 1;
    bool use_lut = false;
    unsigned calibrate = 0;

    memset(rx_thread_inputs, 0, sizeof(rx_thread_inputs));
    memset(tx_thread_inputs, 0, sizeof(tx_thread_inputs));
    for(unsigned i = 0; i < MAX_CHS; ++i)
    {
        tx_thread_input_t* inp = &tx_thread_inputs[i];
        inp->start_phase = -1;
        inp->delta_phase = -1;
        inp->gain = INT16_MIN;
    }

    //Device parameters
    //                 { endpoint, default_value, ignore flag, stop_on_fail flag }
    struct dme_findsetv_data dev_data[] = {
        [DD_RX_FREQ] = { "rx/freqency", 900e6, true, true },
        [DD_TX_FREQ] = { "tx/freqency", 920e6, true, true },

        [DD_TDD_FREQ] = { "tdd/freqency", 910e6, true, true },

        [DD_RX_BANDWIDTH] = { "rx/bandwidth", 1e6, true, true },
        [DD_TX_BANDWIDTH] = { "tx/bandwidth", 1e6, true, true },

        [DD_RX_GAIN_VGA] = { "rx/gain/vga", 15, true, true },
        [DD_RX_GAIN_PGA] = { "rx/gain/pga", 15, true, true },
        [DD_RX_GAIN_LNA] = { "rx/gain/lna", 15, true, true },
        [DD_TX_GAIN] = { "tx/gain", 0, true, true },

        [DD_RX_PATH] = { "rx/path", (uintptr_t)"rx_auto", false, true },
        [DD_TX_PATH] = { "tx/path", (uintptr_t)"tx_auto", false, true },

    };

    //primary logging for proper usage() call - may be overriden below
    usdrlog_setlevel(NULL, loglevel);
    //set colored log output
    usdrlog_enablecolorize(NULL);

    while ((opt = getopt(argc, argv, "B:U:u:R:Qq:e:E:w:W:y:Y:l:S:O:C:F:f:c:r:i:XtTNAoha:D:s:p:P:z:I:x:j:H:d:g:JG:")) != -1) {
        switch (opt) {
        //Time-division duplexing (TDD) frequency
        case 'q': dev_data[DD_TDD_FREQ].value = atof(optarg); dev_data[DD_TDD_FREQ].ignore = false; break;
        //RX frequency
        case 'e': dev_data[DD_RX_FREQ].value = atof(optarg); dev_data[DD_RX_FREQ].ignore = false; break;
        //TX frequency
        case 'E': dev_data[DD_TX_FREQ].value = atof(optarg); dev_data[DD_TX_FREQ].ignore = false; break;
        //RX bandwidth
        case 'w': dev_data[DD_RX_BANDWIDTH].value = atof(optarg); dev_data[DD_RX_BANDWIDTH].ignore = false; break;
        //TX bandwidth
        case 'W': dev_data[DD_TX_BANDWIDTH].value = atof(optarg); dev_data[DD_TX_BANDWIDTH].ignore = false; break;
        //RX LNA gain
        case 'y': dev_data[DD_RX_GAIN_LNA].value = atoi(optarg); dev_data[DD_RX_GAIN_LNA].ignore = false; break;
        //TX gain
        case 'Y': dev_data[DD_TX_GAIN].value = atoi(optarg); dev_data[DD_TX_GAIN].ignore = false; break;
        //RX LNA path ([rx_auto]|rxl|rxw|rxh|adc|rxl_lb|rxw_lb|rxh_lb)
        case 'p': dev_data[DD_RX_PATH].value = (uintptr_t)optarg; dev_data[DD_RX_PATH].ignore = false; break;
        //TX LNA path ([tx_auto]|txb1|txb2|txw|txh)
        case 'P': dev_data[DD_TX_PATH].value = (uintptr_t)optarg; dev_data[DD_TX_PATH].ignore = false; break;
        //RX PGA gain
        case 'u': dev_data[DD_RX_GAIN_PGA].value = atoi(optarg); dev_data[DD_RX_GAIN_PGA].ignore = false; break;
        //RX VGA gain
        case 'U': dev_data[DD_RX_GAIN_VGA].value = atoi(optarg); dev_data[DD_RX_GAIN_VGA].ignore = false; break;
        case 'G':
            calibrate = atoi(optarg);
            break;
        case 'J':
            use_lut = true;
            break;
        //Statistics option
        case 'j':
            statistics = atoi(optarg);
            break;
        //Reference clock source path, [internal]|external
        case 'a':
            refclkpath = optarg;
            break;
        //Reference clock (in Hz). Ignored when internal clocking is selected.
        //If omitted, the default internal ref clock will be used (26MHz typically)
        case 'x':
            fref = atof(optarg);
            break;
        //Calibration frequency
        case 'B':
            cal_freq = atof(optarg);
            break;
        //Sync type ([all]|1pps|rx|tx|any|none|off)
        case 's':
            synctype = optarg;
            break;
        //Print available devices
        case 'Q':
            listdevs = true;
            break;
        //Device additional options & parameters
        //Format is:
        //  param1=val1,param2=val2,...,paramN=valN
        //Each val may contain subparametes, delimited by ':'
        //For example, if you're using the PCIE Development board V1 revision, you can specify it's dedicated params:
        //  -Dpciefev1:osc_on
        //            - this enables the devboard clock, that can be used as 'external' clock for your on-board sdr device.
        //              (see -a & -x options above)
        //  See the full devboard parameters list in the documentation.
        case 'D':
            device_name = optarg;
            break;
        //Set log level (0 - errors only -> 6+ - trace msgs)
        case 'l':
            loglevel = atof(optarg);
            usdrlog_setlevel(NULL, loglevel);
            break;
        //Set file name to store RX data (default: ./out.data)
        //A suffix will be automatically added to the file name when using several RX RF channels
        case 'f':
            filename_rx = optarg;
            break;
        //Set file name(s) to read TX data (produce sine if omitted)
        //Use colon-separated list for several TX RF channels
        //If the number of channels exceeds the number of files, round-robin file rotation will be applied.
        case 'I':
        {
            const char* sep = ":";
            char* pch = strtok(optarg, sep);
            unsigned i = 0;

            while (pch != NULL && i < MAX_CHS)
            {
                USDR_LOG(LOG_TAG, USDR_LOG_DEBUG, "TX file #%u: '%s'", i, pch);
                filename_tx[i++] = pch;
                pch = strtok(NULL, sep);
            }

            if(!i)
            {
                USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "-I option parsing error!");
                exit(EXIT_FAILURE);
            }

            tx_from_file = true;
            break;
        }
        //TX cycling - if filesize/tx_block_sz < count
        case 'o':
            tx_file_cycle = true;
            break;
        //Block count - TX/RX samples count in one data block
        case 'c':
            count = atoi(optarg);
            explicit_count = true;
            break;
        //RX LML mode
        case 'R':
            lmlcfg = atoi(optarg);
            break;
        //Sample rate
        case 'r':
            rate = atof(optarg);
            break;
        //Set data format (default: ci16)
        case 'F':
            fmt = optarg;
            break;
        //Channels mask - autodetect if not specified
        case 'C':
            chmsk = atoi(optarg);
            chmsk_alter = true;
            break;
        //RX buffer size, in samples
        case 'S':
            samples_rx = atoi(optarg);
            break;
        //TX buffer size, in samples
        case 'O':
            samples_tx = atoi(optarg);
            break;
        //Skip device initialization
        case 'X':
            noinit = 1;
            break;
        //TX only mode
        case 't':
            dotx = 1;
            dorx = 0;
            break;
        //TX and RX mode
        case 'T':
            dotx = 1;
            dorx = 1;
            break;
        //No time stamp for TX
        case 'N':
            nots = true;
            break;
        //Antenna configuration [0]
        case 'A':
            antennacfg = atoi(optarg);
            break;
        //Comma-separated list of sin generator start phases (FP values)
        //Ordered by channel#
        //If start phase is not specified or ==-1, default sequence is applied (see start_phase[] below)
        case 'H':
        {
            char *pt = strtok(optarg, ",");
            unsigned i = 0;
            while (pt != NULL && i < MAX_CHS) {
                tx_thread_inputs[i++].start_phase = atof(pt);
                pt = strtok(NULL, ",");
            }
            break;
        }
        //Comma-separated list of sin generator phase deltas (FP values). The resulting frequency == sample_rate * phase_delta
        //Ordered by channel#
        //If not specified or ==-1, default sequence is applied (see start_dphase[] below)
        case 'd':
        {
            char *pt = strtok(optarg, ",");
            unsigned i = 0;
            while (pt != NULL && i < MAX_CHS) {
                tx_thread_inputs[i++].delta_phase = atof(pt);
                pt = strtok(NULL, ",");
            }
            break;
        }
        //Comma-separated list of sin generator gains (dBFS -100..0 range).
        //Ordered by channel#
        //If not specified, default 0dBFS is applied (see gains[] below)
        case 'g':
        {
            char *pt = strtok(optarg, ",");
            unsigned i = 0;
            while (pt != NULL && i < MAX_CHS) {
                float gn = atof(pt);
                gn = (gn > 0.0 ? 0.0 : gn);
                gn = (gn < -100.0 ? -100.0 : gn);
                tx_thread_inputs[i++].gain = gn;
                pt = strtok(NULL, ",");
            }
            break;
        }
        //Don't stop on error
        case 'z':
            stop_on_error = false;
            break;
        //Show usage
        case 'h':
            usdrlog_disablecolorize(NULL);
            usage(USDR_LOG_INFO, argv[0]);
            exit(EXIT_SUCCESS);
        default:
            usage(USDR_LOG_ERROR, argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (tx_from_file && !explicit_count) {
        count = -1;
    }

    start_tx_delay = samples_tx;

    // Discover & print available device list and exit (-Q option)
    if (listdevs) {
        char buffer[4096];
        int count = usdr_dmd_discovery(device_name, sizeof(buffer), buffer);

        USDR_LOG(LOG_TAG, USDR_LOG_INFO, "Enumerated devices %d:\n%s", count, buffer);

        return 0;
    }

    rx_bufcnt = 0;
    tx_bufcnt = 0;

    //Prepare parameters to TX
    if (dotx) {
        if(tx_from_file)
        {
            s_in_file[0] = fopen(filename_tx[0], "rb+");
            if (!s_in_file[0]) {
                USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to open TX source data file #%u '%s'", 0, filename_tx[0]);
                return 3;
            }
        }

        if (dev_data[DD_TX_BANDWIDTH].ignore) {
            dev_data[DD_TX_BANDWIDTH].ignore = false;
            dev_data[DD_TX_BANDWIDTH].value = rate;
        }
    }

    //Prepare parameters to RX
    if (dorx) {
        s_out_file[0] = fopen(filename_rx, "wb+c");
        if (!s_out_file[0]) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to create RX storage data file #%u '%s'", 0, filename_rx);
            return 3;
        }

        if (dev_data[DD_RX_BANDWIDTH].ignore) {
            dev_data[DD_RX_BANDWIDTH].ignore = false;
            dev_data[DD_RX_BANDWIDTH].value = rate;
        }
    }

    //Open device & create dev handle
    res = usdr_dmd_create_string(device_name, &dev);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to create device: errno %d", res);
        return 1;
    }

    res = usdr_dme_get_u32(dev, "/ll/devices", (unsigned*)&devices);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_DEBUG, "Set device count to 1");
    } else {
        USDR_LOG(LOG_TAG, USDR_LOG_INFO, "Devices in the array: %d", devices);
    }

    if (!chmsk_alter) {
        unsigned swchmax;
        res = usdr_dme_get_u32(dev, "/ll/sdr/max_sw_rx_chans", &swchmax);
        if (res == 0) {
            chmsk = (1ULL << devices * swchmax) - 1;
        }
        if (devices > 1) {
            fmt = "ci16";
        }
    }

    // set external reference clock
    if (refclkpath)
    {
        res = usdr_dme_set_string(dev, "/dm/sdr/refclk/path", refclkpath);
        if(res)
        {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to apply Ext Reference Clock path(%s): errno %d", refclkpath, res);
        }
        else if(fref && strcasecmp(refclkpath, "internal"))
        {
            res = usdr_dme_set_uint(dev, "/dm/sdr/refclk/frequency", fref);
            if(res)
            {
                USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to set Ext Reference Clock freq(%" PRIu64 " Hz): errno %d", fref, res);
            }
            else
            {
                uint64_t actual_fref = 0;
                res = usdr_dme_get_uint(dev, "/dm/sdr/refclk/frequency", &actual_fref);
                if(res)
                {
                    USDR_LOG(LOG_TAG, USDR_LOG_WARNING, "Unable to get actual Ext Reference Clock freq: errno %d, "
                                                        "assuming it was set to %" PRIu64 " Hz", res, fref);
                }
                else
                {
                    USDR_LOG(LOG_TAG, USDR_LOG_INFO, "Ext Reference Clock freq set to %" PRIu64
                                                     " Hz, actual: %" PRIu64 " Hz", fref, actual_fref);
                }
            }
        }
    }

    if (!noinit) {
        res = usdr_dme_set_uint(dev, "/dm/power/en", 1);
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to set power: errno %d", res);
        }

        //Set sample rate
        res = usdr_dmr_rate_set(dev, NULL, rate);
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to set device rate: errno %d", res);
            if (stop_on_error) goto dev_close;
        }

        usleep(5000);
        if (lmlcfg != 0) {
            USDR_LOG(LOG_TAG, USDR_LOG_INFO, "======================= setting LML mode to %d =======================", lmlcfg);
        }
        res = usdr_dme_set_uint(dev, "/debug/hw/lms7002m/0/rxlml", lmlcfg);

        print_device_temperature(dev);
    }

    //Open RX data stream
    if (dorx) {
        res = usdr_dms_create_ex(dev, "/ll/srx/0", fmt, chmsk, samples_rx, rxflags, &usds_rx);
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to initialize RX data stream: errno %d", res);
            if (stop_on_error) goto dev_close;
        }

        res = res ? res : usdr_dms_info(usds_rx, &snfo_rx);
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to get RX data stream info: errno %d", res);
            goto dev_close;
        } else {
            s_rx_blksampl = snfo_rx.pktsyms;
            s_rx_blksz = snfo_rx.pktbszie;
            rx_bufcnt = snfo_rx.channels;
        }
    } else {
        memset(&snfo_rx, 0, sizeof(snfo_rx));
    }

    //Open TX data stream
    if (dotx) {
        res = usdr_dms_create(dev, "/ll/stx/0", fmt, chmsk, samples_tx, &usds_tx);
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to initialize TX data stream: errno %d", res);
            if (stop_on_error) goto dev_close;
        }

        res = res ? res : usdr_dms_info(usds_tx, &snfo_tx);
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to get TX data stream info: errno %d", res);
            goto dev_close;
        } else {
            s_tx_blksz = snfo_tx.pktbszie;
            s_tx_blksampl = snfo_tx.pktsyms;
            tx_bufcnt = snfo_tx.channels;
        }
    } else {
        memset(&snfo_tx, 0, sizeof(snfo_tx));
    }

    USDR_LOG(LOG_TAG, USDR_LOG_INFO, "Configured RX %d (%d bytes) x %d buffs  TX %d x %d buffs  ===  CH_MASK %x FMT %s",
             s_rx_blksampl, s_rx_blksz, rx_bufcnt, s_tx_blksz, tx_bufcnt, chmsk, fmt);

    if (rx_bufcnt > MAX_CHS || tx_bufcnt > MAX_CHS) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Too many requested channels %d/%d (MAX: %d)", rx_bufcnt, tx_bufcnt, MAX_CHS);
        if (stop_on_error) goto dev_close;
    }

    // initialize thread input params
    for(unsigned i = 0; i < rx_bufcnt; ++i)
    {
        rx_thread_input_t* inp = &rx_thread_inputs[i];
        inp->chan = i;
    }

    static double start_phase[]  = { 0, 0.5, 0.25, 0.125 };
    static double start_dphase[] = { 0.3333333333333333333333333, 0.02, 0.03, 0.04 };
    static int16_t gains[] = {0.0, 0.0, 0.0, 0.0};

    for(unsigned i = 0; i < tx_bufcnt; ++i)
    {
        tx_thread_input_t* inp = &tx_thread_inputs[i];
        inp->chan = i;
        inp->samplerate = rate;
        inp->samples_count = samples_tx;
        inp->start_phase = inp->start_phase >= 0 ? inp->start_phase : start_phase[i % (sizeof(start_phase) / sizeof(*start_phase))];
        inp->delta_phase = inp->delta_phase >= 0 ? inp->delta_phase : start_dphase[i % (sizeof(start_dphase) / sizeof(*start_dphase))];
        inp->gain = inp->gain != INT16_MIN ? inp->gain : gains[i % (sizeof(gains) / sizeof(*gains))];
    }

    for(unsigned i = 0; i < MAX_CHS; ++i)
    {
        USDR_LOG(LOG_TAG, USDR_LOG_DEBUG, "TX SINGEN CH#%2d PHASE_START:%.4f PHASE_DELTA:%.4f",
                 i, tx_thread_inputs[i].start_phase, tx_thread_inputs[i].delta_phase);
    }
    //

    //Create TX buffers and threads
    if (dotx) {
        unsigned fidx = 1;
        for (unsigned f = 1; tx_from_file && f < tx_bufcnt; f++) {

            if(!filename_tx[fidx])
                fidx = 0;

            const char* fname = filename_tx[fidx++];

            s_in_file[f] = fopen(fname, "rb+");
            if (!s_in_file[f]) {
                USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to open TX source data file #%u '%s'", f, fname);
                return 3;
            }
            else
                USDR_LOG(LOG_TAG, USDR_LOG_DEBUG, "TX source data file #%u '%s' opened OK", f, fname);
        }

        for (unsigned i = 0; i < tx_bufcnt; i++) {
            tbuff[i] = ring_buffer_create(256, sizeof(tx_header_t) + snfo_tx.pktbszie);

            fn_rxtx_thread_t thread_func;
            if(tx_from_file)
                thread_func = disk_read_thread;
            else if(strcmp(fmt, SFMT_CI16) == 0)
                thread_func = (use_lut) ? freq_gen_thread_ci16_lut : freq_gen_thread_ci16;
            else if(strcmp(fmt, SFMT_CF32) == 0)
                thread_func = freq_gen_thread_cf32;
            else
            {
                USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to start TX thread %d: invalid format '%s', "
                                                  "use -I option to read from file or specify %s/%s data format (-F option) for sine generator",
                         i, fmt, SFMT_CI16, SFMT_CF32);
                goto dev_close;
            }

            res = pthread_create(&rthread[i], NULL, thread_func, &tx_thread_inputs[i]);

            if (res) {
                USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to start TX thread %d: errno %d", i, res);
                goto dev_close;
            }
        }
    }

    //Create RX buffers and threads
    if (dorx) {
        for (unsigned f = 1; f < rx_bufcnt; f++) {
            char fmod[1024];
            if (strcmp(filename_rx, "/dev/null") != 0) {
                snprintf(fmod, sizeof(fmod), "%s.%d", filename_rx, f);
            } else {
                snprintf(fmod, sizeof(fmod), "%s", filename_rx);
            }

            s_out_file[f] = fopen(fmod, "wb+c");
            if (!s_out_file[f]) {
                USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to create RX storage data file #%u '%s'", f, fmod);
                return 3;
            }
            else
                USDR_LOG(LOG_TAG, USDR_LOG_DEBUG, "RX storage data file #%u '%s' created OK", f, fmod);
        }

        for (unsigned i = 0; i < rx_bufcnt; i++) {
            rbuff[i] = ring_buffer_create(256, snfo_rx.pktbszie);
            res = pthread_create(&wthread[i], NULL, disk_write_thread, &rx_thread_inputs[i]);
            if (res) {
                USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to start RX thread %d: errno %d", i, res);
                goto dev_close;
            }
        }
    }

    //SIGINT handler to stop worker threads properly
    signal(SIGINT, on_stop);

    usleep(10000);
    usdr_dme_get_uint(dev, "/dm/debug/all", temp);
    usleep(1000);

    //Set calibration freq
    if (cal_freq > 1e6) {
        res = usdr_dme_set_uint(dev, "/dm/sync/cal/freq", (unsigned)(cal_freq));
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to set calibration frequency: errno %d", res);
        }
    }

    strms[1] = usds_tx;
    strms[0] = usds_rx;
    res = usdr_dms_sync(dev, "off", 2, strms);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to sync data streams: errno %d", res);
        if (stop_on_error) goto dev_close;
    }

    //Start RX streaming
    if (dorx) {
        res = usds_rx ? usdr_dms_op(usds_rx, USDR_DMS_START, 0) : -EPROTONOSUPPORT;
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to start RX data stream: errno %d", res);
            if (stop_on_error) goto dev_close;
        }
    }

    //Start TX streaming
    if (dotx) {
        res = usds_tx ? usdr_dms_op(usds_tx, USDR_DMS_START, 0) : -EPROTONOSUPPORT;
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to start TX data stream: errno %d", res);
            if (stop_on_error) goto dev_close;
        }
    }

    //Sync TX&RX data streams
    res = usdr_dms_sync(dev, synctype, 2, strms);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to sync data streams: errno %d", res);
        if (stop_on_error) goto dev_close;
    }


    //Set antenna configuration
    res = usdr_dme_set_uint(dev, "/dm/sdr/0/tfe/antcfg", antennacfg);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to set antenna configuration parameter [%u]: errno %d", antennacfg, res);
    }

    //Set device parameters from the dev_data struct (see above)
    if (!noinit) {
        res = usdr_dme_findsetv_uint(dev, "/dm/sdr/0/", SIZEOF_ARRAY(dev_data), dev_data);
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to set device parameters: errno %d", res);
            if (stop_on_error) goto dev_close;
        }
    }

    if (calibrate) {
        res = usdr_dme_set_uint(dev, "/dm/sdr/0/calibrate", calibrate);
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "SDR Calibration done: %d\n", res);

        res = usdr_dme_findsetv_uint(dev, "/dm/sdr/0/", SIZEOF_ARRAY(dev_data), dev_data);
    }

    uint64_t stm = start_tx_delay;

    //Check stream handles and exit if NULL
    if (dotx && !usds_tx) {
        goto dev_close;
    }
    if (dorx && !usds_rx) {
        goto dev_close;
    }

    //TX & RX
    usdr_dms_recv_nfo_t rxstat;
    usdr_dms_send_stat_t txstat;

    struct timespec tp, tp_prev;
    clock_gettime(CLOCK_REALTIME, &tp_prev);

    uint64_t pkt_rx_time = 1000000000ULL * snfo_rx.pktsyms / rate;
    uint64_t s_rx_time = tp_prev.tv_sec * 1000000000ULL + tp_prev.tv_nsec;
    uint64_t logprev = s_rx_time;


    uint64_t s_tx_time = tp_prev.tv_sec * 1000000000ULL + tp_prev.tv_nsec;
    uint64_t logprev_tx = s_tx_time;

    uint64_t exp_rx_ts = 0;
    uint64_t overruns_sps = 0;
    uint64_t overruns_cnt = 0;

    unsigned tx_samples_cnt;

    for (unsigned i = 0; !s_stop && (i < count); i++)
    {
        if(dotx && !do_transmit(usds_tx, &stm, &snfo_tx, nots, i, &tx_samples_cnt, &txstat))
            goto stop;

        if(dorx && !do_receive(usds_rx, i, &rxstat))
            goto stop;

        if (statistics) {
            clock_gettime(CLOCK_REALTIME, &tp);

            if (dorx) {
                uint64_t curtime = tp.tv_sec * 1000000000ULL + tp.tv_nsec;
                uint64_t took = curtime - (tp_prev.tv_sec * 1000000000ULL + tp_prev.tv_nsec);
                if (i == 0) { //Alternative
                    s_rx_time = curtime;
                }

                s_rx_time += pkt_rx_time;
                int64_t lag = curtime - s_rx_time;

                if (exp_rx_ts != rxstat.fsymtime) {
                    overruns_cnt++;
                    overruns_sps += rxstat.fsymtime - exp_rx_ts;
                }
                exp_rx_ts = rxstat.fsymtime + snfo_rx.pktsyms;

                if ((statistics > 1) || (logprev + 1000000000ULL < curtime)) {
                    fprintf(stderr, "RX%6d/%d: Sps: %11ld lst:%d  took:%11ld ns lag:%11ld ns OVR:%ld / %ld\n", i, count, rxstat.fsymtime, rxstat.totlost, took, lag,
                            overruns_cnt, overruns_sps / snfo_rx.pktsyms);
                    logprev = logprev + 1000000000ULL;
                }
            }

            if (dotx) {
                uint64_t pkt_tx_time = 1000000000ULL * tx_samples_cnt / rate;

                uint64_t curtime = tp.tv_sec * 1000000000ULL + tp.tv_nsec;
                uint64_t took = curtime - (tp_prev.tv_sec * 1000000000ULL + tp_prev.tv_nsec);
                if (i == 0) { //Alternative
                    s_tx_time = curtime;
                }

                s_tx_time += pkt_tx_time;
                int64_t lag = curtime - s_tx_time;

                if ((statistics > 1) || (logprev_tx + 1000000000ULL < curtime)) {
                    fprintf(stderr, "TX%6d/%d: Sps: %11ld lst:%d  took:%11ld ns lag:%11ld ns UND:%d -- %08x FIFO: %d  len:%d\n", i, count, 0l, 0, took, lag,
                            txstat.underruns, txstat.ktime, txstat.fifo_used, tx_samples_cnt);
                    logprev_tx = logprev_tx + 1000000000ULL;
                }
            }

            tp_prev = tp;
        }
    }

    fprintf(stderr, "RX Overruns %ld (samples %ld)\n", overruns_cnt, overruns_sps);

stop:
    usdr_dme_get_uint(dev, "/dm/debug/rxtime", temp);

    //Stop RX&TX streams
    if (dorx) {
        res = usdr_dms_op(usds_rx, USDR_DMS_STOP, 0);
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to stop RX data stream: errno %d", res);
            goto dev_close;
        }
    }
    if (dotx) {
        res = usdr_dms_op(usds_tx, USDR_DMS_STOP, 0);
        if (res) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to stop TX data stream: errno %d", res);
            goto dev_close;
        }
    }

    thread_stop = true;

    res = usdr_dme_get_uint(dev, "/dm/debug/all", temp);
    if (res) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to get device debug data: errno %d", res);
        goto dev_close;
    }

    print_device_temperature(dev);

    //Finalize all the threads started above
    if (dorx) {
        for (unsigned i = 0; i < rx_bufcnt; i++) {
            pthread_join(wthread[i], NULL);
        }
    }
    if (dotx) {
        for (unsigned i = 0; i < tx_bufcnt; i++) {
            pthread_join(rthread[i], NULL);
        }
    }

dev_close:
    //Dispose stream handles
    if (strms[1]) usdr_dms_destroy(strms[1]);
    if (strms[0]) usdr_dms_destroy(strms[0]);
    //Close & Dispose dev connection handle
    usdr_dmd_close(dev);
    return res;
}
