// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef SIMPLEAPI_H
#define SIMPLEAPI_H

#include <usdr_logging.h>
#include <dm_dev.h>
#include <dm_rate.h>
#include <dm_stream.h>


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
void SetRxLbGain(sdr_data_t* pdev, int gain);

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

#endif
