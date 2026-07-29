// Copyright (c) 2023-2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "usdr_soapy.h"
#include <stdexcept>
#include <iostream>
#include <memory>
#include <SoapySDR/Logger.hpp>
#include <SoapySDR/Time.hpp>
#include <SoapySDR/Formats.hpp>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <cmath>
#include <string.h>
#include <climits>
#include <unistd.h>

// #include <usdr_logging.h>

std::map<std::string, std::weak_ptr<usdr_handle>> usdr_handle::s_created;

static const std::set<std::string> USDR_SOAPY_DEVICE_ARGS = {
    "bus",
    "device",
    "fe",
    "extclk",
    "extref",
};

static void usdr_soapy_apply_device_arg(const SoapySDR::Kwargs &args, SoapySDR::Kwargs &dev_args, const char *key)
{
    if (args.count(key)) {
        dev_args[key] = args.at(key);
    }
}

SoapySDR::Kwargs usdrSoapyDeviceArgs(const SoapySDR::Kwargs &args)
{
    SoapySDR::Kwargs dev_args = SoapySDR::KwargsFromString((args.count("dev")) ? args.at("dev") : "");

    for (const auto &key : USDR_SOAPY_DEVICE_ARGS) {
        usdr_soapy_apply_device_arg(args, dev_args, key.c_str());
    }

    return dev_args;
}

std::string usdrSoapyDeviceString(const SoapySDR::Kwargs &args)
{
    return SoapySDR::KwargsToString(usdrSoapyDeviceArgs(args));
}

bool usdrSoapyIsDeviceArg(const std::string &key)
{
    // "dev" is a packed Soapy device string, not a lower-level device parameter.
    return key == "dev" || USDR_SOAPY_DEVICE_ARGS.count(key) != 0;
}

std::shared_ptr<usdr_handle> usdr_handle::get(const std::string& name)
{
    auto idx = s_created.find(name);
    if (idx != s_created.end()) {
         if (std::shared_ptr<usdr_handle> obj = idx->second.lock())
             return obj;
    }

    std::shared_ptr<usdr_handle> obj = std::make_shared<usdr_handle>(name);
    s_created.insert(make_pair(name, obj));
    return obj;
}

usdr_handle::usdr_handle(const std::string& name)
{
    int res = usdr_dmd_create_string(name.c_str(), &_dev);
    if (res < 0)
        throw std::runtime_error(std::string("usdr_handle::usdr_handle(") + name.c_str() + ") - unable to open the device: error: " + strerror(-res));
    devcnt = res;

    SoapySDR::log(SOAPY_SDR_INFO, std::string("Created: `") + name.c_str() + "`");
}

usdr_handle::~usdr_handle()
{
    usdr_dmd_close(_dev);
}

struct rfic_gain_descriptor
{
    int direction;
    SoapySDR::Range range;
    const char* name;
    const char* altname;
    const char* altname2;
    const char* property_name;
};

static const rfic_gain_descriptor lms7_gains[] {
    { SOAPY_SDR_RX, SoapySDR::Range(0.0, 30.0),   "LNA", nullptr, nullptr, "/dm/sdr/0/rx/gain/lna" },
    { SOAPY_SDR_RX, SoapySDR::Range(0.0, 12.0),   "TIA", "VGA",   "VGA1",  "/dm/sdr/0/rx/gain/vga" },
    { SOAPY_SDR_RX, SoapySDR::Range(-12.0, 19.0), "PGA", "VGA2",  nullptr, "/dm/sdr/0/rx/gain/pga" },
    { SOAPY_SDR_TX, SoapySDR::Range(-52.0, 0.0),  "PAD", nullptr, nullptr, "/dm/sdr/0/tx/gain" },
    { 0, SoapySDR::Range(), nullptr, nullptr, nullptr, nullptr }
};

static const rfic_gain_descriptor lms6_gains[] {
    { SOAPY_SDR_RX, SoapySDR::Range(0.0, 6.0),    "LNA",  nullptr, nullptr, "/dm/sdr/0/rx/gain/lna" },
    { SOAPY_SDR_RX, SoapySDR::Range(5, 31),       "VGA1", "TIA",   nullptr, "/dm/sdr/0/rx/gain/vga" },
    { SOAPY_SDR_RX, SoapySDR::Range(0, 60.0),     "VGA2", "PGA",   nullptr, "/dm/sdr/0/rx/gain/pga" },
    { SOAPY_SDR_TX, SoapySDR::Range(-35.0, -4),   "VGA1", nullptr, nullptr, "/dm/sdr/0/tx/gain/vga1" },
    { SOAPY_SDR_TX, SoapySDR::Range(0.0, 25.0),   "VGA2", nullptr, nullptr,  "/dm/sdr/0/tx/gain/vga2" },
    { 0, SoapySDR::Range(), nullptr, nullptr, nullptr, nullptr }
};

static const rfic_gain_descriptor ad45lb49_gains[] {
    { SOAPY_SDR_RX, SoapySDR::Range(0, 4),        "LNA",  "SEL",   nullptr, "/dm/sdr/0/rx/gain/lna" },
    { SOAPY_SDR_RX, SoapySDR::Range(0, 31),       "VGA",  "ATTN",  nullptr, "/dm/sdr/0/rx/gain/vga" },
    { SOAPY_SDR_RX, SoapySDR::Range(0, 31),       "PGA",  nullptr, nullptr, "/dm/sdr/0/rx/gain/pga" },
    { 0, SoapySDR::Range(), nullptr, nullptr, nullptr, nullptr }
};

static const rfic_gain_descriptor unk_gains[] {
    { SOAPY_SDR_RX, SoapySDR::Range(-99, 99),   "GRX", nullptr, nullptr, "/dm/sdr/0/rx/gain" },
    { SOAPY_SDR_TX, SoapySDR::Range(-99, 99),   "GTX", nullptr, nullptr, "/dm/sdr/0/tx/gain" },
    { 0, SoapySDR::Range(), nullptr, nullptr, nullptr, nullptr }
};

static inline const rfic_gain_descriptor* get_gains(rfic_type_t t) {
    switch (t) {
    case RFIC_LMS6002D: return lms6_gains;
    case RFIC_LMS7002M: return lms7_gains;
    case RFIC_AD45LB49: return ad45lb49_gains;
    default: return unk_gains;
    }
}

static bool get_overall_gain_range(rfic_type_t t, int direction, SoapySDR::Range& range)
{
    const rfic_gain_descriptor* gains = get_gains(t);
    bool found = false;
    double min_gain = 0.0;
    double max_gain = 0.0;

    for (unsigned i = 0; gains[i].name != nullptr; i++) {
        if (gains[i].direction != direction) {
            continue;
        }

        min_gain += gains[i].range.minimum();
        max_gain += gains[i].range.maximum();
        found = true;
    }

    if (found) {
        range = SoapySDR::Range(min_gain, max_gain);
    }
    return found;
}

struct device_ranges
{
    SoapySDR::Range frequency_range;
    SoapySDR::Range samplerate_range;
    SoapySDR::Range bandwidth_range;
};

static const char RATE_CONFIG_ENDPOINT[] = "/dm/rate/rxtxadcdac";

enum rate_config_index
{
    RATE_CONFIG_RX = 0,
    RATE_CONFIG_TX,
    RATE_CONFIG_ADC,
    RATE_CONFIG_DAC,
    RATE_CONFIG_WORDS
};

typedef std::array<unsigned, RATE_CONFIG_WORDS> rate_config;

static const double DEFAULT_SAMPLE_RATE = 5e6;

struct rx_packet_policy
{
    double min_sample_rate;
    unsigned packet_samples;
};

static const rx_packet_policy RX_PACKET_POLICIES[] {
    { 7.6e6, 7680 },
    { 3.8e6, 3840 },
    { 0.0, 1920 },
};

static unsigned default_rx_packet_samples(double sample_rate)
{
    for (const auto &policy : RX_PACKET_POLICIES) {
        if (sample_rate >= policy.min_sample_rate) {
            return policy.packet_samples;
        }
    }
    return RX_PACKET_POLICIES[sizeof(RX_PACKET_POLICIES) / sizeof(RX_PACKET_POLICIES[0]) - 1].packet_samples;
}

static const device_ranges usdr_ranges {
    .frequency_range = SoapySDR::Range(0.1e6, 3800e6),
    .samplerate_range = SoapySDR::Range(1e6, 85e6),
    .bandwidth_range = SoapySDR::Range(0.5e6, 40e6),
};

static const device_ranges xsdr_ranges {
    .frequency_range = SoapySDR::Range(0.1e6, 3800e6),
    .samplerate_range = SoapySDR::Range(1e6, 125e6),
    .bandwidth_range = SoapySDR::Range(0.5e6, 125e6),
};

static const device_ranges ssdr_ranges {
    .frequency_range = SoapySDR::Range(0.1e6, 12500e6),
    .samplerate_range = SoapySDR::Range(4e6, 125e6),
    .bandwidth_range = SoapySDR::Range(0.5e6, 125e6),
};

static const device_ranges dsdr_ranges {
    .frequency_range = SoapySDR::Range(5e6, 12500e6),
    .samplerate_range = SoapySDR::Range(4e6, 500e6),
    .bandwidth_range = SoapySDR::Range(0.5e6, 500e6),
};

static const device_ranges lsdr_ranges {
    .frequency_range = SoapySDR::Range(0.1e6, 3800e6),
    .samplerate_range = SoapySDR::Range(2e6, 125e6),
    .bandwidth_range = SoapySDR::Range(0.5e6, 125e6),
};

static const device_ranges limesdr_mini_ranges {
    .frequency_range = SoapySDR::Range(30e6, 3800e6),
    .samplerate_range = SoapySDR::Range(0.1e6, 40e6),
    .bandwidth_range = SoapySDR::Range(0.5e6, 40e6),
};

static const device_ranges unk_ranges {
    .frequency_range = SoapySDR::Range(30e6, 3800e6),
    .samplerate_range = SoapySDR::Range(1e6, 40e6),
    .bandwidth_range = SoapySDR::Range(0.5e6, 40e6),
};

static inline const device_ranges* get_ranges(device_type_t t) {
    switch (t) {
    case DEVICE_USDR: return &usdr_ranges;
    case DEVICE_XSDR: return &xsdr_ranges;
    case DEVICE_SSDR: return &ssdr_ranges;
    case DEVICE_DSDR: return &dsdr_ranges;
    case DEVICE_LSDR: return &lsdr_ranges;
    case DEVICE_LIMESDR_MINI: return &limesdr_mini_ranges;
    default: return &unk_ranges;
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint64_t SoapyUSDR::max_sw_chans(const int direction) const
{
    validateDirection(direction);
    return (direction == SOAPY_SDR_RX) ? max_sw_rx_chans : max_sw_tx_chans;
}

uint64_t SoapyUSDR::max_hw_chans(const int direction) const
{
    validateDirection(direction);
    return (direction == SOAPY_SDR_RX) ? max_hw_rx_chans : max_hw_tx_chans;
}

void SoapyUSDR::validateDirection(const int direction) const
{
    if (direction != SOAPY_SDR_RX && direction != SOAPY_SDR_TX) {
        throw std::invalid_argument("SoapyUSDR: invalid direction " + std::to_string(direction));
    }
}

void SoapyUSDR::validateChannel(const int direction, const size_t channel) const
{
    const uint64_t chans = max_sw_chans(direction);
    if (channel >= chans) {
        throw std::out_of_range("SoapyUSDR: channel " + std::to_string(channel) +
                                " is outside available software channels [0, " +
                                std::to_string(chans) + ")");
    }
}

void SoapyUSDR::ensureSampleRateConfigured(const int direction, const size_t channel)
{
    validateChannel(direction, channel);

    const unsigned actual_rate = (direction == SOAPY_SDR_TX) ? _actual_tx_rate : _actual_rx_rate;
    if (actual_rate != 0) {
        return;
    }

    const device_ranges *dev_ranges = get_ranges(device_type);
    const double default_rate = dev_ranges ? dev_ranges->samplerate_range.minimum() : DEFAULT_SAMPLE_RATE;
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::ensureSampleRateConfigured(%s, %d) => %.3f MHz",
                   direction == SOAPY_SDR_TX ? "TX" : "RX", int(channel), default_rate / 1e6);
    setSampleRate(direction, channel, default_rate);
}

const char* SoapyUSDR::get_sdr_param(int sdridx, const char* dir, const char* par, const char* subpar)
{
    if (subpar) {
        snprintf(_param_name, sizeof(_param_name), "/dm/sdr/%d/%s/%s/%s", sdridx, dir, par, subpar);
    } else {
        snprintf(_param_name, sizeof(_param_name), "/dm/sdr/%d/%s/%s", sdridx, dir, par);
    }
    return _param_name;
}

const char* SoapyUSDR::get_sdr_param_chan(int sdridx, const char* dir, const char* par, const char* subpar, unsigned chan)
{
    const bool tx = (strcmp(dir, "tx") == 0);
    const uint64_t max_sw_chans = tx ? max_sw_tx_chans : max_sw_rx_chans;
    if (max_sw_chans <= 1)
        return get_sdr_param(sdridx, dir, par, subpar);

    if (subpar) {
        snprintf(_param_name, sizeof(_param_name), "/dm/sdr/%d/%s/%s/%s/%d", sdridx, dir, par, subpar, chan);
    } else {
        snprintf(_param_name, sizeof(_param_name), "/dm/sdr/%d/%s/%s/%d", sdridx, dir, par, chan);
    }
    return _param_name;
}



SoapyUSDR::SoapyUSDR(const SoapySDR::Kwargs &args_orig)
 : _actual_tx_rate(0)
 , _actual_rx_rate(0)
 , _desired_rx_pkt(0)
 , last_recv_pkt_time(0)
 , avg_gap(0)
 , rx_pkts(0)
 , tx_pkts(0)
 , rd(NULL)
{
    unsigned loglevel = 3;
#ifdef __linux
    const char* lenv = getenv("SOAPY_USDR_LOGLEVEL");
    if (lenv) {
        loglevel = atoi(lenv);
    }
#endif

    std::string env_str;
    if (getenv("SOAPY_USDR_ARGS")) {
        env_str = std::string(getenv("SOAPY_USDR_ARGS"));

        SoapySDR::logf(callLogLvl(), "SoapyUSDR::SoapyUSDR() overriding default parameters to `%s`", env_str.c_str());
    }
    SoapySDR::Kwargs env_args = SoapySDR::KwargsFromString(env_str);
    const SoapySDR::Kwargs &args = (env_str.length() > 0) ? env_args : args_orig;


    if (args.count("loglevel")) {
        loglevel = std::stoi(args.at("loglevel"));
    }

    const std::string dev = usdrSoapyDeviceString(args);

    if (args.count("txcorr")) {
        _txcorr = atoi(args.at("txcorr").c_str());
    }
    if (args.count("calls")) {
        _dump_calls = atoi(args.at("calls").c_str()) ? true : false;
    }

    usdrlog_setlevel(NULL, loglevel);

    SoapySDR::logf(SOAPY_SDR_DEBUG, "Make connection: '%s'", args.count("dev") ? args.at("dev").c_str() : "*");
    for (auto& i: args) {
        SoapySDR::logf(SOAPY_SDR_DEBUG, "Param %s => %s", i.first.c_str(), i.second.c_str());
    }

    _dev = usdr_handle::get(dev);

    if (args.count("refclk")) {
        // TODO:
        SoapySDR::logf(callLogLvl(), "SoapyUSDR::SoapyUSDR() set ref to internal clock");
    }
    if (args.count("extclk")) {
        // TODO:
        SoapySDR::logf(callLogLvl(), "SoapyUSDR::SoapyUSDR() set ref to external clock");
    }
    if (args.count("desired_rx_pkt")) {
        _desired_rx_pkt = atoi(args.at("desired_rx_pkt").c_str());

        SoapySDR::logf(callLogLvl(), "SoapyUSDR::SoapyUSDR() set `desired_rx_pkt` to %d", _desired_rx_pkt);
    }
    if (args.count("rxdump")) {
        const char* filename = args.at("rxdump").c_str();
        rd = fopen(filename, "wb+");
        if (rd == NULL)
            throw std::runtime_error("SoapyUSDR::SoapyUSDR() - unable to create rx dump file");

        SoapySDR::logf(callLogLvl(), "SoapyUSDR::SoapyUSDR() dumping recieve to %s", filename);
    }
    if (args.count("rx12bit")) {
        _force_rx_wire12bit = true;
        SoapySDR::logf(callLogLvl(), "SoapyUSDR::SoapyUSDR() forcing RX wire format to 12bit");
    }

    uint64_t val;
    int res;

    usdr_dme_get_uint(_dev->dev(), "/ll/sdr/max_hw_rx_chans", &max_hw_rx_chans);
    usdr_dme_get_uint(_dev->dev(), "/ll/sdr/max_hw_tx_chans", &max_hw_tx_chans);
    usdr_dme_get_uint(_dev->dev(), "/ll/sdr/max_sw_rx_chans", &max_sw_rx_chans);
    usdr_dme_get_uint(_dev->dev(), "/ll/sdr/max_sw_tx_chans", &max_sw_tx_chans);

    res = usdr_dme_get_uint(_dev->dev(), "/ll/sdr/0/rfic/0", &val);
    if (res == 0) {
        const char* rfic = reinterpret_cast<const char*>(val);
        if (strcmp(rfic, "lms6002d") == 0)
            type = RFIC_LMS6002D;
        else if (strcmp(rfic, "lms7002m") == 0)
            type = RFIC_LMS7002M;
        else if (strcmp(rfic, "ad45lb49") == 0)
            type = RFIC_AD45LB49;
        else if (strcmp(rfic, "afe79xx") == 0)
            type = RFIC_AFE79XX;
    }

    res = usdr_dme_get_uint(_dev->dev(), "/ll/device/name", &val);
    if (res == 0) {
        const char* device = reinterpret_cast<const char*>(val);
        SoapySDR::logf(callLogLvl(), "SoapyUSDR::SoapyUSDR() Device name is \"%s\"", device);
        if (strcmp(device, "usdr") == 0)
            device_type = DEVICE_USDR;
        else if (strcmp(device, "xsdr") == 0)
            device_type = DEVICE_XSDR;
        else if (strcmp(device, "ssdr") == 0)
            device_type = DEVICE_SSDR;
        else if (strcmp(device, "dsdr") == 0)
            device_type = DEVICE_DSDR;
        else if (strcmp(device, "lsdr") == 0)
            device_type = DEVICE_LSDR;
        else if (strcmp(device, "limemini") == 0)
            device_type = DEVICE_LIMESDR_MINI;
    }

    if (args.count("rx_bw")) {
        unsigned bw = atoi(args.at("rx_bw").c_str());
        SoapySDR::logf(callLogLvl(), "SoapyUSDR::SoapyUSDR() RX_BW set to %d", bw);
        usdr_dme_set_uint(_dev->dev(), "/dm/sdr/0/rx/bandwidth", bw);
    }
    if (args.count("tx_bw")) {
        unsigned bw = atoi(args.at("tx_bw").c_str());
        SoapySDR::logf(callLogLvl(), "SoapyUSDR::SoapyUSDR() TX_BW set to %d", bw);
        usdr_dme_set_uint(_dev->dev(), "/dm/sdr/0/tx/bandwidth", bw);
    }

    _streams[0].active = false;
    _streams[1].active = false;
}

SoapyUSDR::~SoapyUSDR(void)
{
    if (rd) {
        fclose(rd);
    }
}

/*******************************************************************
 * Identification API
 ******************************************************************/
std::string SoapyUSDR::getDriverKey(void) const
{
    return "usdrsoapy";
}

std::string SoapyUSDR::getHardwareKey(void) const
{
    return "usdrdev";
}

SoapySDR::Kwargs SoapyUSDR::getHardwareInfo(void) const
{
    SoapySDR::Kwargs info;
    return info;
}

/*******************************************************************
 * Channels API
 ******************************************************************/
size_t SoapyUSDR::getNumChannels(const int direction) const
{
    validateDirection(direction);

    uint64_t chans = 1;
    const char* nch = direction == SOAPY_SDR_RX ? "/ll/sdr/max_sw_rx_chans" :  "/ll/sdr/max_sw_tx_chans";
    int res = usdr_dme_get_uint(_dev->dev(), nch, &chans);
    if (res) {
        SoapySDR::logf(callLogLvl(), "SoapyUSDR::getNumChannels(%d): couldn't obtain channels count, defaulting to 1", direction);
        return 1;
    }

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::getNumChannels(%d) => %d", direction, chans);
    return chans;
}

bool SoapyUSDR::getFullDuplex(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);
    return true;
}

SoapySDR::Kwargs SoapyUSDR::getChannelInfo(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    SoapySDR::Kwargs info;
    info["software_channel"] = std::to_string(channel);
    info["software_channels"] = std::to_string(max_sw_chans(direction));
    info["hardware_channels"] = std::to_string(max_hw_chans(direction));
    return info;
}

/*******************************************************************
 * Antenna API
 ******************************************************************/
std::vector<std::string> SoapyUSDR::listAntennas(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    std::vector<std::string> ants;
    if (direction == SOAPY_SDR_RX)
    {
        ants.push_back("LNAH");
        ants.push_back("LNAL");
        ants.push_back("LNAW");
    }
    if (direction == SOAPY_SDR_TX)
    {
        ants.push_back("TXH");
        ants.push_back("TXW");
    }
    return ants;
}

void SoapyUSDR::setAntenna(const int direction, const size_t channel, const std::string &name)
{
    validateChannel(direction, channel);

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setAntenna(%d, %d, %s)", direction, int(channel), name.c_str());

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
}

std::string SoapyUSDR::getAntenna(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    std::string antenna = "";

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::getAntenna(%d, %d, %s)", direction, int(channel), antenna.c_str());
    return antenna;
}

/*******************************************************************
 * Frontend corrections API
 ******************************************************************/

bool SoapyUSDR::hasDCOffsetMode(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);
    return (direction == SOAPY_SDR_RX);
}

void SoapyUSDR::setDCOffsetMode(const int direction, const size_t channel, const bool /*automatic*/)
{
    validateChannel(direction, channel);
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    if (direction == SOAPY_SDR_RX) {
    }
}

bool SoapyUSDR::getDCOffsetMode(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    if (direction == SOAPY_SDR_RX) {
    }

    return false;
}

bool SoapyUSDR::hasDCOffset(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);
    return (direction == SOAPY_SDR_TX);
}

void SoapyUSDR::setDCOffset(const int direction, const size_t channel, const std::complex<double> &/*offset*/)
{
    validateChannel(direction, channel);
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    if (direction == SOAPY_SDR_TX) {
    }
}

std::complex<double> SoapyUSDR::getDCOffset(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    double I = 0.0, Q = 0.0;
    return std::complex<double>(I, Q);
}

bool SoapyUSDR::hasIQBalance(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);
    return true;
}

void SoapyUSDR::setIQBalance(const int direction, const size_t channel, const std::complex<double> &/*balance*/)
{
    validateChannel(direction, channel);
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    //TODO
}

std::complex<double> SoapyUSDR::getIQBalance(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    return std::complex<double>(0,0);
}

bool SoapyUSDR::hasIQBalanceMode(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);
    return false;
}

void SoapyUSDR::setIQBalanceMode(const int direction, const size_t channel, const bool automatic)
{
    validateChannel(direction, channel);
    if (automatic) {
        throw std::runtime_error("SoapyUSDR::setIQBalanceMode() automatic IQ balance is not supported");
    }
}

bool SoapyUSDR::getIQBalanceMode(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);
    return false;
}

bool SoapyUSDR::hasFrequencyCorrection(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);
    return false;
}

void SoapyUSDR::setFrequencyCorrection(const int direction, const size_t channel, const double value)
{
    validateChannel(direction, channel);
    if (value != 0.0) {
        throw std::runtime_error("SoapyUSDR::setFrequencyCorrection() frequency correction is not supported");
    }
}

double SoapyUSDR::getFrequencyCorrection(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);
    return 0.0;
}

/*******************************************************************
 * Gain API
 ******************************************************************/

std::vector<std::string> SoapyUSDR::listGains(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    std::vector<std::string> gain_list;
    const rfic_gain_descriptor* gains = get_gains(type);
    for (unsigned i = 0; gains[i].name != nullptr; i++) {
        if (gains[i].direction == direction) {
            gain_list.push_back(gains[i].name);
        }
    }

    return gain_list;
}

bool SoapyUSDR::hasGainMode(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);
    return false;
}

void SoapyUSDR::setGainMode(const int direction, const size_t channel, const bool automatic)
{
    validateChannel(direction, channel);
    if (automatic) {
        throw std::runtime_error("SoapyUSDR::setGainMode() automatic gain mode is not supported");
    }
}

bool SoapyUSDR::getGainMode(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);
    return false;
}

void SoapyUSDR::setGain(const int direction, const size_t channel, const double value)
{
    validateChannel(direction, channel);

    const char* dir = (direction == SOAPY_SDR_TX) ? "tx" : "rx";
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setGain(%s, %d, %g dB)", dir, int(channel), value);

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    const char* defparam = get_sdr_param_chan(0, dir, "gain", "auto", channel);
    int res = usdr_dme_set_uint(_dev->dev(), defparam, value);
    if (res) {
        SoapySDR::logf(callLogLvl(), "SoapyUSDR::setGain(%s, %d, %g dB) => %s failed %d",
                       dir, int(channel), value, defparam, res);
    }
    _actual_gains[direction][channel][""] = value;
}

void SoapyUSDR::setGain(const int direction, const size_t channel, const std::string &name, const double value)
{
    validateChannel(direction, channel);

    const char* dir = (direction == SOAPY_SDR_TX) ? "tx" : "rx";
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setGain(%s, %d, %s, %g dB)",
                   direction == SOAPY_SDR_RX ? "RX" : "TX",
                   int(channel), name.c_str(), value);

    const rfic_gain_descriptor* gains = get_gains(type);
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    const char* defparam = get_sdr_param_chan(0, dir, "gain", nullptr, channel);
    unsigned i;

    for (i = 0; gains[i].name != nullptr; i++) {
        if ((gains[i].direction == direction) && (name == gains[i].name ||
                (gains[i].altname && (name == gains[i].altname)) || (gains[i].altname2 && (name == gains[i].altname2)))) {
            defparam = gains[i].property_name;
            break;
        }
    }
    if (gains[i].name == nullptr) {
        throw std::invalid_argument("SoapyUSDR::setGain(" + name + ") unknown gain name");
    }

    char chan_param[128];
    if (max_sw_chans(direction) > 1) {
        snprintf(chan_param, sizeof(chan_param), "%s/%u", defparam, (unsigned)channel);
        defparam = chan_param;
    }

    int res = usdr_dme_set_uint(_dev->dev(), defparam, value);
    if (res)
        throw std::runtime_error(std::string("SoapyUSDR::setGain(") + defparam + ", " + std::to_string((int)value) + ")");

    _actual_gains[direction][channel][gains[i].name] = value;
    if (gains[i].altname) {
        _actual_gains[direction][channel][gains[i].altname] = value;
    }
    if (gains[i].altname2) {
        _actual_gains[direction][channel][gains[i].altname2] = value;
    }
}

double SoapyUSDR::getGain(const int direction, const size_t channel, const std::string &name) const
{
    validateChannel(direction, channel);

    const rfic_gain_descriptor* gains = get_gains(type);
    unsigned i;
    for (i = 0; gains[i].name != nullptr; i++) {
        if ((gains[i].direction == direction) && (name == gains[i].name ||
                (gains[i].altname && (name == gains[i].altname)) || (gains[i].altname2 && (name == gains[i].altname2)))) {
            break;
        }
    }
    if (gains[i].name == nullptr) {
        throw std::invalid_argument("SoapyUSDR::getGain(" + name + ") unknown gain name");
    }

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    double value = 0.0;
    const auto dir_it = _actual_gains.find(direction);
    if (dir_it != _actual_gains.end()) {
        const auto chan_it = dir_it->second.find(channel);
        if (chan_it != dir_it->second.end()) {
            const auto gain_it = chan_it->second.find(name);
            if (gain_it != chan_it->second.end()) {
                value = gain_it->second;
            }
        }
    }

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::getGain(%s, %d, %s) => %g dB",
                   direction == SOAPY_SDR_RX ? "RX" : "TX",
                   int(channel), name.c_str(), value);
    return value;
}

double SoapyUSDR::getGain(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    const auto dir_it = _actual_gains.find(direction);
    if (dir_it == _actual_gains.end()) {
        return 0.0;
    }

    const auto chan_it = dir_it->second.find(channel);
    if (chan_it == dir_it->second.end()) {
        return 0.0;
    }

    const auto gain_it = chan_it->second.find("");
    if (gain_it != chan_it->second.end()) {
        return gain_it->second;
    }

    double value = 0.0;
    const rfic_gain_descriptor* gains = get_gains(type);
    for (unsigned i = 0; gains[i].name != nullptr; i++) {
        if (gains[i].direction != direction) {
            continue;
        }
        const auto named_it = chan_it->second.find(gains[i].name);
        if (named_it != chan_it->second.end()) {
            value += named_it->second;
        }
    }
    return value;
}

SoapySDR::Range SoapyUSDR::getGainRange(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    SoapySDR::Range range;
    if (get_overall_gain_range(type, direction, range)) {
        return range;
    }

    return SoapySDR::Device::getGainRange(direction, channel);
}

SoapySDR::Range SoapyUSDR::getGainRange(const int direction, const size_t channel, const std::string &name) const
{
    validateChannel(direction, channel);

    const rfic_gain_descriptor* gains = get_gains(type);
    for (unsigned i = 0; gains[i].name != nullptr; i++) {
        if ((gains[i].direction == direction) && (name == gains[i].name)) {
            return gains[i].range;
        }
    }
    return SoapySDR::Device::getGainRange(direction, channel, name);
}

/*******************************************************************
 * Frequency API
 ******************************************************************/
SoapySDR::ArgInfoList SoapyUSDR::getFrequencyArgsInfo(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    auto infos = SoapySDR::Device::getFrequencyArgsInfo(direction, channel);
    /*{
        SoapySDR::ArgInfo info;
        info.key = "CORRECTIONS";
        info.name = "Corrections";
        info.value = "true";
        info.description = "Automatically apply DC/IQ corrections";
        info.type = SoapySDR::ArgInfo::BOOL;
        infos.push_back(info);
    }*/
    return infos;
}

void SoapyUSDR::setFrequency(const int direction, const size_t channel, const double frequency, const SoapySDR::Kwargs &args)
{
    setFrequency(direction, channel, "RF", frequency, args);
}

void SoapyUSDR::setFrequency(const int direction, const size_t channel, const std::string &name, const double frequency, const SoapySDR::Kwargs &/*args*/)
{
    validateChannel(direction, channel);
    if (!(name.empty() || name == "RF" || name == "BB")) {
        throw std::invalid_argument("SoapyUSDR::setFrequency(" + name + ") unknown frequency name");
    }
    ensureSampleRateConfigured(direction, channel);

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setFrequency(%s, %d, %s, %g MHz)",
                   direction == SOAPY_SDR_RX ? "RX" : "TX",
                   int(channel), name.c_str(), frequency/1e6);
    int res;

    const char* dir = (direction == SOAPY_SDR_TX) ? "tx" : "rx";
    const char* subpar = (name == "BB") ? "bb" : NULL;
    std::string pname = get_sdr_param_chan(0, dir, "frequency", subpar, channel);

    res = usdr_dme_set_uint(_dev->dev(), pname.c_str(), (uint64_t)(int64_t)frequency);
    if (res)
        throw std::runtime_error(std::string("SoapyUSDR::setFrequency(") + pname + ", " + std::to_string(frequency) + ")");

    const std::string actual_name = name.empty() ? "RF" : name;
    _actual_frequency[direction][channel][actual_name] = frequency;
}

double SoapyUSDR::getFrequency(const int direction, const size_t channel) const
{
    return getFrequency(direction, channel, "RF");
}

double SoapyUSDR::getFrequency(const int direction, const size_t channel, const std::string &name) const
{
    validateChannel(direction, channel);

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::getFrequency(%d, %s)", int(channel), name.c_str());

    const auto dir_it = _actual_frequency.find(direction);
    if (dir_it == _actual_frequency.end()) {
        return 0.0;
    }

    const auto chan_it = dir_it->second.find(channel);
    if (chan_it == dir_it->second.end()) {
        return 0.0;
    }

    const std::string actual_name = name.empty() ? "RF" : name;
    const auto freq_it = chan_it->second.find(actual_name);
    if (freq_it == chan_it->second.end()) {
        return 0.0;
    }

    return freq_it->second;
}

std::vector<std::string> SoapyUSDR::listFrequencies(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    std::vector<std::string> opts;
    opts.push_back("RF");
    //opts.push_back("BB");
    return opts;
}

SoapySDR::RangeList SoapyUSDR::getFrequencyRange(const int direction, const size_t channel, const std::string &name) const
{
    validateChannel(direction, channel);

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    SoapySDR::RangeList ranges;
    if (name == "RF" || name.empty())
    {
        const device_ranges *dev_ranges = get_ranges(device_type);
        if (!dev_ranges)
            return ranges;
        ranges.push_back(dev_ranges->frequency_range);
    }
    else if (name == "BB")
    {
        double sample_rate = (direction == SOAPY_SDR_TX) ? _actual_tx_rate : _actual_rx_rate;
        if (sample_rate == 0.0) {
            const device_ranges *dev_ranges = get_ranges(device_type);
            if (dev_ranges) {
                sample_rate = dev_ranges->samplerate_range.minimum();
            }
        }
        ranges.push_back(SoapySDR::Range(-sample_rate / 2, sample_rate / 2));
    }
    return ranges;
}

SoapySDR::RangeList SoapyUSDR::getFrequencyRange(const int direction, const size_t channel) const
{
    return getFrequencyRange(direction, channel, "RF");
}

/*******************************************************************
 * Sample Rate API
 ******************************************************************/

void SoapyUSDR::setSampleRate(const int direction, const size_t channel, const double rate)
{
    validateChannel(direction, channel);

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setSampleRate(%d, %s, %g MHz)", int(channel), (direction == SOAPY_SDR_TX) ? "TX" : "RX", rate/1e6);


    if (direction == SOAPY_SDR_RX)
    {
        _actual_rx_rate = rate;
        _actual_tx_rate = rate; // FIXUP
    }
    else if (direction == SOAPY_SDR_TX)
    {
        _actual_tx_rate = rate;
        _actual_rx_rate = rate; // FIXUP
    }
    else
    {
        return;
    }

    rate_config rates = {};
    rates[RATE_CONFIG_RX] = _actual_rx_rate;
    rates[RATE_CONFIG_TX] = _actual_tx_rate;
    int res = usdr_dme_set_uint(_dev->dev(), RATE_CONFIG_ENDPOINT,
                            (uintptr_t)rates.data());


    if (res) {
        SoapySDR::logf(callLogLvl(), "SoapyUSDR::setSampleRate(%d, %s, %g MHz) - error %d",
                       int(channel), (direction == SOAPY_SDR_TX) ? "TX" : "RX", rate/1e6, res);

        throw std::runtime_error("SoapyUSDR::setSampleRate() unable to set samplerate!");
    }
}

double SoapyUSDR::getSampleRate(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);

    if (direction == SOAPY_SDR_RX)
    {
        return _actual_rx_rate;
    }
    else if (direction == SOAPY_SDR_TX)
    {
        return _actual_tx_rate;
    }

    return 0;
}

SoapySDR::RangeList SoapyUSDR::getSampleRateRange(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    SoapySDR::RangeList ranges;
    const device_ranges *dev_ranges = get_ranges(device_type);
    if (!dev_ranges)
        return ranges;
    ranges.push_back(dev_ranges->samplerate_range);
    return ranges;
}

std::vector<double> SoapyUSDR::listSampleRates(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    std::vector<double> rates;
    const device_ranges *dev_ranges = get_ranges(device_type);
    if (!dev_ranges)
        return rates;
    const int min_sr = (int)std::ceil(dev_ranges->samplerate_range.minimum() / 1e6);
    const int max_sr = (int)std::floor(dev_ranges->samplerate_range.maximum() / 1e6);
    for (int i = min_sr; i <= max_sr; ++i)
    {
        rates.push_back(i * 1e6);
    }
    return rates;
}
/*******************************************************************
 * Bandwidth API
 ******************************************************************/
void SoapyUSDR::setUParam(const int direction, const char* param, const char* sub, unsigned pval)
{
    const char* dir = (direction == SOAPY_SDR_TX) ? "tx" : "rx";
    const char* pname = get_sdr_param(0, dir, param,  sub);
    int res = usdr_dme_set_uint(_dev->dev(), pname, pval);
    if (res) {
        throw std::runtime_error("SoapyUSDR::setParam(" + std::string(pname) + ") error");
    }
}

void SoapyUSDR::setBandwidth(const int direction, const size_t channel, const double bw)
{
    validateChannel(direction, channel);

    if (bw == 0.0) return; //special ignore value

    const char* dir = (direction == SOAPY_SDR_TX) ? "tx" : "rx";
    const char* pname = get_sdr_param_chan(0, dir, "bandwidth",  NULL, channel);
    int res;
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setBandwidth(%s, %g MHz)",dir, bw/1e6);

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    res = usdr_dme_set_uint(_dev->dev(), pname, bw);
    if (res)
        throw std::runtime_error("SoapyUSDR::setBandwidth(" + std::string(pname) + ") error");

    // TODO readback
    _actual_bandwidth[direction][channel] = bw;
}

double SoapyUSDR::getBandwidth(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    const auto dir_it = _actual_bandwidth.find(direction);
    if (dir_it == _actual_bandwidth.end()) {
        return 0.0;
    }

    const auto chan_it = dir_it->second.find(channel);
    if (chan_it == dir_it->second.end()) {
        return 0.0;
    }

    return chan_it->second;
}

std::vector<double> SoapyUSDR::listBandwidths(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    std::vector<double> bandwidths;
    SoapySDR::RangeList ranges = getBandwidthRange(direction, channel);
    for (const auto &range : ranges) {
        const double minimum = range.minimum();
        const double maximum = range.maximum();
        if (maximum < minimum) {
            continue;
        }

        bandwidths.push_back(minimum);
        const int min_mhz = (int)std::ceil(minimum / 1e6);
        const int max_mhz = (int)std::floor(maximum / 1e6);
        for (int mhz = min_mhz; mhz <= max_mhz; mhz++) {
            const double bandwidth = mhz * 1e6;
            if (bandwidth > minimum && bandwidth < maximum) {
                bandwidths.push_back(bandwidth);
            }
        }
        if (maximum != minimum) {
            bandwidths.push_back(maximum);
        }
    }
    return bandwidths;
}

SoapySDR::RangeList SoapyUSDR::getBandwidthRange(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    SoapySDR::RangeList bws;
    const device_ranges *dev_ranges = get_ranges(device_type);
    if (!dev_ranges)
        return bws;
    bws.push_back(dev_ranges->bandwidth_range);
    return bws;
}

/*******************************************************************
 * Clocking API
 ******************************************************************/

void SoapyUSDR::setMasterClockRate(const double rate)
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    // TODO: get reference clock in case of autodetection

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setMasterClockRate(%.3f)", rate/1e6);
}

double SoapyUSDR::getMasterClockRate(void) const
{
    double rate = 0;

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::getMasterClockRate() => %.3f", rate/1e6);
    return rate;
}

SoapySDR::RangeList SoapyUSDR::getMasterClockRates(void) const
{
    SoapySDR::RangeList clks;
    clks.push_back(SoapySDR::Range(0, 0)); // means autodetect
    clks.push_back(SoapySDR::Range(10e6, 52e6));
    return clks;
}

void SoapyUSDR::setReferenceClockRate(const double rate)
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setReferenceClockRate(%.3f MHz)", rate/1e6);
    int res = usdr_dme_set_uint(_dev->dev(), "/dm/sdr/refclk/frequency", (uint64_t)rate);
    if (res) {
        throw std::invalid_argument("SoapyUSDR::setReferenceClockRate("+std::to_string(rate)+") failed");
    }

    _ref_clock_rate = rate;
}

double SoapyUSDR::getReferenceClockRate(void) const
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);

    uint64_t rate = 0;
    int res = usdr_dme_get_uint(_dev->dev(), "/dm/sdr/refclk/frequency", &rate);
    if (res == 0) {
        return (double)rate;
    }

    return _ref_clock_rate;
}

SoapySDR::RangeList SoapyUSDR::getReferenceClockRates(void) const
{
    return getMasterClockRates();
}

std::vector<std::string> SoapyUSDR::listClockSources(void) const
{
    return { "internal", "external" };
}

void SoapyUSDR::setClockSource(const std::string &source)
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setClockSource(%s)", source.c_str());
    int res = usdr_dme_set_uint(_dev->dev(), "/dm/sdr/refclk/path", (uintptr_t)source.c_str());
    if (res) {
        throw std::invalid_argument("SoapyUSDR::setClockSource("+source+") failed");
    }

    _clk_source = source;
}

std::string SoapyUSDR::getClockSource(void) const
{
    return _clk_source;
}

/*******************************************************************
 * Time API
 ******************************************************************/

bool SoapyUSDR::hasHardwareTime(const std::string &what) const
{
    //assume hardware time when no argument is specified
    //some boards may not ever support hw time, so TODO

    return what.empty();
}

long long SoapyUSDR::getHardwareTime(const std::string &what) const
{
    long long hwtime = 0;

    if (!what.empty()) {
        throw std::invalid_argument("SoapyUSDR::getHardwareTime("+what+") unknown argument");
    }

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::getHardwareTime() => %lld", hwtime);
    return hwtime;
}

void SoapyUSDR::setHardwareTime(const long long timeNs, const std::string &what)
{
    if (!what.empty()) {
        throw std::invalid_argument("SoapyUSDR::setHardwareTime("+what+") unknown argument");
    }

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setHardwareTime(%lld)", timeNs);
}

std::vector<std::string> SoapyUSDR::listTimeSources(void) const
{
    return { "internal", "external" };
}

/*******************************************************************
 * Sensor API
 ******************************************************************/

std::vector<std::string> SoapyUSDR::listSensors(void) const
{
    std::vector<std::string> sensors;
    sensors.push_back("clock_locked");
    sensors.push_back("ref_locked");
    sensors.push_back("board_temp");
    return sensors;
}

SoapySDR::ArgInfo SoapyUSDR::getSensorInfo(const std::string &name) const
{
    SoapySDR::ArgInfo info;
    if (name == "clock_locked")
    {
        info.key = "clock_locked";
        info.name = "Clock Locked";
        info.type = SoapySDR::ArgInfo::BOOL;
        info.value = "false";
        info.description = "CGEN clock is locked, good VCO selection.";
    }
    else if (name == "ref_locked")
    {
        info.key = "ref_locked";
        info.name = "Reference Locked";
        info.type = SoapySDR::ArgInfo::BOOL;
        info.value = "false";
        info.description = "Reference clock is locked.";
    }
    else if (name == "board_temp")
    {
        info.key = "board_temp";
        info.name = "USDR board temerature";
        info.type = SoapySDR::ArgInfo::FLOAT;
        info.value = "0.0";
        info.units = "C";
        info.description = "The temperature of the USDR board in degrees C.";
    }
    return info;
}

std::string SoapyUSDR::readSensor(const std::string &name) const
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    if (name == "clock_locked")
    {
        return "true";
    }
    else if (name == "ref_locked")
    {
        return "true";
    }
    else if (name == "board_temp")
    {
        uint64_t val;
        int res = usdr_dme_get_uint(_dev->dev(), "/dm/sensor/temp", &val);
        if (res)
            return "NaN";

        int32_t v = (int32_t)((uint32_t)val);
        float temp = v / 256.0;
        return std::to_string(temp);
    }

    throw std::runtime_error("SoapyUSDR::readSensor("+name+") - unknown sensor name");
}

std::vector<std::string> SoapyUSDR::listSensors(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    std::vector<std::string> sensors;
    sensors.push_back("lo_locked");
    return sensors;
}

SoapySDR::ArgInfo SoapyUSDR::getSensorInfo(const int direction, const size_t channel, const std::string &name) const
{
    validateChannel(direction, channel);

    SoapySDR::ArgInfo info;
    if (name == "lo_locked")
    {
        info.key = "lo_locked";
        info.name = "LO Locked";
        info.type = SoapySDR::ArgInfo::BOOL;
        info.value = "false";
        info.description = "LO synthesizer is locked, good VCO selection.";
    }

    return info;
}

std::string SoapyUSDR::readSensor(const int direction, const size_t channel, const std::string &name) const
{
    validateChannel(direction, channel);

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);

    if (name == "lo_locked")
    {
        return "true";
    }

    throw std::runtime_error("SoapyUSDR::readSensor("+name+") - unknown sensor name");
}

/*******************************************************************
 * Register API
 ******************************************************************/

void SoapyUSDR::writeRegister(const unsigned addr, const unsigned /*value*/)
{
    throw std::runtime_error(
                "SoapyUSDR::WriteRegister("+std::to_string(addr)+") FAIL");
}

unsigned SoapyUSDR::readRegister(const unsigned addr) const
{
    throw std::runtime_error(
                "SoapyUSDR::ReadRegister("+std::to_string(addr)+") FAIL");
}

/*******************************************************************
 * Settings API
 ******************************************************************/
SoapySDR::ArgInfoList SoapyUSDR::getSettingInfo(void) const
{
    SoapySDR::ArgInfoList infos;

    return infos;
}

#ifdef SOAPY_SDR_API_HAS_GET_SPECIFIC_SETTING_INFO
SoapySDR::ArgInfo SoapyUSDR::getSettingInfo(const std::string &key) const
{
    const SoapySDR::ArgInfoList infos = getSettingInfo();
    for (const auto &info : infos) {
        if (info.key == key) {
            return info;
        }
    }
    throw std::runtime_error("unknown setting key: " + key);
}
#endif

void SoapyUSDR::writeSetting(const std::string &key, const std::string &value)
{
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::writeSetting(%s, %s)", key.c_str(), value.c_str());

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    if (key == "calibrate") {
        int res = usdr_dme_set_uint(_dev->dev(), "/dm/sdr/0/calibrate", (uintptr_t)value.c_str());
        if (res) {
            throw std::invalid_argument("SoapyUSDR::writeSetting("+key+") failed");
        }
    } else {
        throw std::runtime_error("unknown setting key: " + key);
    }
}

std::string SoapyUSDR::readSetting(const std::string &key) const
{
    throw std::runtime_error("unknown setting key: " + key);
}

SoapySDR::ArgInfoList SoapyUSDR::getSettingInfo(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    SoapySDR::ArgInfoList infos;
    return infos;
}

#ifdef SOAPY_SDR_API_HAS_GET_SPECIFIC_SETTING_INFO
SoapySDR::ArgInfo SoapyUSDR::getSettingInfo(const int direction, const size_t channel, const std::string &key) const
{
    const SoapySDR::ArgInfoList infos = getSettingInfo(direction, channel);
    for (const auto &info : infos) {
        if (info.key == key) {
            return info;
        }
    }
    throw std::runtime_error("unknown setting key: " + key);
}
#endif

void SoapyUSDR::writeSetting(const int direction, const size_t channel,
                             const std::string &key, const std::string &value)
{
    validateChannel(direction, channel);

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::writeSetting(%d, %d, %s, %s)", direction, (int)channel, key.c_str(), value.c_str());

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    throw std::runtime_error("unknown setting key: "+key);
}

std::string SoapyUSDR::readSetting(const int direction, const size_t channel, const std::string &key) const
{
    validateChannel(direction, channel);
    throw std::runtime_error("unknown setting key: " + key);
}

/*******************************************************************
 * I2C API
 ******************************************************************/
void SoapyUSDR::writeI2C(const int addr, const std::string &/*data*/)
{
    throw std::runtime_error(
                "SoapyUSDR::writeI2C("+std::to_string(addr)+") FAIL");
}

std::string SoapyUSDR::readI2C(const int addr, const size_t /*numBytes*/)
{
    throw std::runtime_error(
                "SoapyUSDR::readI2C("+std::to_string(addr)+") FAIL");
}

/*******************************************************************
 * SPI API
 ******************************************************************/
unsigned SoapyUSDR::transactSPI(const int addr, const unsigned /*data*/, const size_t /*numBits*/)
{
    throw std::runtime_error(
                "SoapyUSDR::transactSPI("+std::to_string(addr)+") FAIL");
}

/*******************************************************************
 * Native device API
 ******************************************************************/

void* SoapyUSDR::getNativeDeviceHandle(void) const
{
    return (void*)_dev->dev();
}

/*******************************************************************
 * Stream data structure
 ******************************************************************/
struct USDRConnectionStream
{
};

/*******************************************************************
 * Stream information
 ******************************************************************/
std::vector<std::string> SoapyUSDR::getStreamFormats(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    std::vector<std::string> formats;
    formats.push_back(SOAPY_SDR_CF32);
    formats.push_back(SOAPY_SDR_CS16);
    return formats;
}

std::string SoapyUSDR::getNativeStreamFormat(const int direction, const size_t channel, double &fullScale) const
{
    validateChannel(direction, channel);

    fullScale = 32767;
    return SOAPY_SDR_CS16;
}

SoapySDR::ArgInfoList SoapyUSDR::getStreamArgsInfo(const int direction, const size_t channel) const
{
    validateChannel(direction, channel);

    SoapySDR::ArgInfoList argInfos;

    //float scale
    {
        SoapySDR::ArgInfo info;
        info.key = "floatScale";
        info.name = "Float Scale";
        info.description = "The buffer will be scaled (or expected to be scaled) to [-floatScale;floatScale)";
        info.type = SoapySDR::ArgInfo::FLOAT;
        info.value = "1.0";
        argInfos.push_back(info);
    }

    //link format
    {
        SoapySDR::ArgInfo info;
        info.key = "linkFormat";
        info.name = "Link Format";
        info.description = "The format of the samples over the link.";
        info.type = SoapySDR::ArgInfo::STRING;
        info.options.push_back(SOAPY_SDR_CS16);
        info.optionNames.push_back("Complex int16");
        info.options.push_back(SOAPY_SDR_CS12);
        info.optionNames.push_back("Complex int12");
        info.value = SOAPY_SDR_CS16;
        argInfos.push_back(info);
    }

    //buffer length
    {
        SoapySDR::ArgInfo info;
        info.key = "bufferLength";
        info.name = "Buffer Length";
        info.description = "Hardware packet size over the link.";
        info.type = SoapySDR::ArgInfo::INT;
        info.value = std::to_string(_desired_rx_pkt);
        argInfos.push_back(info);
    }


    return argInfos;
}

/*******************************************************************
 * Stream config
 ******************************************************************/
SoapySDR::Stream *SoapyUSDR::setupStream(
        const int direction,
        const std::string &format,
        const std::vector<size_t> &channels,
        const SoapySDR::Kwargs &args)
{
    validateDirection(direction);

    size_t num_channels = channels.size();
    unsigned chmsk = 0;
    bool wire12bit = false;

    if (num_channels < 1) {
        validateChannel(direction, 0);
        num_channels = 1;
        chmsk = 1;
    } else {
        for (size_t ch: channels) {
            validateChannel(direction, ch);
            if (ch >= sizeof(chmsk) * CHAR_BIT) {
                throw std::runtime_error(std::string("SoapyUSDR::setupStream channel ") +
                                         std::to_string(ch) + " exceeds stream channel mask width");
            }
            if (chmsk & (1u << ch)) {
                throw std::runtime_error(std::string("SoapyUSDR::setupStream channel ") + std::to_string(ch) + " is already in channels mask!");
            }
            chmsk |= 1u << ch;
        }
    }

    unsigned pktSamples = 0;

    if (args.count("linkFormat")) {
        const std::string& link_fmt = args.at("linkFormat");
        if (direction == SOAPY_SDR_TX && link_fmt != SOAPY_SDR_CS16) {
            throw std::runtime_error("SoapyUSDR::setupStream([linkFormat="+link_fmt+"]) unsupported link format");
        }
        if (format == SOAPY_SDR_CS16 && link_fmt == SOAPY_SDR_CS12) {
            throw std::runtime_error("SoapyUSDR::setupStream([linkFormat="+link_fmt+"]) is only supported for complex float32 output format");
        }
        wire12bit = (link_fmt == SOAPY_SDR_CS12);
    }

    if (args.count("floatScale")) {
        const std::string& float_scale = args.at("floatScale");
        float scale = std::atof(float_scale.c_str());
        if (scale != 1.0f) {
            throw std::runtime_error("SoapyUSDR::setupStream([floatScale="+float_scale+") unsupported scale");
        }
    }

    if (args.count("bufferLength")) {
        const std::string& buffer_length = args.at("bufferLength");
        pktSamples = std::atoi(buffer_length.c_str());
        if ((pktSamples != 0) && (pktSamples < 128)) {
            throw std::runtime_error("SoapyUSDR::setupStream([bufferLength="+buffer_length+") is too small");
        }
        if (pktSamples > 128*1024) {
            throw std::runtime_error("SoapyUSDR::setupStream([bufferLength="+buffer_length+") is too large");
        }
    }

    if (direction == SOAPY_SDR_RX && _force_rx_wire12bit) {
        wire12bit = true;
    }
    const char* uformat = (format == SOAPY_SDR_CF32) ? (wire12bit ? "cf32@ci12" : "cf32" ):
                          (format == SOAPY_SDR_CS16) ? "ci16" : NULL;
    if (uformat == NULL) {
        throw std::runtime_error("SoapyUSDR::setupStream(" + format + ") unsupported format");
    }

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setupStream(%s, %s, Chans %d [0x%02x] format `%s`)\n",
                   direction == SOAPY_SDR_RX ? "RX" : "TX", format.c_str(), (unsigned)channels.size(), chmsk, uformat);

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);

    if (_streams[direction].setup && _streams[direction].active) {
        throw std::runtime_error("SoapyUSDR::setupStream(" + std::string(_streams[direction].stream) + ") is active, deactivate it first!");
    }
    if (_streams[direction].setup) {
        closeStream((SoapySDR::Stream *)&_streams[direction]);
    }

    _streams[direction].fmt = uformat;
    _streams[direction].chmsk = chmsk;
    _streams[direction].stream = direction == SOAPY_SDR_RX ? "/ll/srx/0" : "/ll/stx/0";

    if (_actual_rx_rate == 0) {
        const device_ranges *dev_ranges = get_ranges(device_type);
        if (dev_ranges)
            setSampleRate(SOAPY_SDR_RX, 0, dev_ranges->samplerate_range.minimum());
        else
            setSampleRate(SOAPY_SDR_RX, 0, DEFAULT_SAMPLE_RATE);
    }

    if (direction == SOAPY_SDR_RX) {
        const unsigned defbufsz = default_rx_packet_samples(_actual_rx_rate);

        _streams[direction].nfo.pktsyms =
            (pktSamples != 0) ? pktSamples :
            (_desired_rx_pkt != 0) ? _desired_rx_pkt : defbufsz;
    } else {
        _streams[direction].nfo.pktsyms = 0;
    }

    _streams[direction].self = this;
    USDRStream* ustr = &_streams[direction];
    int res;
    unsigned numElems = 0;

    res = usdr_dms_create_ex(_dev->dev(), ustr->stream, ustr->fmt, ustr->chmsk,
                             (numElems == 0) ? ustr->nfo.pktsyms : numElems,
                             0, &ustr->strm);
    if (res) {
        throw std::runtime_error("SoapyUSDR::setupStream not supported!");
    }

    res = usdr_dms_info(ustr->strm, &ustr->nfo);
    if (res) {
        throw std::runtime_error("SoapyUSDR::setupStream failed!");
    }

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setupStream(%s) %d Samples per packet, burst size %d * %d chs; res = %d",
                   ustr->stream, numElems, ustr->nfo.pktsyms, ustr->nfo.channels, res);

    res = usdr_dms_sync(_dev->dev(), "off", 1, &ustr->strm);
    if (res) {
        throw std::runtime_error("SoapyUSDR::setupStream failed!");
    }

    res = usdr_dms_op(ustr->strm, USDR_DMS_START, 0);
    ustr->setup = true;

    if (direction == SOAPY_SDR_RX) {
        _rx_log_chans = num_channels;
    } else {
        _tx_log_chans = num_channels;
    }

    return (SoapySDR::Stream *)&_streams[direction];
}

void SoapyUSDR::closeStream(SoapySDR::Stream *stream)
{
    USDRStream* ustr = (USDRStream*)(stream);
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::closeStream(%s)\n", ustr->stream);

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);

    if (ustr->strm) {
        usdr_dms_op(ustr->strm, USDR_DMS_STOP, 0);
        usdr_dms_destroy(ustr->strm);
        ustr->strm = NULL;
    }

    if (ustr->rxcbuf.size() > 0) {
        for (unsigned i = 0; i < ustr->rxcbuf.size(); i++) {
            ring_circbuf_destroy(ustr->rxcbuf[i]);
        }
        ustr->rxcbuf.resize(0);
    }

    ustr->setup = false;
}

size_t SoapyUSDR::getStreamMTU(SoapySDR::Stream *stream) const
{
    USDRStream* ustr = (USDRStream*)(stream);

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::getStreamMTU(%s) => %d",
                   ustr->stream, ustr->nfo.pktsyms);

    return ustr->nfo.pktsyms;
}

int SoapyUSDR::activateStream(
        SoapySDR::Stream *stream,
        const int flags,
        const long long timeNs,
        const size_t numElems)
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    USDRStream* ustr = (USDRStream*)(stream);
    bool tx_dir = (ustr == &_streams[SOAPY_SDR_TX]);
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::activateStream(%s, @ %lld ns, %d samples, %08x)",
                   ustr->stream, timeNs, (unsigned)numElems, flags);

    int res = usdr_dms_sync(_dev->dev(), tx_dir ? "tx" : "rx", 1, &ustr->strm);
    if (res)
        return SOAPY_SDR_STREAM_ERROR;

    ustr->active = true;
    return 0;
}

int SoapyUSDR::deactivateStream(
        SoapySDR::Stream *stream,
        const int flags,
        const long long timeNs)
{
    USDRStream* ustr = (USDRStream*)(stream);
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::deactivateStream(%s, @ %lld ns, %08x)",
                   ustr->stream, timeNs, flags);

    ustr->active = false;
    return 0;
}

/*******************************************************************
 * Stream API
 ******************************************************************/
int SoapyUSDR::readStream(
        SoapySDR::Stream *stream,
        void * const *buffs,
        size_t numElems,
        int &flags,
        long long &timeNs,
        const long timeoutUs)
{
    USDRStream* ustr = (USDRStream*)(stream);
    if (!ustr->active) {
        if (timeoutUs > 0) {
            usleep(timeoutUs);
        }
        return SOAPY_SDR_TIMEOUT;
    }

    int res;
    struct usdr_dms_recv_nfo nfo;

    //handle the one packet flag by clipping
    if ((flags & SOAPY_SDR_ONE_PACKET) != 0) {
        numElems = std::min(numElems, (size_t)ustr->nfo.pktsyms);
    }

    if (ustr->rxcbuf.size() > 0) {
        size_t req_bytes = numElems * ustr->nfo.pktbszie / ustr->nfo.pktsyms;
        do {
            // fprintf(stderr, "rxcb wpos=%lld rpos=%lld req_bytes=%lld\n",
            //         (long long)ustr->rxcbuf->wpos,
            //         (long long)ustr->rxcbuf->rpos,
            //         (long long)req_bytes);

            bool have_all_channels = true;
            for (unsigned i = 0; i < ustr->rxcbuf.size(); i++) {
                if (ring_circbuf_rspace(ustr->rxcbuf[i]) < req_bytes) {
                    have_all_channels = false;
                    break;
                }
            }

            if (have_all_channels) {
                for (unsigned i = 0; i < ustr->rxcbuf.size(); i++) {
                    // TODO: decide how to handle alignment requirements for user-provided stream buffers.
                    ring_circbuf_read(ustr->rxcbuf[i], buffs[i], req_bytes);
                }

                flags &= ~SOAPY_SDR_HAS_TIME;
                timeNs = 0;
                return numElems;
            }

            // We don't have enough data here
            std::vector<void*> chans(ustr->rxcbuf.size());
            for (unsigned i = 0; i < ustr->rxcbuf.size(); i++) {
                chans[i] = ring_circbuf_wptr(ustr->rxcbuf[i]);
            }

            res = usdr_dms_recv(ustr->strm, chans.data(), timeoutUs / 1000, &nfo);
            if (res == 0) {
                for (unsigned i = 0; i < ustr->rxcbuf.size(); i++) {
                    ustr->rxcbuf[i]->wpos += ustr->nfo.pktbszie;
                }
                last_recv_pkt_time = nfo.fsymtime;
            }
        } while (res == 0);

        return SOAPY_SDR_TIMEOUT;
    } else {
        if (numElems != ustr->nfo.pktsyms) {
            size_t blksz;
            blksz = ustr->nfo.pktsyms * 16;
            while (blksz < numElems * 2)
                blksz <<= 1;

            size_t blksz_bytes = blksz * ustr->nfo.pktbszie / ustr->nfo.pktsyms;

            SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::readStream(%s) requested %d but block is configured for %d, injecting jitter buffer of %d bytes. Performance will be degraded",
                           ustr->stream, numElems, ustr->nfo.pktsyms, blksz_bytes);

            ustr->rxcbuf.resize(_rx_log_chans);
            for (unsigned i = 0; i < _rx_log_chans; i++) {
                ustr->rxcbuf[i] = ring_circbuf_create(blksz_bytes);
            }

            // Reenter
            return readStream(stream, buffs, numElems, flags, timeNs, timeoutUs);
        }

        // TODO: decide how to handle alignment requirements for user-provided stream buffers.
        res = usdr_dms_recv(ustr->strm, (void**)buffs, timeoutUs / 1000, &nfo);

        if (rd && res == 0) {
            const size_t bytes_per_channel = nfo.totsyms * ustr->nfo.pktbszie / ustr->nfo.pktsyms;
            for (unsigned i = 0; i < _rx_log_chans; i++) {
                fwrite(buffs[i], bytes_per_channel, 1, rd);
            }

            const float marker[2] = { -2.0f, 2.0f };
            fwrite(marker, sizeof(marker), 1, rd);
        }

        flags |= SOAPY_SDR_HAS_TIME;
        timeNs = SoapySDR::ticksToTimeNs(nfo.fsymtime, _actual_rx_rate);

        last_recv_pkt_time = nfo.fsymtime;
        return (res) ? SOAPY_SDR_TIMEOUT : nfo.totsyms;
    }
}

int SoapyUSDR::writeStream(SoapySDR::Stream *stream,
    const void *const *buffs,
    const size_t numElems,
    int &flags,
    const long long timeNs,
    const long timeoutUs)
{
    USDRStream *ustr = (USDRStream *) (stream);
    long long ts;
    if (flags & SOAPY_SDR_HAS_TIME) {
        ts = SoapySDR::timeNsToTicks(timeNs, _actual_tx_rate) + _txcorr;
        this->calc_ts = ts;
    } else {
        ts = this->calc_ts;
    }

    int64_t lag = ts - last_recv_pkt_time;
    if (tx_pkts == 0) {
        avg_gap = lag;
    } else {
        double alpha = 0.01;
        avg_gap = (1 - alpha) * avg_gap + alpha * lag;
    }

    SoapySDR::logf(SOAPY_SDR_DEBUG, "writeStream::writeStream(%s) @ %lld num %d should be %d\n", ustr->stream, ts, numElems, ustr->nfo.pktsyms);

    unsigned toSend = numElems;
    // TODO: decide how to handle alignment requirements for user-provided stream buffers.
    int res = usdr_dms_send(ustr->strm, (const void **) buffs, numElems, ts, timeoutUs / 1000);
    if (this->calc_ts >= 0)
        this->calc_ts += numElems;

    if (tx_pkts % 1000 == 0) {
        SoapySDR::logf(_dump_calls ? SOAPY_SDR_ERROR : SOAPY_SDR_TRACE,
                       "TX %lld / %d -> lag %f (%d)", timeNs, numElems, avg_gap, lag);
    }

    tx_pkts++;
    return (res) ? SOAPY_SDR_TIMEOUT : toSend;
}

int SoapyUSDR::readStreamStatus(
        SoapySDR::Stream *stream,
        size_t &chanMask,
        int &flags,
        long long &timeNs,
        const long timeoutUs)
{
    (void)stream;
    (void)chanMask;
    (void)flags;
    (void)timeNs;
    (void)timeoutUs;

    return SOAPY_SDR_NOT_SUPPORTED;
}
