// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include <math.h>
#include <string.h>

#include "simpleapi.h"

static volatile bool s_stop = false;
void on_stop(UNUSED int signo)
{
    if (s_stop)
        exit(1);

    s_stop = true;
}

int di[10000];
int dq[10000];


int main(int argc, char** argv)
{
    struct sdr_data data;
    int loglevel = 3;

    const char* device = "";
    unsigned packet_size = 4096;
    unsigned channels = 2;

    const char* format = "ci16";

    int lna_gain = 15;
    int pga_gain = 15;
    int pad_gain = 15;


    unsigned smaplerate_rx = 13e6 / 1;
    unsigned smaplerate_tx = 13e6 / 1;
    unsigned rx_bandwidth = smaplerate_rx;
    unsigned tx_bandwidth = smaplerate_tx;

    unsigned start_freq = 100e6;
    unsigned stop_freq = 3800e6;
    unsigned step = 10e6;

    unsigned ch = 0;
    const char* outfile = "cal.csv";
    int opt;
    int lb_gain = 0;

    int dsp_rx = 0;
    int dsp_tx = 0;

    while ((opt = getopt(argc, argv, "c:D:e:E:s:W:Y:r:l:o:b:m:M:")) != -1) {
        switch (opt) {
        case 'b': lb_gain = atoi(optarg); break;
        case 'c': ch = atoi(optarg); break;
        case 'D': device = optarg; break;
        case 'e': start_freq = atof(optarg); break;
        case 'E': stop_freq = atof(optarg); break;
        case 's': step = atof(optarg);
        case 'W': tx_bandwidth = atof(optarg); break;
        case 'Y': pad_gain = atoi(optarg); break;
        case 'r': rx_bandwidth = smaplerate_rx = smaplerate_tx = atof(optarg); break;
        case 'l': loglevel = atoi(optarg); break;
        case 'o': outfile = optarg; break;
        case 'm': dsp_rx = atof(optarg); break;
        case 'M': dsp_tx = atof(optarg); break;
        default:
            fprintf(stderr, "Usage: %s '[-D device] [-r samplerate] [-l loglevel] [-e START] [-E STOP] [-s STEP] [-W BW] [-Y GAIN] [-b RX_LB_GAIN] [-m DSP_RX] [-M DSP_TX] [-o out.csv]\n",
                    argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    fprintf(stderr, "Calibration rate=%.1f range [%.1f .. %.1f] step %.1f channel %d, bw %.1f\n",
            smaplerate_rx / 1.0e6, start_freq / 1.0e6, stop_freq / 1.0e6, step / 1.0e6, ch, tx_bandwidth / 1.0e6);

    Initialize(device, loglevel, smaplerate_rx, smaplerate_tx, format, packet_size, channels, &data);

    SetTxGain(&data, pad_gain);
    SetRxLnaGain(&data, lna_gain);
    SetRxPgaGain(&data, pga_gain);
    SetRxLbGain(&data, lb_gain);


    SetTxBandwidth(&data, tx_bandwidth);
    SetRxBandwidth(&data, rx_bandwidth);

    SetTxFreq(&data, start_freq);
    SetRxFreq(&data, start_freq);

    SetBBRxFreq(&data, 1, dsp_rx); SetBBRxFreq(&data, 2, dsp_rx);
    SetBBTxFreq(&data, 1, dsp_tx); SetBBTxFreq(&data, 2, dsp_tx);

    Start(&data);

    // Warmup
    sleep(1);

    signal(SIGINT, on_stop);

    unsigned tx_freq;
    unsigned j = 0;
    for (tx_freq = start_freq; tx_freq < stop_freq; tx_freq += step) {
        fprintf(stdout, "Tuning to %.3f Mhz\n", tx_freq / 1.0e6);
        SetTxFreq(&data, tx_freq);
        usleep(10000); // 10 ms


        Calibrate(&data, ch, TX_LO);
        GetTXLOCalData(&data, &di[j], &dq[j]);
        j++;

        if (s_stop)
            break;
    }


    for (unsigned i = 0; i < j; i++) {
        fprintf(stderr, "%4.3fMhz I=%+3d Q=%+3d\n", (start_freq + i * step) / 1.0e6, di[i], dq[i]);
    }

    if (outfile) {
        FILE* o = fopen(outfile, "w+c");
        if (o == NULL) {
            fprintf(stderr, "Unable to create CSV file!\n");
            return 1;
        }

        fprintf(o, "# %d, %d, %d, %d, %d, %d\n",
                start_freq, stop_freq, step, smaplerate_tx, tx_bandwidth, pad_gain);
        for (unsigned i = 0; i < j; i++) {
            fprintf(o, "%u, %d, %d\n", start_freq + i * step, di[i], dq[i]);
        }
        fclose(o);
    }

    return 0;
}
