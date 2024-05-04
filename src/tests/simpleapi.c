// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "simpleapi.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void CheckErrorAndDie(int err, const char* operation)
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
    usdrlog_enablecolorize(NULL);

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

void SetRxLbGain(sdr_data_t* pdev, int gain)
{
    int res;
    res = usdr_dme_set_uint(pdev->dev, "/dm/sdr/0/rx/gain/lb",
                            gain);
    CheckErrorAndDie(res, "SetRxLbGain");
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
