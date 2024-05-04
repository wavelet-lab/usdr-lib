// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <SoapySDR/Version.h>
#include <stdio.h> //printf
#include <stdlib.h> //free
#include <complex.h>

int main(void)
{
    size_t length;

    //enumerate devices
    SoapySDRKwargs *results = SoapySDRDevice_enumerate(NULL, &length);
    for (size_t i = 0; i < length; i++)
    {
        printf("Found device #%d: ", (int)i);
        for (size_t j = 0; j < results[i].size; j++)
        {
            printf("%s=%s, ", results[i].keys[j], results[i].vals[j]);
        }
        printf("\n");
    }
    SoapySDRKwargsList_clear(results, length);

    if (length == 0) {
        printf("No device were found!\n");
        exit(1);
    }

    //create device instance
    //args can be user defined or from the enumeration result
    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "usdr");
    SoapySDRKwargs_set(&args, "loglvl", "2");
    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);

    if (sdr == NULL) {
       printf("Unable to create USDR device!\n");
       exit(1);
    }

    //query device info
    char** names = SoapySDRDevice_listAntennas(sdr, SOAPY_SDR_RX, 0, &length);
    printf("Rx antennas: ");
    for (size_t i = 0; i < length; i++) printf("%s, ", names[i]);
    printf("\n");
    SoapySDRStrings_clear(&names, length);

    names = SoapySDRDevice_listGains(sdr, SOAPY_SDR_RX, 0, &length);
    printf("Rx gains: ");
    for (size_t i = 0; i < length; i++) printf("%s, ", names[i]);
    printf("\n");
    SoapySDRStrings_clear(&names, length);

    SoapySDRRange *ranges = SoapySDRDevice_getFrequencyRange(sdr, SOAPY_SDR_RX, 0, &length);
    printf("Rx freq ranges: ");
    for (size_t i = 0; i < length; i++) {
        printf("[%g Hz -> %g Hz], ", ranges[i].minimum, ranges[i].maximum);
    }
    printf("\n");
    free(ranges);

    //apply settings
    if (SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, 1e6) != 0) {
        printf("setSampleRate fail: %s\n", SoapySDRDevice_lastError());
    }
    if (SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, 912.3e6, NULL) != 0) {
        printf("setFrequency fail: %s\n", SoapySDRDevice_lastError());
    }

    ranges = SoapySDRDevice_getFrequencyRangeComponent(sdr, SOAPY_SDR_RX, 0, "BB", &length);
    printf("Rx BB freq ranges: ");
    for (size_t i = 0; i < length; i++) {
        printf("[%g Hz -> %g Hz], ", ranges[i].minimum, ranges[i].maximum);
    }
    printf("\n");
    free(ranges);

    //setup a stream (complex floats)
    SoapySDRStream *rxStream;
#if (SOAPY_SDR_API_VERSION < 0x00080000)
    if (SoapySDRDevice_setupStream(sdr, &rxStream, SOAPY_SDR_RX, SOAPY_SDR_CF32, NULL, 0, NULL) != 0) {
#else
    if ((rxStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CF32, NULL, 0, NULL)) != NULL) {
#endif
        printf("setupStream fail: %s\n", SoapySDRDevice_lastError());
    }
    SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0); //start streaming

    //create a re-usable buffer for rx samples
    complex float buff[4096];

    //receive some samples
    for (size_t i = 0; i < 10; i++) {
        void* buffs[] = {buff}; //array of buffers
        int flags; //flags set by receive operation
        long long timeNs; //timestamp for receive buffer
        int ret = SoapySDRDevice_readStream(sdr, rxStream, buffs, 4096, &flags, &timeNs, 100000);
        printf("ret=%d, flags=%d, timeNs=%lld\n", ret, flags, timeNs);
    }

    //shutdown the stream
    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0); //stop streaming
    SoapySDRDevice_closeStream(sdr, rxStream);

    char** sensors = SoapySDRDevice_listSensors(sdr, &length);
    for (size_t i = 0; i < length; i++)  {
        SoapySDRArgInfo nfo = SoapySDRDevice_getSensorInfo(sdr, sensors[i]);

        printf("Sensor %d [%-16s]: %-24s (%-48s): %s\n", (int)i, nfo.key, nfo.name, nfo.description,
               SoapySDRDevice_readSensor(sdr, nfo.key));
    }

    //cleanup device handle
    SoapySDRDevice_unmake(sdr);

    printf("Done\n");
    return EXIT_SUCCESS;
}
