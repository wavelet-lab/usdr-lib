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

#define MAX_CHS 32

static FILE* s_out_file[MAX_CHS];
static FILE* s_in_file[MAX_CHS];
static ring_buffer_t* rbuff[MAX_CHS];
static ring_buffer_t* tbuff[MAX_CHS];

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
    unsigned i = (intptr_t)obj;

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
    unsigned i = (intptr_t)obj;
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

double start_phase[8] = { 0, 0.5, 0.25, 0.125 };
double start_dphase[8] = { 0.3333333333333333333333333, 0.02, 0.03, 0.04 };
unsigned tx_get_samples;

/*
 *   Thread function - Sine generator to TX stream (ci16)
 */
void* freq_gen_thread_ci16(void* obj)
{
    unsigned p = (intptr_t)obj;
    double phase = start_phase[p];

    while (!s_stop && !thread_stop) {
        unsigned idx = ring_buffer_pwait(tbuff[p], 100000);
        if (idx == IDX_TIMEDOUT)
            continue;

        char* data = ring_buffer_at(tbuff[p], idx);
        int16_t *iqp = (int16_t *)data;

        for (unsigned i = 0; i < tx_get_samples; i++) {
            float ii, qq;
            sincosf(2 * M_PI * phase, &ii, &qq);
            iqp[2 * i + 0] = -30000 * (ii) + 0.5;
            iqp[2 * i + 1] = 30000 * (qq) + 0.5;

            phase += start_dphase[p];
            if (phase > 1.0)
                phase -= 1.0;

        }

        ring_buffer_ppost(tbuff[p]);
    }

    return NULL;
}

/*
 *   Thread function - Sine generator to TX stream (cf32)
 */
void* freq_gen_thread_cf32(void* obj)
{
    unsigned p = (intptr_t)obj;
    double phase = start_phase[p];

    while (!s_stop && !thread_stop) {
        unsigned idx = ring_buffer_pwait(tbuff[p], 100000);
        if (idx == IDX_TIMEDOUT)
            continue;

        char* data = ring_buffer_at(tbuff[p], idx);
        float *iqp = (float *)data;

        for (unsigned i = 0; i < tx_get_samples; i++) {
            //float ii, qq; (but we use trick to not do inversion, however, it leads to incorrect phase)
            sincosf(2 * M_PI * phase, &iqp[2 * i + 1], &iqp[2 * i + 0]);
            phase += start_dphase[p];
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
        USDR_LOG(LOG_TAG, USDR_LOG_WARNING, "Unable to get device temperature: errno %d", res);
        return false;
    } else {
        USDR_LOG(LOG_TAG, USDR_LOG_INFO, "Temp = %.1f C", temp / 256.0);
        return true;
    }
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
    USDR_LOG(LOG_TAG, severity, "Usage: %s "
                                "[-D device] "
                                "[-f RX_filename [./out.data]] "
                                "[-I TX_filename ] "
                                "[-o <flag: cycle TX from file>] "
                                "[-c count [128]] "
                                "[-r samplerate [50e6]] "
                                "[-F format [ci16] | cf32] "
                                "[-C chmsk [0x1]] "
                                "[-S TX samples_per_blk [4096]] "
                                "[-O RX samples_per_blk [4096]] "
                                "[-t <flag: TX only mode>] "
                                "[-T <flag: TX+RX mode>] "
                                "[-N <flag: No TX timestamps>] "
                                "[-q TDD_FREQ [910e6]] "
                                "[-e RX_FREQ [900e6]] "
                                "[-E TX_FREQ [920e6]] "
                                "[-w RX_BANDWIDTH [1e6]] "
                                "[-W TX_BANDWIDTH [1e6]] "
                                "[-y RX_GAIN_LNA [15]] "
                                "[-Y TX_GAIN [0]] "
                                "[-p RX_PATH ([rx_auto]|rxl|rxw|rxh|adc|rxl_lb|rxw_lb|rxh_lb)] "
                                "[-P TX_PATH ([tx_auto]|txb1|txb2|txw|txh)] "
                                "[-u RX_GAIN_PGA [15]] "
                                "[-U RX_GAIN_VGA [15]] "
                                "[-a Reference clock path [internal]] "
                                "[-x Reference clock frequency [0(not set)]] "
                                "[-B Calibration freq [0]] "
                                "[-s Sync type [all]] "
                                "[-Q <flag: Discover and exit>] "
                                "[-R RX_LML_MODE [0]] "
                                "[-A Antenna configuration [0]] "
                                "[-X <flag: Skip initialization>] "
                                "[-z <flag: Continue on error>] "
                                "[-l loglevel [3(INFO)]] "
                                "[-h <flag: This help>]",
             me);
}

/*
 * Get packet from the circular buffer & TX
 * Returns true on success, false on error or EOF
 */
static bool do_transmit(pusdr_dms_t strm, uint64_t* ts, const usdr_dms_nfo_t* nfo, bool nots, unsigned iteration)
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

    if(sample_cnt)
    {
        //Core TX function - transmit data from tx provider thread via circular buffer
        res = usdr_dms_send(strm, (const void**)buffers, sample_cnt, nots ? UINT64_MAX : *ts, 32250);
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
static bool do_receive(pusdr_dms_t strm, unsigned iteration)
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
    res = usdr_dms_recv(strm, buffers, 2250, NULL);
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
    const char* filename = "out.data";
    const char* infilename = "/dev/zero";
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

    while ((opt = getopt(argc, argv, "B:U:u:R:Qq:e:E:w:W:y:Y:l:S:O:C:F:f:c:r:i:XtTNAoha:D:s:p:P:z:I:x:")) != -1) {
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
        //Reference clock path, [internal]|external
        case 'a':
            refclkpath = optarg;
            break;
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
        //Device name
        case 'D':
            device_name = optarg;
            break;
        //Set log level (0 - errors only -> 6+ - trace msgs)
        case 'l':
            loglevel = atof(optarg);
            usdrlog_setlevel(NULL, loglevel);
            break;
        //Set file name to store RX data (default: ./out.data)
        case 'f':
            filename = optarg;
            break;
        //Set file name to read TX data (produce sine if omitted)
        case 'I':
            infilename = optarg;
            tx_from_file = true;
            break;
        //TX cycling - if filesize/tx_block_sz < count
        case 'o':
            tx_file_cycle = true;
            break;
        //Block count - how many samples blocks to TX/RX
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
        //Channels mask
        case 'C':
            chmsk = atoi(optarg); chmsk_alter = true;
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
        s_in_file[0] = fopen(infilename, "rb+");
        if (!s_in_file[0]) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to open data file(tx) '%s'", infilename);
            return 3;
        }

        if (dev_data[DD_TX_BANDWIDTH].ignore) {
            dev_data[DD_TX_BANDWIDTH].ignore = false;
            dev_data[DD_TX_BANDWIDTH].value = rate;
        }
    }

    //Prepare parameters to RX
    if (dorx) {
        s_out_file[0] = fopen(filename, "wb+c");
        if (!s_out_file[0]) {
            USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to create data file(rx) '%s'", filename);
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
                uint64_t actual_fref = fref;
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
            if (stop_on_error) goto dev_close;
            s_rx_blksampl = 0;
            s_rx_blksz = 0;
            rx_bufcnt = 0;
        } else {
            s_rx_blksampl = snfo_rx.pktsyms;
            s_rx_blksz = snfo_rx.pktbszie;
            rx_bufcnt = snfo_rx.channels;
        }
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
            if (stop_on_error) goto dev_close;
            snfo_tx.channels = 0;
            snfo_tx.pktsyms = 0;
        } else {
            s_tx_blksz = snfo_tx.pktbszie;
            s_tx_blksampl = snfo_tx.pktsyms;
            tx_bufcnt = snfo_tx.channels;
        }
    }

    USDR_LOG(LOG_TAG, USDR_LOG_INFO, "Configured RX %d (%d bytes) x %d buffs  TX %d x %d buffs  ===  CH_MASK %x FMT %s",
             s_rx_blksampl, s_rx_blksz, rx_bufcnt, s_tx_blksz, tx_bufcnt, chmsk, fmt);

    if (rx_bufcnt > MAX_CHS || tx_bufcnt > MAX_CHS) {
        USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Too many requested channels %d/%d (MAX: %d)", rx_bufcnt, tx_bufcnt, MAX_CHS);
        if (stop_on_error) goto dev_close;
    }

    tx_get_samples = samples_tx;

    //Create TX buffers and threads
    if (dotx) {
        for (unsigned i = 0; i < tx_bufcnt; i++) {
            tbuff[i] = ring_buffer_create(256, sizeof(tx_header_t) + snfo_tx.pktbszie);

            fn_rxtx_thread_t thread_func;
            if(tx_from_file)
                thread_func = disk_read_thread;
            else if(strcmp(fmt, SFMT_CI16) == 0)
                thread_func = freq_gen_thread_ci16;
            else if(strcmp(fmt, SFMT_CF32) == 0)
                thread_func = freq_gen_thread_cf32;
            else
            {
                USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to start TX thread %d: invalid format '%s', "
                                                  "use -I option to read from file or specify %s/%s data format (-F option) for sine generator",
                         i, fmt, SFMT_CI16, SFMT_CF32);
                goto dev_close;
            }

            res = pthread_create(&rthread[i], NULL, thread_func, (void*)(intptr_t)i);

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
            snprintf(fmod, sizeof(fmod), "%s.%d", filename, f);

            s_out_file[f] = fopen(fmod, "wb+c");
            if (!s_out_file[f]) {
                USDR_LOG(LOG_TAG, USDR_LOG_ERROR, "Unable to create RX storage data file '%s'", fmod);
                return 3;
            }
        }

        for (unsigned i = 0; i < rx_bufcnt; i++) {
            rbuff[i] = ring_buffer_create(256, snfo_rx.pktbszie);
            res = pthread_create(&wthread[i], NULL, disk_write_thread, (void*)(intptr_t)i);
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

    uint64_t stm = start_tx_delay;

    //Check stream handles and exit if NULL
    if (dotx && !usds_tx) {
        goto dev_close;
    }
    if (dorx && !usds_rx) {
        goto dev_close;
    }

    //TX & RX
    for (unsigned i = 0; !s_stop && (i < count); i++)
    {
        if(dotx && !do_transmit(usds_tx, &stm, &snfo_tx, nots, i))
            goto stop;

        if(dorx && !do_receive(usds_rx, i))
            goto stop;
    }

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
