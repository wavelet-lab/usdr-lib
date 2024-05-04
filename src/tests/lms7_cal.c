// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#define _GNU_SOURCE
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#define USDR

void calc_vco_iq(int32_t phase, int16_t* i, int16_t* q)
{
    float p = -M_PI * (double)phase / (double)INT32_MIN;
    float c, s;
    sincosf(p, &s, &c);
    *i = c * INT16_MAX;
    *q = s * INT16_MAX;
}

int32_t do_shift_up(int32_t inphase, int32_t delta, int16_t* iqbuf, unsigned csamples)
{
    int32_t phase = inphase;
    for (unsigned n = 0; n < csamples; n++, phase += delta) {
        int16_t vcoi, vcoq;
        int16_t i = iqbuf[2*n], q = iqbuf[2*n + 1];
        int16_t oi, oq;
        calc_vco_iq(phase, &vcoi, &vcoq);
        // (oi, oq) = (vcoi, vcoq) * (i, q)
        oi = ((int32_t)vcoi * i - (int32_t)vcoq * q) >> 15;
        oq = ((int32_t)vcoi * q + (int32_t)vcoq * i) >> 15;
        iqbuf[2*n+0] = oi;
        iqbuf[2*n+1] = oq;
    }
    return phase;
}

float calc_tone_db(int16_t* iqbuf, unsigned csamples, int32_t freq)
{
    int32_t avgi = 0, avgq = 0;
    int32_t phase = 0;
    int16_t vcoi, vcoq;
    for (unsigned n = 0; n < csamples; n++, phase += freq) {
        int16_t i = iqbuf[2*n], q = iqbuf[2*n + 1];
        calc_vco_iq(phase, &vcoi, &vcoq);
        // (oi, oq) = (vcoi, vcoq) * (i, q)
        avgi += (((int32_t)vcoi * i - (int32_t)vcoq * q) >> 15);
        avgq += (((int32_t)vcoi * q + (int32_t)vcoq * i) >> 15);
    }

    float aai = (float)avgi / csamples / INT16_MAX;
    float aaq = (float)avgq / csamples / INT16_MAX;
    return 10 * log10f(aai * aai + aaq * aaq);
}

float calc_0_tone_db(int16_t* iqbuf, unsigned csamples)
{
    int32_t avgi = 0, avgq = 0;
    for (unsigned n = 0; n < csamples; n++) {
        int16_t i = iqbuf[2*n], q = iqbuf[2*n + 1];
        avgi += i;
        avgq += q;
    }

    float aai = (((float)avgi / csamples)) / INT16_MAX;
    float aaq = (((float)avgq / csamples)) / INT16_MAX;

    fprintf(stderr, "t0: %.3f %.3f\n", aai, aaq);
    return 10 * log10f(aai * aai + aaq * aaq);
}

typedef void* sdrdev_t;
typedef enum corr_type {
    RX_LO_CORR,
    TX_LO_CORR,
    RX_IMB_CORR,
    TX_IMB_CORR,
} corr_type_t;

typedef int (*func_apply_corr)(sdrdev_t dev, corr_type_t type, int32_t pi, int32_t pq);


// Calibration functions

int dev_initialize(const char* devstring, unsigned spb, unsigned loglevel, float rate, float freq, float freq_tx, sdrdev_t* odev);
int dev_uninitialize(sdrdev_t dev);
int dev_recv_burst(sdrdev_t dev, int16_t* data);

int dev_apply_corr(sdrdev_t dev, corr_type_t type, int32_t pi, int32_t pq);


#ifdef USDR
#include <dm_dev.h>
#include <dm_rate.h>
#include <dm_stream.h>
#include <usdr_logging.h>

typedef struct devinfo {
    pdm_dev_t dev;
    pusdr_dms_t usds;
    unsigned blksz;

    // Monitor file data
    FILE* f;
} devinfo_t;

int dev_initialize(const char* devstring, unsigned spb, unsigned loglevel, float rate, float freq, float freq_tx, sdrdev_t* odev)
{
    int res;
    pdm_dev_t dev;
    pusdr_dms_t usds;
    unsigned blksz, bufcnt;
    usdr_dms_nfo_t snfo;

    unsigned chmsk = 0x3;
    const char* fmt = "&i16";
    unsigned samples = spb;

    usdrlog_setlevel(NULL, loglevel);

    res = usdr_dmd_create_string("", &dev);
    if (res) {
        fprintf(stderr, "Unable to create device: errno %d\n", res);
        return 1;
    }

    res = usdr_dme_set_uint(dev, usdr_dmd_find_entity(dev, "/dm/power/en"), 1);
    if (res) {
        fprintf(stderr, "Unable to set power: errno %d\n", res);
        goto dev_close;
    }

    res = usdr_dmr_rate_set(dev, NULL, rate);
    if (res) {
        fprintf(stderr, "Unable to set device rate: errno %d\n", res);
        goto dev_close;
    }

 //   res = usdr_dme_set_uint(dev, usdr_dmd_find_entity(dev, "/dm/sdr/0/rx/bandwidth"), rate);
    if (res) {
        fprintf(stderr, "Unable to set rx frequency: errno %d\n", res);
        goto dev_close;
    }


    res = usdr_dms_create(dev, "/ll/srx/0", fmt, chmsk, samples, &usds);
    if (res) {
        fprintf(stderr, "Unable to initialize data stream: errno %d\n", res);
        goto dev_close;
    }

    res = usdr_dms_info(usds, &snfo);
    if (res) {
        fprintf(stderr, "Unable to get data stream info: errno %d\n", res);
        goto dev_close;
    }

    blksz = snfo.pktbszie;
    bufcnt = snfo.channels;

    fprintf(stderr, "Configured %d x %d buffs\n", blksz, bufcnt);

    res = usdr_dme_set_uint(dev, usdr_dmd_find_entity(dev, "/dm/sdr/0/rx/freqency"), freq);
    if (res) {
        fprintf(stderr, "Unable to set rx frequency: errno %d\n", res);
        goto dev_close;
    }

    if (freq_tx > 0) {
        res = usdr_dme_set_uint(dev, usdr_dmd_find_entity(dev, "/dm/sdr/0/tx/freqency"), freq_tx);
        if (res) {
            fprintf(stderr, "Unable to set tx frequency: errno %d\n", res);
            goto dev_close;
        }

        res = usdr_dme_set_uint(dev, usdr_dmd_find_entity(dev, "/dm/sdr/0/rx/path"), (uintptr_t)"rx_lb_auto");
        if (res) {
            fprintf(stderr, "Unable to set rx path: errno %d\n", res);
            goto dev_close;
        }

        res = usdr_dme_set_uint(dev, usdr_dmd_find_entity(dev, "/dm/sdr/0/tx/path"), (uintptr_t)"tx_auto");
        if (res) {
            fprintf(stderr, "Unable to set tx path: errno %d\n", res);
            goto dev_close;
        }

        res = usdr_dme_set_uint(dev, usdr_dmd_find_entity(dev, "/dm/sdr/0/rx/gain/lb"), 0);
        if (res) {
            fprintf(stderr, "Unable to set rx lb gain: errno %d\n", res);
            goto dev_close;
        }

        res = usdr_dme_set_uint(dev, usdr_dmd_find_entity(dev, "/dm/sdr/0/tx/bandwidth"), rate);
        if (res) {
            fprintf(stderr, "Unable to set tx bandwidth: errno %d\n", res);
            goto dev_close;
        }

        res = usdr_dme_set_uint(dev, usdr_dmd_find_entity(dev, "/dm/sdr/0/rx/bandwidth"), rate);
        if (res) {
            fprintf(stderr, "Unable to set tx bandwidth: errno %d\n", res);
            goto dev_close;
        }

        res = usdr_dme_set_uint(dev, usdr_dmd_find_entity(dev, "/dm/sdr/0/tx/gain"), 0);
        if (res) {
            fprintf(stderr, "Unable to set tx gain: errno %d\n", res);
            goto dev_close;
        }

        res = usdr_dme_set_uint(dev, usdr_dmd_find_entity(dev, "/dm/sdr/0/tx/gain/lb"), 0);
        if (res) {
            fprintf(stderr, "Unable to set tx gain: errno %d\n", res);
            goto dev_close;
        }
    }

    res = usdr_dms_op(usds, USDR_DMS_START, 0);
    if (res) {
        fprintf(stderr, "Unable to start data stream: errno %d\n", res);
        goto dev_close;
    }

    devinfo_t* di = (devinfo_t*)malloc(sizeof(devinfo_t));
    di->dev = dev;
    di->usds = usds;
    di->blksz = blksz;
    di->f = fopen("cal.dat", "wb+c");
    *odev = (sdrdev_t)di;
    return 0;

dev_close:
    usdr_dmd_close(dev);
    return res;
}

int dev_uninitialize(sdrdev_t dev)
{
    devinfo_t* di = (devinfo_t*)dev;

    usdr_dms_op(di->usds, USDR_DMS_STOP, 0);
    usdr_dmd_close(di->dev);

    if (di->f)
        fclose(di->f);

    free(di);
    return 0;
}

int dev_recv_burst(sdrdev_t dev, int16_t* data)
{
    int res;
    void* buffers[4] = { data, };
    devinfo_t* di = (devinfo_t*)dev;

    res = usdr_dms_recv(di->usds, buffers, 2250, NULL);
    if (res) {
        fprintf(stderr, "Unable to recv data: errno %d\n", res);
        //goto dev_close;

    }
    if (di->f) {
        fwrite(data, di->blksz, 1, di->f);
    }
    return res;
}

int dev_apply_corr(sdrdev_t dev, corr_type_t type, int32_t pi, int32_t pq)
{
    devinfo_t* di = (devinfo_t*)dev;
    const char* param;
    switch  (type) {
    case RX_LO_CORR: param = "/dm/sdr/0/rx/dccorr"; break;
    case TX_LO_CORR: param = "/dm/sdr/0/tx/dccorr"; break;
    case RX_IMB_CORR: param = "/dm/sdr/0/rx/phgaincorr"; break;
    case TX_IMB_CORR: param = "/dm/sdr/0/tx/phgaincorr"; break;
    default:
        return -EINVAL;
    }

    usdr_dme_set_uint(di->dev, usdr_dmd_find_entity(di->dev, param),
                      (1ull << 32) | (((uint32_t)(pi & 0xffff)) << 16) | (pq & 0xffff));

    return usdr_dme_set_uint(di->dev, usdr_dmd_find_entity(di->dev, param),
                      (2ull << 32) | (((uint32_t)(pi & 0xffff)) << 16) | (pq & 0xffff));
}

#else

#endif

typedef int (*meas_func_t)(intptr_t param, float* res);
struct correction_options
{
    meas_func_t func;
    intptr_t param;
    int range;
    int verbose;

    int best_i;
    int best_q;
};

int find_best_correction(sdrdev_t dev, corr_type_t type, struct correction_options* pco)
{
    int initial_m = pco->range;
    int i, q;
    int best_i = 0, best_q = 0;
    int nbest_i = 0, nbest_q = 0;
    int res;

    float best_meas = 0;
    int iixds[] = {initial_m, 10, 6, 4};

    for (int iteration = 0; iteration < 4; iteration++) {
        int step = iixds[iteration];
        // iterate i
        // iterate q

        for (i = best_i - step; i <= best_i + step; i++) {
            res = dev_apply_corr(dev, type, i, best_q);
            if (res < 0)
                return res;

            float meas;
            res = pco->func(pco->param, &meas);
            if (res < 0)
                return res;

            if (pco->verbose >= 2) {
                fprintf(stderr, "MEAS %8.3f n=%d i=%d q=%d\n",
                        meas, iteration, i, best_q);
            }

            if (meas < best_meas) {
                best_meas = meas;
                nbest_i = i;
            }
        }
        best_i = nbest_i;

        for (q = best_q - step; q <= best_q + step; q++) {
            res = dev_apply_corr(dev, type, best_i, q);
            if (res < 0)
                return res;

            float meas;
            res = pco->func(pco->param, &meas);
            if (res < 0)
                return res;

            if (pco->verbose >= 2) {
                fprintf(stderr, "MEAS %8.3f n=%d i=%d q=%d\n",
                        meas, iteration, best_i, q);
            }

            if (meas < best_meas) {
                best_meas = meas;
                nbest_q = q;
            }
        }
        best_q = nbest_q;

        if (pco->verbose >= 1) {
            fprintf(stderr, "Best correction %d for i=%d q=%d meas=%.3f\n", iteration,
                    best_i, best_q, best_meas);
        }
    }

    pco->best_i = best_i;
    pco->best_q = best_q;
    //fprintf(stderr, "Best correction for i=%d q=%d meas=%.3f\n",
    //        best_i, best_q, best_meas);
    return 0;
}

struct meas_data {
    sdrdev_t d;
    int16_t* pbuffer;
    unsigned nsmaples;
    int32_t phase;
};

int meas_calc_tone(intptr_t param, float* meas)
{
    struct meas_data* md = (struct meas_data*)param;
    int res;
    int iters;
    float m = 0;

    // skip first
    for (iters = 0; iters < 8; iters++) {
        res = dev_recv_burst(md->d, md->pbuffer);
        if (res) {
            return res;
        }
        if (iters > 0)
            m += calc_tone_db(md->pbuffer, md->nsmaples, md->phase);
    }

    *meas = m / (iters - 1);
    return 0;
}

int main(int argc, char** argv)
{
    unsigned samples = 16384;
    unsigned loglevel = 3;
    float rate = 4e6;
    float freq = 933.92e6;
    //float tx_freq = freq + 0.5e6;
    const char* connstring = "";
    int opt;
    float gain = 0;
    int verbose = 0;
    corr_type_t atest;

    while ((opt = getopt(argc, argv, "f:r:g:v:d:")) != -1) {
        switch (opt) {
        case 'f':
            freq = atof(optarg);
            break;
        case 'r':
            rate = atof(optarg);
            break;
        case 'g':
            gain = atof(optarg);
            break;
        case 'v':
            verbose = atoi(optarg);
            break;
        case 'd':
            connstring = optarg;
            break;

        default: /* '?' */
            fprintf(stderr, "Usage: %s [-f freq] [-r rate] test_name\n",
                    argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Expected test_name after options\n");
        exit(EXIT_FAILURE);
    }

    float tx_freq = freq + rate / 8; //CHECKME!!!!

    int res;
    sdrdev_t d;
    int16_t buffer[16384 * 2];
    struct meas_data md;
    md.d = d;
    md.phase = 0;
    md.pbuffer = buffer;
    md.nsmaples = samples;

    struct correction_options co;
    co.func = meas_calc_tone;
    co.param = (intptr_t)&md;
    co.range = 63;
    co.verbose = verbose;

    if (strcasecmp(argv[optind], "rxlo") == 0) {
        atest = RX_LO_CORR;
    } else if (strcasecmp(argv[optind], "txlo") == 0) {
        atest = TX_LO_CORR;

        co.range = 255;
        md.phase = -0.5 * INT32_MAX;
    } else {
        fprintf(stderr, "Unknown calibration test '%s'\n", argv[optind]);
        exit(EXIT_FAILURE);
    }

    res = dev_initialize(connstring, samples, loglevel, rate, freq, tx_freq, &d);
    if (res)
        return res;

    res = find_best_correction(d, atest, &co);
    if (res) {
        fprintf(stderr, "%s Optimization error: %d\n", argv[optind], res);
        goto stopp;
    }

    fprintf(stderr, "%s Optimization found!\nI=%d Q=%d / FREQ=%.3f\n", argv[optind],
            co.best_i, co.best_q, freq);

stopp:
    dev_uninitialize(d);
    return 0;
}





/*
unsigned i;
for (i = 0; i < 75; i++) {
    // TODO
    res = dev_recv_burst(d, buffer);
    if (res) {
        goto stopp;
    }

    float level = calc_0_tone_db(buffer, samples);
    float lev2 = calc_tone_db(buffer, samples, -0.125 * INT32_MAX);
    fprintf(stderr, "Iteration %d: level %.3f  %.3f   %d  %d\n", i, level, lev2,
            buffer[0], buffer[1]);
}



#if 0
    res = find_best_correction(d, RX_LO_CORR, &co);
    if (res) {
        fprintf(stderr, "RXLO Optimization error: %d\n", res);
        goto stopp;
    }

    fprintf(stderr, "RXLO Optimization found!\n");
#endif

    //TX dc correction
    co.range = 255;
    md.phase = -0.5 * INT32_MAX;
    res = find_best_correction(d, TX_LO_CORR, &co);
    if (res) {
        fprintf(stderr, "TXLO Optimization error: %d\n", res);
        goto stopp;
    }

    fprintf(stderr, "TXLO Optimization found!\n");
*/
