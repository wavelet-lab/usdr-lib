// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <dm_dev.h>
#include <dm_rate.h>
#include <dm_stream.h>

#include <usdr_logging.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include <math.h>
#include <string.h>

#include "simpleapi.h"

#if 0
struct sdr_data {
    pdm_dev_t dev;
    pusdr_dms_t strms[2]; // 0 - RX, 1 - TX
    usdr_dms_nfo_t snfo_rx;
    usdr_dms_nfo_t snfo_tx;
};
typedef struct sdr_data sdr_data_t;


void Initialize(const char* device_string, int loglevel,
                unsigned samplerate_rx,
                unsigned samplerate_tx,
                const char* format,
                unsigned samples_per_packet,
                unsigned channels,
                sdr_data_t* pdev);

void SetTxGain(sdr_data_t* pdev, int gain);
void SetRxLnaGain(sdr_data_t* pdev, int gain);
void SetRxPgaGain(sdr_data_t* pdev, int gain);
void SetRxTiaGain(sdr_data_t* pdev, int gain);

void SetTxFreq(sdr_data_t* pdev, unsigned freq);
void SetRxFreq(sdr_data_t* pdev, unsigned freq);

void SetBBTxFreq(sdr_data_t* pdev, int channel, int freq);
void SetBBRxFreq(sdr_data_t* pdev, int channel, int freq);

void SetTxBandwidth(sdr_data_t* pdev, unsigned freq);
void SetRxBandwidth(sdr_data_t* pdev, unsigned freq);

void TrimDacVCTCXO(sdr_data_t* pdev, uint16_t val);

void SetTXDCCorr(sdr_data_t* pdev, int channel, int i, int q);


// DO NOT CALL CALIBRATE AFTER START
enum calibrate_flags {
    RX_LO = 1,
    RX_IQIMB = 2,
    TX_LO = 4,
    TX_IQIMB = 8,
};

void Calibrate(sdr_data_t* pdev, unsigned chan, unsigned flags);
void GetTXLOCalData(sdr_data_t* pdev, int* ci, int *cq);

void Start(sdr_data_t* pdev);

void TransmitData(sdr_data_t* pdev, const void* ch1_data, const void* ch2_data,
                  unsigned samples, int64_t time);
void ReceiveData(sdr_data_t* pdev, void* ch1_data, void* ch2_data);

void Stop(sdr_data_t* pdev);

void Destroy(sdr_data_t* pdev);


void CheckErrorAndDie(int err, const char* operation)
{
    if (err < 0) {
        fprintf(stderr, "Unable to %s: error %d (%s)\n",
                operation, err, strerror(-err));
        exit(EXIT_FAILURE);
    }
}

void Initialize(const char* device_string, int loglevel,
                unsigned samplerate_rx,
                unsigned samplerate_tx,
                const char* format,
                unsigned samples_per_packet,
                unsigned channels,
                sdr_data_t* pdev)
{
    int res;
    unsigned chmsk = (channels == 1) ? 0x1 : (channels == 2) ? 0x3 : 0;
    unsigned rates[4] = { samplerate_rx, samplerate_tx, 0, 0 };
    usdrlog_setlevel(NULL, loglevel);

    res = usdr_dmd_create_string(device_string, &pdev->dev);
    CheckErrorAndDie(res, "initialize");

    res = usdr_dme_set_uint(pdev->dev, "/dm/power/en", 1);
    CheckErrorAndDie(res, "power on");

    res = usdr_dme_set_uint(pdev->dev, "/dm/rate/rxtxadcdac", (uintptr_t)&rates[0]);
    CheckErrorAndDie(res, "set samplerate");

    res = usdr_dms_create(pdev->dev, "/ll/stx/0", format, chmsk, 4096, &pdev->strms[1]);
    CheckErrorAndDie(res, "tx stream");

    res = usdr_dms_create_ex(pdev->dev, "/ll/srx/0", format, chmsk, samples_per_packet, 0, &pdev->strms[0]);
    CheckErrorAndDie(res, "rx stream");

    res = usdr_dms_info(pdev->strms[1], &pdev->snfo_tx);
    CheckErrorAndDie(res, "tx info");

    res = usdr_dms_info(pdev->strms[0], &pdev->snfo_rx);
    CheckErrorAndDie(res, "rx info");

    res = usdr_dms_sync(pdev->dev, "off", 2, pdev->strms);
    CheckErrorAndDie(res, "tx & rx syncronization off");

    res = usdr_dms_op(pdev->strms[0], USDR_DMS_START, 0);
    CheckErrorAndDie(res, "rx stream precharge");

    res = usdr_dms_op(pdev->strms[1], USDR_DMS_START, 0);
    CheckErrorAndDie(res, "tx stream precharge");
}

void Destroy(sdr_data_t* pdev)
{
    usdr_dmd_close(pdev->dev);
}

void Start(sdr_data_t* pdev)
{
    int res;

    res = usdr_dms_sync(pdev->dev, "none", 2, pdev->strms);
    CheckErrorAndDie(res, "start");
}

void Stop(sdr_data_t* pdev)
{
    int res;

    res = usdr_dms_op(pdev->strms[0], USDR_DMS_STOP, 0);
    CheckErrorAndDie(res, "rx stream stop");

    res = usdr_dms_op(pdev->strms[1], USDR_DMS_STOP, 0);
    CheckErrorAndDie(res, "tx stream stop");
}

void Calibrate(sdr_data_t* pdev, unsigned chan, unsigned flags)
{
    int res;
    const char* ctype;
    char cdata[16];

    switch (flags) {
    case RX_LO: ctype = "rxlo"; break;
    case TX_LO: ctype = "txlo"; break;
    case RX_IQIMB: ctype = "rxiqimb"; break;
    case TX_IQIMB: ctype = "rxiqimb"; break;
    case RX_LO | RX_IQIMB: ctype = "rx"; break;
    case TX_LO | TX_IQIMB: ctype = "tx"; break;
    case TX_LO | RX_LO: ctype = "lo"; break;
    default: ctype = "all"; break;
    }

    snprintf(cdata, sizeof(cdata), "%s:%s", chan == 1 ? "a" : "b", ctype);

    res = usdr_dme_set_uint(pdev->dev, "/dm/sdr/0/calibrate",
                            (uintptr_t)&cdata[0]);
    CheckErrorAndDie(res, "calibrate");
}

void GetTXLOCalData(sdr_data_t* pdev, int* ci, int *cq)
{
    int res;
    uint64_t ptr;
    int* cdata;
    res = usdr_dme_get_uint(pdev->dev, "/dm/sdr/0/calibrate", &ptr);
    CheckErrorAndDie(res, "get_calibrate");

    // Index in complex array
    // 0 - RXLO
    // 1 - TXLO
    // 2 - RXIQIMB
    // 3 - TXIQIMB

    cdata = (int*)(void*)ptr;
    *ci = cdata[2 * 1 + 0];
    *cq = cdata[2 * 1 + 1];
}

void TrimDacVCTCXO(sdr_data_t* pdev, uint16_t val)
{
    int res = usdr_dme_set_uint(pdev->dev, "/dm/sdr/0/dac_vctcxo", val);
    CheckErrorAndDie(res, "TrimDacVCTCXO");
}


void SetTxGain(sdr_data_t* pdev, int gain)
{
    int res;
    res = usdr_dme_set_uint(pdev->dev, "/dm/sdr/0/tx/gain",
                            gain);
    CheckErrorAndDie(res, "SetTxGain");
}
void SetRxLnaGain(sdr_data_t* pdev, int gain)
{
    int res;
    res = usdr_dme_set_uint(pdev->dev, "/dm/sdr/0/rx/gain/lna",
                            gain);
    CheckErrorAndDie(res, "SetRxLnaGain");
}
void SetRxTiaGain(sdr_data_t* pdev, int gain)
{
    int res;
    res = usdr_dme_set_uint(pdev->dev, "/dm/sdr/0/rx/gain/vga",
                            gain);
    CheckErrorAndDie(res, "SetRxTiaGain");
}
void SetRxPgaGain(sdr_data_t* pdev, int gain)
{
    int res;
    res = usdr_dme_set_uint(pdev->dev, "/dm/sdr/0/rx/gain/pga",
                            gain);
    CheckErrorAndDie(res, "SetRxPgaGain");
}

void SetTxFreq(sdr_data_t* pdev, unsigned freq)
{
    int res;
    res = usdr_dme_set_uint(pdev->dev, "/dm/sdr/0/tx/freqency",
                            freq);
    CheckErrorAndDie(res, "SetTxFreq");
}
void SetRxFreq(sdr_data_t* pdev, unsigned freq)
{
    int res;
    res = usdr_dme_set_uint(pdev->dev, "/dm/sdr/0/rx/freqency",
                            freq);
    CheckErrorAndDie(res, "SetRxFreq");
}

void SetBBTxFreq(sdr_data_t* pdev, int chan, int freq)
{
    int res;
    uint64_t val = (((uint64_t)chan) << 32) | (uint32_t)freq;
    res = usdr_dme_set_uint(pdev->dev, "/dm/sdr/0/tx/freqency/bb",
                            val);
    CheckErrorAndDie(res, "SetBBTxFreq");
}
void SetBBRxFreq(sdr_data_t* pdev, int chan, int freq)
{
    int res;
    uint64_t val = (((uint64_t)chan) << 32) | (uint32_t)freq;
    res = usdr_dme_set_uint(pdev->dev, "/dm/sdr/0/rx/freqency/bb",
                            val);
    CheckErrorAndDie(res, "SetBBRxFreq");
}

void SetTxBandwidth(sdr_data_t* pdev, unsigned freq)
{
    int res;
    res = usdr_dme_set_uint(pdev->dev, "/dm/sdr/0/tx/bandwidth",
                            freq);
    CheckErrorAndDie(res, "SetTxBandwidth");
}

void SetRxBandwidth(sdr_data_t* pdev, unsigned freq)
{
    int res;
    res = usdr_dme_set_uint(pdev->dev, "/dm/sdr/0/rx/bandwidth",
                            freq);
    CheckErrorAndDie(res, "SetRxBandwidth");
}

void SetTXDCCorr(sdr_data_t* pdev, int chan, int i, int q)
{
    uint64_t val = (((uint64_t)chan) << 32) | ((((uint64_t)i) & 0xffff) << 16) | ((((uint64_t)q) & 0xffff) << 0);
    int res;
    res = usdr_dme_set_uint(pdev->dev, "/dm/sdr/0/tx/dccorr",
                            val);
    CheckErrorAndDie(res, "SetTXDCCorr");
}

// set time to -1 for immediate send
// set ch2_data is ignored in single channel mode
void TransmitData(sdr_data_t* pdev, const void* ch1_data, const void* ch2_data,
                  unsigned samples, int64_t time)
{
    int res;
    const void* buffers[2] = {
        ch1_data,
        ch2_data
    };

    res = usdr_dms_send(pdev->strms[1], buffers, samples, time, 32250);
    CheckErrorAndDie(res, "TransmitData");
}

void ReceiveData(sdr_data_t* pdev, void* ch1_data, void* ch2_data)
{
    int res;
    void* buffers[2] = {
        ch1_data,
        ch2_data
    };

    res = usdr_dms_recv(pdev->strms[0], buffers, 2250, NULL);
    CheckErrorAndDie(res, "ReceiveData");
}

#endif

int16_t tmp_signal_i[8] = { 30000, 0, 0, -30000, -30000, 0, 0, 30000 };

//float tmp_signal_f[8] = { 0, 0.9, 0, 0, 0, -0.9, 0, 0 };

float tmp_signal_f[8] = { 0.9, 0, 0, -0.9, -0.9, 0, 0, 0.9 };
//float tmp_signal_f[8] = { 0.9, 0.9, 0.9, 0.9, 0.9, 0.9, 0.9, 0.9 };

#define SAMPLES_FLOAT

#ifdef SAMPLES_FLOAT
#define tmp_signal tmp_signal_f
#define FORMAT "cf32"
typedef float samples_t;
#else
#define tmp_signal tmp_signal_i
#define FORMAT "ci16"
typedef int16_t samples_t;
#endif

int main(int argc, char** argv)
{
    struct sdr_data data;
    int loglevel = 3;

    const char* device = argc > 1 ? argv[1] : "";
    unsigned packet_size = 4096;
    unsigned channels = 1;

    unsigned smaplerate_rx = 12e6 / 6;
    unsigned smaplerate_tx = 12e6 / 6;
    const char* format = FORMAT;
    unsigned rx_freq = 980e6;
    unsigned tx_freq = 720e6;


    unsigned tx_mask = 1; //3; //Send to both channels

    //   rx_freq = 2680e6;
 //   tx_freq = 2560e6;

    unsigned rx_bandwidth = 1e6;// smaplerate_rx;
    unsigned tx_bandwidth = 1e6;//smaplerate_tx;

    int lna_gain = 15;
    int pga_gain = 15;
    int pad_gain = 15; //25;
    int txi, txq;

    Initialize(device, loglevel, smaplerate_rx, smaplerate_tx, format, packet_size, channels, &data);

    SetTxGain(&data, pad_gain);
    SetRxLnaGain(&data, lna_gain);
    SetRxPgaGain(&data, pga_gain);

    SetTxFreq(&data, tx_freq);
    SetRxFreq(&data, rx_freq);

    SetTxBandwidth(&data, tx_bandwidth);
    SetRxBandwidth(&data, rx_bandwidth);

    // Optionall tune frequency offset
    // TrimDacVCTCXO(&data, 43981);

    // Do calibration only when All gains / freqs are set but before actual start
    samples_t dummy[2 * 65536];
    samples_t spkt[2 * 65536];
    samples_t rpkt_1[2 * 65536];
    samples_t rpkt_2[2 * 65536];

    memset(dummy, 0, sizeof(dummy));
    for (unsigned off = 0; off < sizeof(spkt); off += sizeof(tmp_signal)) {
        memcpy((void*)spkt + off, tmp_signal, sizeof(tmp_signal));
    }

    Start(&data);
    //Calibrate(&data, 1, TX_LO);

  //  GetTXLOCalData(&data,  &txi, &txq);
  //  fprintf(stderr, " Calibration TX i=%d q=%d\n\n\n", txi, txq);


    // Calibrate(&data, 2); when 2 channels are active

    // SetTXDCCorr(&data, 1, 237, -270);
    // SetBBTxFreq(&data, 1, -100e3);

    for (unsigned i = 0; i < 50000000; i++) {
        TransmitData(&data,
                     (tx_mask & 1) ? spkt : dummy,
                     (tx_mask & 2) ? spkt : dummy,
                     packet_size, -1);

        ReceiveData(&data, rpkt_1, rpkt_2);

    }


    Stop(&data);
    Destroy(&data);
    return 0;
}
