// Copyright (c) 2023-2026 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <SoapySDR/Constants.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Errors.h>
#include <SoapySDR/Formats.h>
#include <SoapySDR/Version.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "usdr_port.h"

#define MAX_CHANS       32
#define MAX_PACKETSIZE  (1024 * 1024)
#define STREAM_ALIGN    ((size_t)64)

static int fail(const char *what)
{
    printf("FAIL %s: %s\n", what, SoapySDRDevice_lastError());
    return EXIT_FAILURE;
}

static void usage(const char *argv0)
{
    printf("Usage: %s [-d bus] [-c channels] [-i packet_size] [-n reads] [-r rate] [-f freq] [-b bw] [-g gain] [-Q]\n", argv0);
    printf("  -d bus          USDR bus string, e.g. usb@3/3/6 or pci,device=/dev/usdr0\n");
    printf("  -c channels     RX channels to stream, default 1\n");
    printf("  -i samples      RX samples per read, default 4096\n");
    printf("  -n reads        RX reads, default 4\n");
    printf("  -r rate         Preferred sample rate, default 5e6\n");
    printf("  -f freq         Preferred RF frequency, default 912.3e6\n");
    printf("  -b bw           Preferred bandwidth, default 1e6\n");
    printf("  -g gain         Preferred gain, default 15\n");
    printf("  -Q              Query/control-plane only, skip RX stream\n");
}

static void print_kwargs(const SoapySDRKwargs *kwargs)
{
    for (size_t i = 0; i < kwargs->size; i++) {
        printf("%s=%s%s", kwargs->keys[i], kwargs->vals[i], (i + 1 == kwargs->size) ? "" : ", ");
    }
}

static void print_strings(const char *title, char **values, size_t length)
{
    printf("%s: ", title);
    for (size_t i = 0; i < length; i++) {
        printf("%s%s", values[i], (i + 1 == length) ? "" : ", ");
    }
    printf("\n");
}

static const char *first_manual_antenna(char **values, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        if (strcmp(values[i], "AUTO") != 0) {
            return values[i];
        }
    }
    return NULL;
}

static void print_ranges(const char *title, const SoapySDRRange *ranges, size_t length)
{
    printf("%s: ", title);
    for (size_t i = 0; i < length; i++) {
        printf("[%g, %g]%s", ranges[i].minimum, ranges[i].maximum, (i + 1 == length) ? "" : ", ");
    }
    printf("\n");
}

static double pick_in_ranges(const SoapySDRRange *ranges, size_t length, double preferred)
{
    if (length == 0) {
        return preferred;
    }
    for (size_t i = 0; i < length; i++) {
        if (preferred >= ranges[i].minimum && preferred <= ranges[i].maximum) {
            return preferred;
        }
    }
    if (ranges[0].maximum <= ranges[0].minimum) {
        return ranges[0].minimum;
    }
    return ranges[0].minimum + (ranges[0].maximum - ranges[0].minimum) / 4.0;
}

static double pick_in_range(SoapySDRRange range, double preferred)
{
    if (preferred >= range.minimum && preferred <= range.maximum) {
        return preferred;
    }
    if (range.maximum <= range.minimum) {
        return range.minimum;
    }
    return range.minimum + (range.maximum - range.minimum) / 2.0;
}

static void print_arg_infos(const char *title, SoapySDRArgInfo *infos, size_t length)
{
    printf("%s:\n", title);
    for (size_t i = 0; i < length; i++) {
        printf("  %-16s %-24s value=%s\n", infos[i].key, infos[i].name, infos[i].value);
    }
}

static int check_identification(SoapySDRDevice *sdr)
{
    char *driver = SoapySDRDevice_getDriverKey(sdr);
    if (SoapySDRDevice_lastStatus() != 0) return fail("getDriverKey");
    char *hardware = SoapySDRDevice_getHardwareKey(sdr);
    if (SoapySDRDevice_lastStatus() != 0) return fail("getHardwareKey");
    SoapySDRKwargs info = SoapySDRDevice_getHardwareInfo(sdr);
    if (SoapySDRDevice_lastStatus() != 0) return fail("getHardwareInfo");

    printf("Driver: %s\n", driver);
    printf("Hardware: %s\n", hardware);
    printf("Hardware info: ");
    print_kwargs(&info);
    printf("\n");

    free(driver);
    free(hardware);
    SoapySDRKwargs_clear(&info);
    return EXIT_SUCCESS;
}

static int check_global_api(SoapySDRDevice *sdr)
{
    size_t length = 0;

    char **strings = SoapySDRDevice_listClockSources(sdr, &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("listClockSources");
    print_strings("\nClock sources", strings, length);
    if (length > 0) {
        if (SoapySDRDevice_setClockSource(sdr, strings[0]) != 0) return fail("setClockSource");
        char *source = SoapySDRDevice_getClockSource(sdr);
        if (SoapySDRDevice_lastStatus() != 0) return fail("getClockSource");
        printf("Clock source read: %s\n", source);
        free(source);
    }
    SoapySDRStrings_clear(&strings, length);

    SoapySDRRange *ranges = SoapySDRDevice_getMasterClockRates(sdr, &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("getMasterClockRates");
    print_ranges("Master clock ranges", ranges, length);
    free(ranges);

    ranges = SoapySDRDevice_getReferenceClockRates(sdr, &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("getReferenceClockRates");
    print_ranges("Reference clock ranges", ranges, length);
    free(ranges);

    double ref_clock_rate = SoapySDRDevice_getReferenceClockRate(sdr);
    if (SoapySDRDevice_lastStatus() != 0) return fail("getReferenceClockRate");
    printf("Reference clock rate: %g\n", ref_clock_rate);
    if (ref_clock_rate > 0.0) {
        if (SoapySDRDevice_setReferenceClockRate(sdr, ref_clock_rate) != 0) return fail("setReferenceClockRate");
        printf("Reference clock set/read: requested=%g actual=%g\n",
               ref_clock_rate, SoapySDRDevice_getReferenceClockRate(sdr));
        if (SoapySDRDevice_lastStatus() != 0) return fail("getReferenceClockRate after set");
    } else {
        printf("Reference clock set/read: SKIP current rate is unavailable\n");
    }

    void *native_handle = SoapySDRDevice_getNativeDeviceHandle(sdr);
    if (SoapySDRDevice_lastStatus() != 0) return fail("getNativeDeviceHandle");
    printf("Native device handle: %p\n", native_handle);

    strings = SoapySDRDevice_listTimeSources(sdr, &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("listTimeSources");
    print_strings("Time sources", strings, length);
    SoapySDRStrings_clear(&strings, length);

    printf("Has hardware time: %s\n", SoapySDRDevice_hasHardwareTime(sdr, "") ? "true" : "false");
    printf("Hardware time: %lld\n", SoapySDRDevice_getHardwareTime(sdr, ""));

    strings = SoapySDRDevice_listSensors(sdr, &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("listSensors");
    for (size_t i = 0; i < length; i++) {
        SoapySDRArgInfo info = SoapySDRDevice_getSensorInfo(sdr, strings[i]);
        if (SoapySDRDevice_lastStatus() != 0) return fail("getSensorInfo");
        char *value = SoapySDRDevice_readSensor(sdr, strings[i]);
        if (SoapySDRDevice_lastStatus() != 0) return fail("readSensor");
        printf("Sensor %-16s %-24s value=%s\n", info.key, info.name, value);
        free(value);
        SoapySDRArgInfo_clear(&info);
    }
    SoapySDRStrings_clear(&strings, length);

    SoapySDRArgInfo *settings = SoapySDRDevice_getSettingInfo(sdr, &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("getSettingInfo");
    print_arg_infos("Settings", settings, length);
    SoapySDRArgInfoList_clear(settings, length);
    return EXIT_SUCCESS;
}

static int check_channel(SoapySDRDevice *sdr, int direction, const char *label, size_t channel,
                         double preferred_rate, double preferred_freq, double preferred_bw, double preferred_gain)
{
    size_t length = 0;
    printf("\n%s%zu\n", label, channel);

    SoapySDRKwargs chinfo = SoapySDRDevice_getChannelInfo(sdr, direction, channel);
    if (SoapySDRDevice_lastStatus() != 0) return fail("getChannelInfo");
    printf("  Channel info: ");
    print_kwargs(&chinfo);
    printf("\n");
    SoapySDRKwargs_clear(&chinfo);

    printf("  Full duplex: %s\n", SoapySDRDevice_getFullDuplex(sdr, direction, channel) ? "true" : "false");

    char **strings = SoapySDRDevice_listAntennas(sdr, direction, channel, &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("listAntennas");
    print_strings("  Antennas", strings, length);
    const char *test_antenna = first_manual_antenna(strings, length);
    if (test_antenna != NULL) {
        if (SoapySDRDevice_setAntenna(sdr, direction, channel, test_antenna) != 0) return fail("setAntenna");
        char *antenna = SoapySDRDevice_getAntenna(sdr, direction, channel);
        if (SoapySDRDevice_lastStatus() != 0) return fail("getAntenna");
        printf("  Antenna set/read: requested=%s actual=%s\n", test_antenna, antenna);
        free(antenna);
    }
    SoapySDRStrings_clear(&strings, length);

    strings = SoapySDRDevice_getStreamFormats(sdr, direction, channel, &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("getStreamFormats");
    print_strings("  Stream formats", strings, length);
    SoapySDRStrings_clear(&strings, length);

    double full_scale = 0.0;
    char *native_format = SoapySDRDevice_getNativeStreamFormat(sdr, direction, channel, &full_scale);
    if (SoapySDRDevice_lastStatus() != 0) return fail("getNativeStreamFormat");
    printf("  Native stream format: %s fullScale=%g\n", native_format, full_scale);
    free(native_format);

    SoapySDRArgInfo *arg_infos = SoapySDRDevice_getStreamArgsInfo(sdr, direction, channel, &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("getStreamArgsInfo");
    print_arg_infos("  Stream args", arg_infos, length);
    SoapySDRArgInfoList_clear(arg_infos, length);

    printf("  Has IQ balance mode: %s\n", SoapySDRDevice_hasIQBalanceMode(sdr, direction, channel) ? "true" : "false");
    if (SoapySDRDevice_lastStatus() != 0) return fail("hasIQBalanceMode");
    if (SoapySDRDevice_setIQBalanceMode(sdr, direction, channel, false) != 0) return fail("setIQBalanceMode(false)");
    printf("  IQ balance mode read: %s\n", SoapySDRDevice_getIQBalanceMode(sdr, direction, channel) ? "true" : "false");
    if (SoapySDRDevice_lastStatus() != 0) return fail("getIQBalanceMode");

    printf("  Has frequency correction: %s\n", SoapySDRDevice_hasFrequencyCorrection(sdr, direction, channel) ? "true" : "false");
    if (SoapySDRDevice_lastStatus() != 0) return fail("hasFrequencyCorrection");
    if (SoapySDRDevice_setFrequencyCorrection(sdr, direction, channel, 0.0) != 0) return fail("setFrequencyCorrection(0)");
    printf("  Frequency correction read: %g\n", SoapySDRDevice_getFrequencyCorrection(sdr, direction, channel));
    if (SoapySDRDevice_lastStatus() != 0) return fail("getFrequencyCorrection");

    strings = SoapySDRDevice_listFrequencies(sdr, direction, channel, &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("listFrequencies");
    print_strings("  Frequency components", strings, length);
    SoapySDRStrings_clear(&strings, length);

    SoapySDRRange *ranges = SoapySDRDevice_getFrequencyRange(sdr, direction, channel, &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("getFrequencyRange");
    print_ranges("  Frequency ranges", ranges, length);
    double freq = pick_in_ranges(ranges, length, preferred_freq);
    free(ranges);

    if (SoapySDRDevice_setFrequency(sdr, direction, channel, freq, NULL) != 0) return fail("setFrequency");
    printf("  Frequency set/read: requested=%g actual=%g\n", freq, SoapySDRDevice_getFrequency(sdr, direction, channel));

    ranges = SoapySDRDevice_getFrequencyRangeComponent(sdr, direction, channel, "RF", &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("getFrequencyRangeComponent(RF)");
    print_ranges("  RF frequency ranges", ranges, length);
    free(ranges);

    ranges = SoapySDRDevice_getFrequencyRangeComponent(sdr, direction, channel, "BB", &length);
    if (SoapySDRDevice_lastStatus() == 0) {
        print_ranges("  BB frequency ranges", ranges, length);
        free(ranges);
    }

    ranges = SoapySDRDevice_getSampleRateRange(sdr, direction, channel, &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("getSampleRateRange");
    print_ranges("  Sample rate ranges", ranges, length);
    double rate = pick_in_ranges(ranges, length, preferred_rate);
    free(ranges);

    if (SoapySDRDevice_setSampleRate(sdr, direction, channel, rate) != 0) return fail("setSampleRate");
    printf("  Sample rate set/read: requested=%g actual=%g\n", rate, SoapySDRDevice_getSampleRate(sdr, direction, channel));

    double *rates = SoapySDRDevice_listSampleRates(sdr, direction, channel, &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("listSampleRates");
    printf("  Listed sample rates: %zu entries\n", length);
    free(rates);

    ranges = SoapySDRDevice_getBandwidthRange(sdr, direction, channel, &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("getBandwidthRange");
    print_ranges("  Bandwidth ranges", ranges, length);
    double bw = pick_in_ranges(ranges, length, preferred_bw);
    free(ranges);

    if (bw > 0.0) {
        if (SoapySDRDevice_setBandwidth(sdr, direction, channel, bw) != 0) return fail("setBandwidth");
        printf("  Bandwidth set/read: requested=%g actual=%g\n", bw, SoapySDRDevice_getBandwidth(sdr, direction, channel));
    }
    double *bandwidths = SoapySDRDevice_listBandwidths(sdr, direction, channel, &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("listBandwidths");
    printf("  Listed bandwidths: %zu entries\n", length);
    free(bandwidths);

    strings = SoapySDRDevice_listGains(sdr, direction, channel, &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("listGains");
    print_strings("  Gains", strings, length);
    printf("  Has gain mode: %s\n", SoapySDRDevice_hasGainMode(sdr, direction, channel) ? "true" : "false");
    if (SoapySDRDevice_lastStatus() != 0) return fail("hasGainMode");
    if (SoapySDRDevice_setGainMode(sdr, direction, channel, false) != 0) return fail("setGainMode(false)");
    printf("  Gain mode read: %s\n", SoapySDRDevice_getGainMode(sdr, direction, channel) ? "true" : "false");
    if (SoapySDRDevice_lastStatus() != 0) return fail("getGainMode");
    for (size_t i = 0; i < length; i++) {
        SoapySDRRange range = SoapySDRDevice_getGainElementRange(sdr, direction, channel, strings[i]);
        if (SoapySDRDevice_lastStatus() != 0) return fail("getGainElementRange");
        double gain = pick_in_range(range, preferred_gain);
        if (SoapySDRDevice_setGainElement(sdr, direction, channel, strings[i], gain) != 0) return fail("setGainElement");
        printf("  Gain %-16s range=[%g, %g] requested=%g actual=%g\n",
               strings[i], range.minimum, range.maximum, gain,
               SoapySDRDevice_getGainElement(sdr, direction, channel, strings[i]));
    }
    printf("  Overall gain read: %g\n", SoapySDRDevice_getGain(sdr, direction, channel));
    SoapySDRStrings_clear(&strings, length);

    strings = SoapySDRDevice_listChannelSensors(sdr, direction, channel, &length);
    if (SoapySDRDevice_lastStatus() != 0) return fail("listChannelSensors");
    for (size_t i = 0; i < length; i++) {
        SoapySDRArgInfo info = SoapySDRDevice_getChannelSensorInfo(sdr, direction, channel, strings[i]);
        if (SoapySDRDevice_lastStatus() != 0) return fail("getChannelSensorInfo");
        char *value = SoapySDRDevice_readChannelSensor(sdr, direction, channel, strings[i]);
        if (SoapySDRDevice_lastStatus() != 0) return fail("readChannelSensor");
        printf("  Sensor %-16s %-24s value=%s\n", info.key, info.name, value);
        free(value);
        SoapySDRArgInfo_clear(&info);
    }
    SoapySDRStrings_clear(&strings, length);
    return EXIT_SUCCESS;
}

static int run_rx_stream(SoapySDRDevice *sdr, unsigned channels, unsigned packet_size, unsigned reads)
{
    int status = EXIT_FAILURE;
    SoapySDRStream *rx_stream = NULL;
    size_t rx_channels = SoapySDRDevice_getNumChannels(sdr, SOAPY_SDR_RX);
    if (rx_channels == 0) {
        printf("\nSKIP RX stream: device has no RX channels\n");
        return EXIT_SUCCESS;
    }
    if (channels > rx_channels) {
        channels = (unsigned)rx_channels;
    }

    size_t *act_channels = calloc(channels, sizeof(*act_channels));
    void **buffs = calloc(channels, sizeof(*buffs));
    if (act_channels == NULL || buffs == NULL) {
        free(act_channels);
        free(buffs);
        return EXIT_FAILURE;
    }
    for (unsigned i = 0; i < channels; i++) {
        act_channels[i] = i;
        const size_t buffer_size = 2u * packet_size * sizeof(float);
        if (usdr_alignalloc(&buffs[i], STREAM_ALIGN, buffer_size) != 0) {
            goto cleanup;
        }
        memset(buffs[i], 0, buffer_size);
    }

    char packet_size_str[32];
    snprintf(packet_size_str, sizeof(packet_size_str), "%u", packet_size);
    SoapySDRKwargs stream_args = {};
    SoapySDRKwargs_set(&stream_args, "bufferLength", packet_size_str);
    SoapySDRKwargs_set(&stream_args, "linkFormat", SOAPY_SDR_CS16);

    printf("\nRX stream: channels=%u packet_size=%u reads=%u\n", channels, packet_size, reads);
#if (SOAPY_SDR_API_VERSION < 0x00080000)
    if (SoapySDRDevice_setupStream(sdr, &rx_stream, SOAPY_SDR_RX, SOAPY_SDR_CF32, act_channels, channels, &stream_args) != 0) {
        SoapySDRKwargs_clear(&stream_args);
        fail("setupStream");
        goto cleanup;
    }
#else
    rx_stream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CF32, act_channels, channels, &stream_args);
    if (rx_stream == NULL) {
        SoapySDRKwargs_clear(&stream_args);
        fail("setupStream");
        goto cleanup;
    }
#endif
    SoapySDRKwargs_clear(&stream_args);

    size_t mtu = SoapySDRDevice_getStreamMTU(sdr, rx_stream);
    printf("RX stream MTU: %zu\n", mtu);

    int flags = 0;
    long long time_ns = 0;
    int ret = SoapySDRDevice_readStream(sdr, rx_stream, buffs, packet_size, &flags, &time_ns, 10000);
    printf("Inactive readStream ret=%d flags=%d timeNs=%lld\n", ret, flags, time_ns);

    if (SoapySDRDevice_activateStream(sdr, rx_stream, 0, 0, 0) != 0) {
        fail("activateStream");
        goto cleanup;
    }
    for (unsigned i = 0; i < reads; i++) {
        flags = 0;
        time_ns = 0;
        ret = SoapySDRDevice_readStream(sdr, rx_stream, buffs, packet_size, &flags, &time_ns, 100000);
        printf("readStream[%u] ret=%d flags=%d timeNs=%lld\n", i, ret, flags, time_ns);
        if (ret <= 0) {
            SoapySDRDevice_deactivateStream(sdr, rx_stream, 0, 0);
            goto cleanup;
        }
    }
    if (SoapySDRDevice_deactivateStream(sdr, rx_stream, 0, 0) != 0) {
        fail("deactivateStream");
        goto cleanup;
    }
    if (SoapySDRDevice_closeStream(sdr, rx_stream) != 0) {
        rx_stream = NULL;
        fail("closeStream");
        goto cleanup;
    }
    rx_stream = NULL;
    status = EXIT_SUCCESS;

cleanup:
    if (rx_stream != NULL) {
        SoapySDRDevice_closeStream(sdr, rx_stream);
    }
    for (unsigned i = 0; i < channels; i++) usdr_alignfree(buffs[i]);
    free(act_channels);
    free(buffs);
    return status;
}

int main(int argc, char **argv)
{
    const char *device = "";
    unsigned channels = 1;
    unsigned packet_size = 4096;
    unsigned reads = 4;
    double sample_rate = 5e6;
    double rx_freq = 912.3e6;
    double bandwidth = 1e6;
    double gain = 15.0;
    bool query_only = false;

    int opt;
    while ((opt = getopt(argc, argv, "hd:c:i:n:r:f:b:g:Q")) != -1) {
        switch (opt) {
        case 'd': device = optarg; break;
        case 'c': channels = (unsigned)atoi(optarg); break;
        case 'i': packet_size = (unsigned)atoi(optarg); break;
        case 'n': reads = (unsigned)atoi(optarg); break;
        case 'r': sample_rate = atof(optarg); break;
        case 'f': rx_freq = atof(optarg); break;
        case 'b': bandwidth = atof(optarg); break;
        case 'g': gain = atof(optarg); break;
        case 'Q': query_only = true; break;
        case 'h':
        default:
            usage(argv[0]);
            return (opt == 'h') ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

    if (channels == 0 || channels > MAX_CHANS) {
        printf("Number of channels should be in range [1;%d]\n", MAX_CHANS);
        return EXIT_FAILURE;
    }
    if (packet_size == 0 || packet_size > MAX_PACKETSIZE) {
        printf("Packet size should be in range [1;%d]\n", MAX_PACKETSIZE);
        return EXIT_FAILURE;
    }

    SoapySDRKwargs enum_args = {};
    SoapySDRKwargs_set(&enum_args, "driver", "usdr");
    size_t length = 0;
    SoapySDRKwargs *results = SoapySDRDevice_enumerate(&enum_args, &length);
    SoapySDRKwargs_clear(&enum_args);
    for (size_t i = 0; i < length; i++) {
        printf("Found device #%zu: ", i);
        print_kwargs(&results[i]);
        printf("\n");
    }
    SoapySDRKwargsList_clear(results, length);
    if (length == 0) {
        printf("No USDR devices were found\n");
        return EXIT_FAILURE;
    }

    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "usdr");
    SoapySDRKwargs_set(&args, "loglevel", "2");
    if (*device != 0) {
        SoapySDRKwargs_set(&args, "bus", device);
    }
    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);
    if (sdr == NULL) return fail("make");

    int status = check_identification(sdr);
    size_t rx_channels = SoapySDRDevice_getNumChannels(sdr, SOAPY_SDR_RX);
    size_t tx_channels = SoapySDRDevice_getNumChannels(sdr, SOAPY_SDR_TX);
    printf("\nChannels: RX=%zu TX=%zu\n", rx_channels, tx_channels);

    if (status == EXIT_SUCCESS) status = check_global_api(sdr);
    for (size_t i = 0; status == EXIT_SUCCESS && i < rx_channels; i++) {
        status = check_channel(sdr, SOAPY_SDR_RX, "RX", i, sample_rate, rx_freq, bandwidth, gain);
    }
    for (size_t i = 0; status == EXIT_SUCCESS && i < tx_channels; i++) {
        status = check_channel(sdr, SOAPY_SDR_TX, "TX", i, sample_rate, rx_freq, bandwidth, gain);
    }
    if (status == EXIT_SUCCESS && !query_only) {
        status = run_rx_stream(sdr, channels, packet_size, reads);
    }

    int unmake_status = SoapySDRDevice_unmake(sdr);
    if (unmake_status != 0 && status == EXIT_SUCCESS) {
        status = fail("unmake");
    }

    printf("\n%s\n", status == EXIT_SUCCESS ? "Done: PASS" : "Done: FAIL");
    return status;
}
