// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#define _GNU_SOURCE
#include <stdint.h>
#include <inttypes.h>

#include <dm_dev.h>
#include <dm_rate.h>
#include <dm_stream.h>

#include <usdr_logging.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <string.h>

#include "../common/ring_buffer.h"

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
            fprintf(stderr, "Can't write %d bytes! error=%zd\n",
                    s_rx_blksz, res);
            break;
        }

        ring_buffer_cpost(rbuff[i]);
    }

    return NULL;
}

void* disk_read_thread(void* obj)
{
    unsigned i = (intptr_t)obj;

    while (!s_stop && !thread_stop) {
        unsigned idx = ring_buffer_pwait(tbuff[i], 100000);
        if (idx == IDX_TIMEDOUT)
            continue;

        char* data = ring_buffer_at(tbuff[i], idx);
        size_t res = fread(data, s_tx_blksz, 1, s_in_file[i]);
        if (res != 1) {
            fprintf(stderr, "Can't read %d bytes! error=%zd\n",
                    s_rx_blksz, res);
            break;
        }

        ring_buffer_ppost(tbuff[i]);
    }

    return NULL;
}

double start_phase[8] = { 0, 0.5, 0.25, 0.125 };
double start_dphase[8] = { 0.3333333333333333333333333, 0.02, 0.03, 0.04 };
unsigned tx_get_samples;
// Sine generator
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
            //float ii, qq; (but we use trick to not do invertion however it leads to incorrect phase)
            sincosf(2 * M_PI * phase, &iqp[2 * i + 1], &iqp[2 * i + 0]);
            phase += start_dphase[p];
            if (phase > 1.0)
                phase -= 1.0;

        }

        ring_buffer_ppost(tbuff[p]);
    }

    return NULL;
}

bool print_device_temperature(pdm_dev_t dev)
{
    uint64_t temp[2];
    res = usdr_dme_get_uint(dev, "/dm/sensor/temp", temp);
    if (res) {
        fprintf(stderr, "Unable to get device temperature: errno %d\n", res);
        return false;
    } else {
        fprintf(stderr, "Temp = %.1f C\n", temp[0] / 256.0);
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
    const char* filename = "out.data";
    const char* infilename = "/dev/zero";
    int opt;
    unsigned chmsk = 0x1;
    bool chmsk_alter = false;
    const char* fmt = "ci16";
    unsigned samples = 4096;
    unsigned loglevel = USDR_LOG_INFO;
    unsigned resync = 1;
    int noinit = 0;
    unsigned dotx = 0;
    unsigned dorx = 1;
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

    while ((opt = getopt(argc, argv, "B:U:u:R:Qq:e:E:w:W:y:Y:l:S:C:F:f:c:r:i:XtTNA:a:D:s:p:P:z")) != -1) {
        switch (opt) {
        case 'q': dev_data[DD_TDD_FREQ].value = atof(optarg); dev_data[DD_TDD_FREQ].ignore = false; break;
        case 'e': dev_data[DD_RX_FREQ].value = atof(optarg); dev_data[DD_RX_FREQ].ignore = false; break;
        case 'E': dev_data[DD_TX_FREQ].value = atof(optarg); dev_data[DD_TX_FREQ].ignore = false; break;
        case 'w': dev_data[DD_RX_BANDWIDTH].value = atof(optarg); dev_data[DD_RX_BANDWIDTH].ignore = false; break;
        case 'W': dev_data[DD_TX_BANDWIDTH].value = atof(optarg); dev_data[DD_TX_BANDWIDTH].ignore = false; break;
        case 'y': dev_data[DD_RX_GAIN_LNA].value = atoi(optarg); dev_data[DD_RX_GAIN_LNA].ignore = false; break;
        case 'Y': dev_data[DD_TX_GAIN].value = atoi(optarg); dev_data[DD_TX_GAIN].ignore = false; break;
        case 'p': dev_data[DD_RX_PATH].value = (uintptr_t)optarg; dev_data[DD_RX_PATH].ignore = false; break;
        case 'P': dev_data[DD_TX_PATH].value = (uintptr_t)optarg; dev_data[DD_TX_PATH].ignore = false; break;
        case 'u': dev_data[DD_RX_GAIN_PGA].value = atoi(optarg); dev_data[DD_RX_GAIN_PGA].ignore = false; break;
        case 'U': dev_data[DD_RX_GAIN_VGA].value = atoi(optarg); dev_data[DD_RX_GAIN_VGA].ignore = false; break;
        case 'a':
            refclkpath = optarg;
            break;
        case 'B':
            cal_freq = atof(optarg);
            break;
        case 's':
            synctype = optarg;
            break;
        case 'Q':
            listdevs = true;
            break;
        case 'D':
            device_name = optarg;
            break;
        case 'i':
            resync = atoi(optarg);
            break;
        case 'l':
            loglevel = atof(optarg);
            break;
        case 'f':
            filename = optarg;
            break;
        case 'c':
            count = atoi(optarg);
            break;
        case 'R':
            lmlcfg = atoi(optarg);
            break;
        case 'r':
            rate = atof(optarg);
            break;
        case 'F':
            fmt = optarg;
            break;
        case 'C':
            chmsk = atoi(optarg); chmsk_alter = true;
            break;
        case 'S':
            samples = atoi(optarg);
            break;
        case 'X':
            noinit = 1;
            break;
        case 't':
            dotx = 1;
            dorx = 0;
            break;
        case 'T':
            dotx = 1;
            dorx = 1;
            break;
        case 'N':
            nots = true;
            break;
        case 'A':
            antennacfg = atoi(optarg);
            break;
        case 'z':
            stop_on_error = false;
            break;
        default:
            fprintf(stderr, "Usage: %s '[-D device] [-f filename] [-c count] [-r samplerate] [-F format] [-C chmsk] [-S samples_per_blk] [-l loglevel] "
                    "[-q TDD_FREQ] [-e RX_FREQ] [-E TX_FREQ] [-w RX_BANDWIDTH] [-W TX_BANDWIDTH] [-y RX_GAIN_LNA] [-Y TX_GAIN] [-p RX_PATH] [-P TX_PATH] [-u RX_GAIN_PGA] [-U RX_GAIN_VGA]"
                    "\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    start_tx_delay = samples;

    //usdrlog_setlevel(NULL, USDR_LOG_CRITICAL_WARNING);
    usdrlog_setlevel(NULL, loglevel);
    usdrlog_enablecolorize(NULL);

    if (listdevs) {
        char buffer[4096];
        int count = usdr_dmd_discovery(device_name, sizeof(buffer), buffer);

        fprintf(stderr, "Enumerated devices %d:\n%s\n",
                count, buffer);

        return 0;
    }

    rx_bufcnt = 0;
    tx_bufcnt = 0;

    if (dotx) {
        s_in_file[0] = fopen(infilename, "rb+");
        if (!s_in_file[0]) {
            fprintf(stderr, "Unable to create data file '%s'\n", filename);
            return 3;
        }

        if (dev_data[DD_TX_BANDWIDTH].ignore) {
            dev_data[DD_TX_BANDWIDTH].ignore = false;
            dev_data[DD_TX_BANDWIDTH].value = rate;
        }
    }
    if (dorx) {
        s_out_file[0] = fopen(filename, "wb+c");
        if (!s_out_file[0]) {
            fprintf(stderr, "Unable to create data file '%s'\n", filename);
            return 3;
        }

        if (dev_data[DD_RX_BANDWIDTH].ignore) {
            dev_data[DD_RX_BANDWIDTH].ignore = false;
            dev_data[DD_RX_BANDWIDTH].value = rate;
        }
    }

    res = usdr_dmd_create_string(device_name, &dev);
    if (res) {
        fprintf(stderr, "Unable to create device: errno %d\n", res);
        return 1;
    }

    res = usdr_dme_get_u32(dev, "/ll/devices", (unsigned*)&devices);
    if (res) {
        fprintf(stderr, "Defaulting devices to 1\n");
    } else {
        fprintf(stderr, "Devices in the array: %d\n", devices);
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

    if (refclkpath) {
        res = usdr_dme_set_string(dev, "/dm/sdr/refclk/path", refclkpath);
    }

    if (!noinit) {
        res = usdr_dme_set_uint(dev, "/dm/power/en", 1);
        if (res) {
            fprintf(stderr, "Unable to set power: errno %d\n", res);
            // goto dev_close;
        }

        res = usdr_dmr_rate_set(dev, NULL, rate);
        if (res) {
            fprintf(stderr, "Unable to set device rate: errno %d\n", res);
            if (stop_on_error) goto dev_close;
        }

        usleep(5000);
        if (lmlcfg != 0) {
            fprintf(stderr, "======================= setting LML mode to %d =======================\n", lmlcfg);
        }
        res = usdr_dme_set_uint(dev, "/debug/hw/lms7002m/0/rxlml", lmlcfg);

        print_device_temperature(dev);
    }

    if (dorx) {
        res = usdr_dms_create_ex(dev, "/ll/srx/0", fmt, chmsk, samples, rxflags, &usds_rx);
        if (res) {
            fprintf(stderr, "Unable to initialize RX data stream: errno %d\n", res);
            if (stop_on_error) goto dev_close;
        }

        res = res ? res : usdr_dms_info(usds_rx, &snfo_rx);
        if (res) {
            fprintf(stderr, "Unable to get data stream info: errno %d\n", res);
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

    if (dotx) {
        res = usdr_dms_create(dev, "/ll/stx/0", fmt, chmsk, 0 * samples, &usds_tx);
        if (res) {
            fprintf(stderr, "Unable to initialize TX data stream: errno %d\n", res);
            if (stop_on_error) goto dev_close;
        }

        res = res ? res : usdr_dms_info(usds_tx, &snfo_tx);
        if (res) {
            fprintf(stderr, "Unable to get data stream info: errno %d\n", res);
            if (stop_on_error) goto dev_close;
            snfo_tx.channels = 0;
            snfo_tx.pktsyms = 0;
        } else {
            s_tx_blksz = snfo_tx.pktbszie;
            s_tx_blksampl = snfo_tx.pktsyms;
            tx_bufcnt = snfo_tx.channels;
        }
    }


    fprintf(stderr, "Configured RX %d (%d bytes) x %d buffs  TX %d x %d buffs  ===  CH_MASK %x FMT %s\n",
            s_rx_blksampl, s_rx_blksz, rx_bufcnt, s_tx_blksz, tx_bufcnt, chmsk, fmt);
    if (rx_bufcnt > MAX_CHS || tx_bufcnt > MAX_CHS) {
        fprintf(stderr, "Too many requested channels %d/%d (MAX: %d)\n", rx_bufcnt, tx_bufcnt, MAX_CHS);
        if (stop_on_error) goto dev_close;
    }

    tx_get_samples = samples;
 //   if (samples * 8 > s_tx_blksz)
  //      goto dev_close;

    if (dotx) {
        for (unsigned i = 0; i < tx_bufcnt; i++) {
            tbuff[i] = ring_buffer_create(256, snfo_tx.pktbszie);
            res = pthread_create(&rthread[i], NULL,
                                 (strcmp(fmt, "ci16") == 0) ? freq_gen_thread_ci16 : freq_gen_thread_cf32,
                                 (void*)(intptr_t)i);
            if (res) {
                fprintf(stderr, "Unable start disk in thread %d: errno %d\n", i, res);
                goto dev_close;
            }
        }
    }
    if (dorx) {
        for (unsigned f = 1; f < rx_bufcnt; f++) {
            char fmod[1024];
            snprintf(fmod, sizeof(fmod), "%s.%d", filename, f);

            s_out_file[f] = fopen(fmod, "wb+c");
            if (!s_out_file[f]) {
                fprintf(stderr, "Unable to create data file '%s'\n", fmod);
                return 3;
            }
        }

        for (unsigned i = 0; i < rx_bufcnt; i++) {
            rbuff[i] = ring_buffer_create(256, snfo_rx.pktbszie);
            res = pthread_create(&wthread[i], NULL, disk_write_thread, (void*)(intptr_t)i);
            if (res) {
                fprintf(stderr, "Unable start thread %d: errno %d\n", i, res);
                goto dev_close;
            }
        }
    }

    signal(SIGINT, on_stop);
/*
    res = usdr_dms_op(usds, USDR_DMS_START, 0);
    if (res) {
        fprintf(stderr, "Unable to start data stream: errno %d\n", res);
        goto dev_close;
    }
    */

    usleep(10000);
    usdr_dme_get_uint(dev, "/dm/debug/all", temp);
    usleep(1000);

    if (cal_freq > 1e6) {
        res = usdr_dme_set_uint(dev, "/dm/sync/cal/freq", (unsigned)(cal_freq));
        if (res) {
            fprintf(stderr, "Unable to set calibration frequency: errno %d\n", res);
        }
    }

    strms[1] = usds_tx;
    strms[0] = usds_rx;
    res = usdr_dms_sync(dev, "off", 2, strms);
    if (res) {
        fprintf(stderr, "Unable to sync data streams: errno %d\n", res);
        if (stop_on_error) goto dev_close;
    }

    if (dorx) {
        res = usds_rx ? usdr_dms_op(usds_rx, USDR_DMS_START, 0) : -EPROTONOSUPPORT;
        if (res) {
            fprintf(stderr, "Unable to start data stream: errno %d\n", res);
            if (stop_on_error) goto dev_close;
        }
    }
    if (dotx) {
        res = usds_tx ? usdr_dms_op(usds_tx, USDR_DMS_START, 0) : -EPROTONOSUPPORT;
        if (res) {
            fprintf(stderr, "Unable to start data stream: errno %d\n", res);
            if (stop_on_error) goto dev_close;
        }
    }

    res = usdr_dms_sync(dev, synctype, 2, strms);
    if (res) {
        fprintf(stderr, "Unable to sync data streams: errno %d\n", res);
        if (stop_on_error) goto dev_close;
    }


    res = usdr_dme_set_uint(dev, "/dm/sdr/0/tfe/antcfg", antennacfg);
    if (res) {
        fprintf(stderr, "Unable to set antcfg: errno %d\n", res);
        // goto dev_close;
    }


    // configure here
    if (!noinit) {
        res = usdr_dme_findsetv_uint(dev, "/dm/sdr/0/", SIZEOF_ARRAY(dev_data), dev_data);
        if (res) {
            fprintf(stderr, "Unable to set device parameters: errno %d\n", res);
            if (stop_on_error) goto dev_close;
        }
    }

    uint64_t stm = start_tx_delay;

    if (dotx && !usds_tx) {
        goto dev_close;
    }
    if (dorx && !usds_rx) {
        goto dev_close;
    }

    if (dotx && !dorx) for (unsigned i = 0; !s_stop && (i < count); i++) {
         void* buffers[MAX_CHS];
         for (unsigned b = 0; b < tx_bufcnt; b++) {
             unsigned idx = ring_buffer_cwait(tbuff[b], 1000000);
             if (idx == IDX_TIMEDOUT) {
                 fprintf(stderr, "Cbuffer[%d] timed out!\n", b);
             }
             buffers[b] = ring_buffer_at(tbuff[b], idx);
         }

         res = usdr_dms_send(usds_tx, (const void**)buffers, samples, nots ? UINT64_MAX : stm, 32250);
         if (res) {
             fprintf(stderr, "Unable to send data: errno %d, i = %d\n", res, i);
             //goto dev_close;
             goto stop;
         }

         for (unsigned b = 0; b < tx_bufcnt; b++) {
             ring_buffer_cpost(tbuff[b]);
         }

         stm += samples;// - 40;
         if (i % 2 == 1) {
             //stm += 2 * 40;
         }

    } else if (dorx && !dotx) for (unsigned i = 0; !s_stop && (i < count); i++) {
        void* buffers[MAX_CHS];
        for (unsigned b = 0; b < rx_bufcnt; b++) {
            unsigned idx = ring_buffer_pwait(rbuff[b], 1000000);
            if (idx == IDX_TIMEDOUT) {
                fprintf(stderr, "Pbuffer[%d] timed out!\n", b);
            }
            buffers[b] = ring_buffer_at(rbuff[b], idx);
        }

        res = usdr_dms_recv(usds_rx, buffers, 2250, NULL);
        if (res) {
            fprintf(stderr, "Unable to recv data: errno %d, i = %d\n", res, i);
            //goto dev_close;
            goto stop;
        }

        for (unsigned b = 0; b < rx_bufcnt; b++) {
            ring_buffer_ppost(rbuff[b]);
        }

        if (i == resync)
            usdr_dme_set_uint(dev, "/dm/resync", 0);

    } else {
        //TX & RX
        void* rx_buffers[MAX_CHS];
        void* tx_buffers[MAX_CHS];
        uint64_t ts = start_tx_delay;

        for (unsigned i = 0; !s_stop && (i < count); i++) {
            // TX
            for (unsigned b = 0; b < tx_bufcnt; b++) {
                unsigned idx = ring_buffer_cwait(tbuff[b], 1000000);
                if (idx == IDX_TIMEDOUT) {
                    fprintf(stderr, "Cbuffer[%d] timed out!\n", b);
                }
                tx_buffers[b] = ring_buffer_at(tbuff[b], idx);

                uint64_t *x = (uint64_t *)(tx_buffers[b]);
                fprintf(stderr, "%016" PRIx64 ".%016" PRIx64 ".%016" PRIx64 ".%016" PRIx64 "\n", x[0], x[1], x[2], x[3]);
            }

            res = usdr_dms_send(usds_tx, (const void**)tx_buffers, samples, nots ? ~0ull : ts, 15250);
            if (res) {
                fprintf(stderr, "Unable to send data: errno %d, i = %d\n", res, i);
                //goto dev_close;
                goto stop;
            }

            for (unsigned b = 0; b < tx_bufcnt; b++) {
                ring_buffer_cpost(tbuff[b]);
            }

            ts += samples;

            //RX
            for (unsigned b = 0; b < rx_bufcnt; b++) {
                unsigned idx = ring_buffer_pwait(rbuff[b], 1000000);
                if (idx == IDX_TIMEDOUT) {
                    fprintf(stderr, "Pbuffer[%d] timed out!\n", b);
                }
                rx_buffers[b] = ring_buffer_at(rbuff[b], idx);
            }

            res = usdr_dms_recv(usds_rx, rx_buffers, 2250, NULL);
            if (res) {
                fprintf(stderr, "Unable to recv data: errno %d, i = %d\n", res, i);
                //goto dev_close;
                goto stop;
            }

            for (unsigned b = 0; b < rx_bufcnt; b++) {
                ring_buffer_ppost(rbuff[b]);
            }
        }
    }

stop:
    usdr_dme_get_uint(dev, "/dm/debug/rxtime", temp);
    usdr_dme_get_uint(dev, "/dm/debug/rxtime", temp);
    //usdr_dme_get_uint(dev, usdr_dmd_find_entity(dev, "/dm/debug/rxtime"), temp);

    if (dorx) {
        res = usdr_dms_op(usds_rx, USDR_DMS_STOP, 0);
        if (res) {
            fprintf(stderr, "Unable to stop data stream: errno %d\n", res);
            goto dev_close;
        }
    }
    if (dotx) {
        res = usdr_dms_op(usds_tx, USDR_DMS_STOP, 0);
        if (res) {
            fprintf(stderr, "Unable to stop data stream: errno %d\n", res);
            goto dev_close;
        }
    }

    thread_stop = true;

    res = usdr_dme_get_uint(dev, "/dm/debug/all", temp);
    if (res) {
        fprintf(stderr, "Unable to get device debug data: errno %d\n", res);
        goto dev_close;
    }

    if (!print_device_temperature(dev)) {
        //goto dev_close;
    }

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
    if (strms[1]) usdr_dms_destroy(strms[1]);
    if (strms[0]) usdr_dms_destroy(strms[0]);
    usdr_dmd_close(dev);
    return res;
}
