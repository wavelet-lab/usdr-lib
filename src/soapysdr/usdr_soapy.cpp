// Copyright (c) 2023-2024 Wavelet Lab
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
#include <cmath>
#include <string.h>

// #include <usdr_logging.h>

std::map<std::string, std::weak_ptr<usdr_handle>> usdr_handle::s_created;

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

const rfic_gain_descriptor lms7_gains[] {
    { SOAPY_SDR_RX, SoapySDR::Range(0.0, 30.0),   "LNA", nullptr, nullptr, "/dm/sdr/0/rx/gain/lna" },
    { SOAPY_SDR_RX, SoapySDR::Range(0.0, 12.0),   "TIA", "VGA",   "VGA1",  "/dm/sdr/0/rx/gain/vga" },
    { SOAPY_SDR_RX, SoapySDR::Range(-12.0, 19.0), "PGA", "VGA2",  nullptr, "/dm/sdr/0/rx/gain/pga" },
    { SOAPY_SDR_TX, SoapySDR::Range(-52.0, 0.0),  "PAD", nullptr, nullptr, "/dm/sdr/0/tx/gain" },
    { 0, SoapySDR::Range(), nullptr, nullptr, nullptr, nullptr }
};

const rfic_gain_descriptor lms6_gains[] {
    { SOAPY_SDR_RX, SoapySDR::Range(0.0, 6.0),    "LNA",  nullptr, nullptr, "/dm/sdr/0/rx/gain/lna" },
    { SOAPY_SDR_RX, SoapySDR::Range(5, 31),       "VGA1", "TIA",   nullptr, "/dm/sdr/0/rx/gain/vga" },
    { SOAPY_SDR_RX, SoapySDR::Range(0, 60.0),     "VGA2", "PGA",   nullptr, "/dm/sdr/0/rx/gain/pga" },
    { SOAPY_SDR_TX, SoapySDR::Range(-35.0, -4),   "VGA1", nullptr, nullptr, "/dm/sdr/0/tx/gain/vga1" },
    { SOAPY_SDR_TX, SoapySDR::Range(0.0, 25.0),   "VGA2", nullptr, nullptr,  "/dm/sdr/0/tx/gain/vga2" },
    { 0, SoapySDR::Range(), nullptr, nullptr, nullptr, nullptr }
};

const rfic_gain_descriptor ad45lb49_gains[] {
    { SOAPY_SDR_RX, SoapySDR::Range(0, 4),        "LNA",  "SEL",   nullptr, "/dm/sdr/0/rx/gain/lna" },
    { SOAPY_SDR_RX, SoapySDR::Range(0, 31),       "VGA",  "ATTN",  nullptr, "/dm/sdr/0/rx/gain/vga" },
    { SOAPY_SDR_RX, SoapySDR::Range(0, 31),       "PGA",  nullptr, nullptr, "/dm/sdr/0/rx/gain/pga" },
    { 0, SoapySDR::Range(), nullptr, nullptr, nullptr, nullptr }
};

const rfic_gain_descriptor unk_gains[] {
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const char* SoapyUSDR::get_sdr_param(int sdridx, const char* dir, const char* par, const char* subpar)
{
    if (subpar) {
        snprintf(_param_name, sizeof(_param_name), "/dm/sdr/%d/%s/%s/%s", sdridx, dir, par, subpar);
    } else {
        snprintf(_param_name, sizeof(_param_name), "/dm/sdr/%d/%s/%s", sdridx, dir, par);
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


    std::string dev = (args.count("dev")) ? args.at("dev") : "";

    if (args.count("loglevel")) {
        loglevel = std::stoi(args.at("loglevel"));
    }
    if (args.count("fe")) {
        if (dev.length() != 0) {
            dev += ",";
        }
        dev += "fe=";
        dev += args.at("fe").c_str();
    }
    if (args.count("bus")) {
        if (dev.length() > 0) {
            dev += ",";
        }
        dev += "bus=";
        dev += args.at("bus").c_str();
    }
    if (args.count("txcorr")) {
        _txcorr = atoi(args.at("txcorr").c_str());
    }
    if (args.count("calls")) {
        _dump_calls = atoi(args.at("calls").c_str()) ? true : false;
    }

    usdrlog_setlevel(NULL, loglevel);


    SoapySDR::logf(callLogLvl(), "Make connection: '%s'", args.count("dev") ? args.at("dev").c_str() : "*");
    for (auto& i: args) {
        SoapySDR::logf(callLogLvl(), "Param %s => %s", i.first.c_str(), i.second.c_str());
    }
    _dev = usdr_handle::get(dev);

    if (args.count("refclk")) {
        setClockSource("internal");
        SoapySDR::logf(callLogLvl(), "SoapyUSDR::SoapyUSDR() set ref to internal clock");
    }
    if (args.count("extclk")) {
        setClockSource("external");
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
    int res = usdr_dme_get_uint(_dev->dev(), "/ll/sdr/0/rfic/0", &val);
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

bool SoapyUSDR::getFullDuplex(const int /*direction*/, const size_t /*channel*/) const
{
    return true;
}

/*******************************************************************
 * Antenna API
 ******************************************************************/
std::vector<std::string> SoapyUSDR::listAntennas(const int direction, const size_t /*channel*/) const
{
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
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setAntenna(%d, %d, %s)", direction, int(channel), name.c_str());

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
}

std::string SoapyUSDR::getAntenna(const int direction, const size_t channel) const
{
    std::string antenna = "";

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::getAntenna(%d, %d, %s)", direction, int(channel), antenna.c_str());
    return antenna;
}

/*******************************************************************
 * Frontend corrections API
 ******************************************************************/

bool SoapyUSDR::hasDCOffsetMode(const int direction, const size_t /*channel*/) const
{
    return (direction == SOAPY_SDR_RX);
}

void SoapyUSDR::setDCOffsetMode(const int direction, const size_t /*channel*/, const bool /*automatic*/)
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    if (direction == SOAPY_SDR_RX) {
    }
}

bool SoapyUSDR::getDCOffsetMode(const int direction, const size_t /*channel*/) const
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    if (direction == SOAPY_SDR_RX) {
    }

    return false;
}

bool SoapyUSDR::hasDCOffset(const int direction, const size_t /*channel*/) const
{
    return (direction == SOAPY_SDR_TX);
}

void SoapyUSDR::setDCOffset(const int direction, const size_t channel, const std::complex<double> &offset)
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    const char* dir = (direction == SOAPY_SDR_TX) ? "tx" : "rx";
    const char* pname = get_sdr_param(0, dir, "dccorr", NULL);

    // Convert from normalized [-1.0, 1.0] to hardware range (typically 16-bit signed)
    // Hardware expects: value = (channel << 32) | (I << 16) | (Q << 0)
    int16_t i_val = (int16_t)(offset.real() * 32767.0);
    int16_t q_val = (int16_t)(offset.imag() * 32767.0);
    uint64_t value = ((uint64_t)channel << 32) | ((uint64_t)(uint16_t)i_val << 16) | (uint64_t)(uint16_t)q_val;

    int res = usdr_dme_set_uint(_dev->dev(), pname, value);
    if (res) {
        SoapySDR::logf(SOAPY_SDR_DEBUG, "SoapyUSDR::setDCOffset(%s) not supported or failed: error %d", dir, res);
    } else {
        SoapySDR::logf(callLogLvl(), "SoapyUSDR::setDCOffset(%s, I=%g, Q=%g) set successfully", dir, offset.real(), offset.imag());
    }
}

std::complex<double> SoapyUSDR::getDCOffset(const int /*direction*/, const size_t /*channel*/) const
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    double I = 0.0, Q = 0.0;
    return std::complex<double>(I, Q);
}

bool SoapyUSDR::hasIQBalance(const int /*direction*/, const size_t /*channel*/) const
{
    return true;
}

void SoapyUSDR::setIQBalance(const int direction, const size_t channel, const std::complex<double> &balance)
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    const char* dir = (direction == SOAPY_SDR_TX) ? "tx" : "rx";
    const char* pname = get_sdr_param(0, dir, "phgaincorr", NULL);

    // Hardware expects: value = (phase_corr << 48) | (channel << 32) | (Q_gain << 16) | (I_gain << 0)
    // balance.real() = gain imbalance (I/Q gain ratio deviation from 1.0)
    // balance.imag() = phase imbalance in degrees
    //
    // Convert gain from ratio to hardware format (typically 2048 = 1.0)
    int16_t i_gain = 2048;  // Default: unity gain
    int16_t q_gain = (int16_t)(2048.0 * (1.0 + balance.real()));
    // Convert phase from normalized to hardware format (typically 2048 = 180 degrees)
    int16_t phase_corr = (int16_t)(balance.imag() * 2048.0 / 180.0);

    uint64_t value = ((uint64_t)(uint16_t)phase_corr << 48) | ((uint64_t)channel << 32) |
                     ((uint64_t)(uint16_t)q_gain << 16) | (uint64_t)(uint16_t)i_gain;

    int res = usdr_dme_set_uint(_dev->dev(), pname, value);
    if (res) {
        SoapySDR::logf(SOAPY_SDR_DEBUG, "SoapyUSDR::setIQBalance(%s) not supported or failed: error %d", dir, res);
    } else {
        SoapySDR::logf(callLogLvl(), "SoapyUSDR::setIQBalance(%s, gain=%g, phase=%g deg) set successfully",
                       dir, balance.real(), balance.imag());
    }
}

std::complex<double> SoapyUSDR::getIQBalance(const int /*direction*/, const size_t /*channel*/) const
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    return std::complex<double>(0,0);
}

/*******************************************************************
 * Gain API
 ******************************************************************/

std::vector<std::string> SoapyUSDR::listGains(const int direction, const size_t /*channel*/) const
{
    std::vector<std::string> gain_list;
    const rfic_gain_descriptor* gains = get_gains(type);
    for (unsigned i = 0; gains[i].name != nullptr; i++) {
        if (gains[i].direction == direction) {
            gain_list.push_back(gains[i].name);
        }
    }

    return gain_list;
}

void SoapyUSDR::setGain(const int direction, const size_t channel, const double value)
{
    const char* dir = (direction == SOAPY_SDR_TX) ? "tx" : "rx";
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setGain(%s, %d, %g dB)", dir, int(channel), value);

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    const char* defparam = get_sdr_param(0, dir, "gain", "auto");
    int res = usdr_dme_set_uint(_dev->dev(), defparam, value);
    if (res) {
        SoapySDR::logf(callLogLvl(), "SoapyUSDR::setGain(%s, %d, %g dB) => %s failed %d",
                       dir, int(channel), value, defparam, res);
    }
}

void SoapyUSDR::setGain(const int direction, const size_t channel, const std::string &name, const double value)
{
    const char* dir = (direction == SOAPY_SDR_TX) ? "tx" : "rx";
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setGain(%s, %d, %s, %g dB)",
                   direction == SOAPY_SDR_RX ? "RX" : "TX",
                   int(channel), name.c_str(), value);

    const rfic_gain_descriptor* gains = get_gains(type);
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    const char* defparam = get_sdr_param(0, dir, "gain", nullptr);
    unsigned i;

    for (i = 0; gains[i].name != nullptr; i++) {
        if ((gains[i].direction == direction) && (name == gains[i].name ||
                (gains[i].altname && (name == gains[i].altname)) || (gains[i].altname2 && (name == gains[i].altname2)))) {
            defparam = gains[i].property_name;
            break;
        }
    }

    int res = usdr_dme_set_uint(_dev->dev(), defparam, value);
    if (res)
        throw std::runtime_error(std::string("SoapyUSDR::setGain(") + defparam + ", " + std::to_string((int)value) + ")");

    _actual_gains[i] = value;
}

double SoapyUSDR::getGain(const int direction, const size_t channel, const std::string &name) const
{
    const rfic_gain_descriptor* gains = get_gains(type);
    unsigned i;
    for (i = 0; gains[i].name != nullptr; i++) {
        if ((gains[i].direction == direction) && (name == gains[i].name ||
                (gains[i].altname && (name == gains[i].altname)) || (gains[i].altname2 && (name == gains[i].altname2)))) {
            break;
        }
    }

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::getGain(%s, %d, %s) => %g dB",
                   direction == SOAPY_SDR_RX ? "RX" : "TX",
                   int(channel), name.c_str(), _actual_gains[i]);
    return _actual_gains[i];
}

SoapySDR::Range SoapyUSDR::getGainRange(const int direction, const size_t channel) const
{
    if (direction == SOAPY_SDR_RX)
    {
        //make it so gain of 0.0 sets PGA at its mid-range
        return SoapySDR::Range(-12.0, 19.0+12.0+30.0);
    }
    return SoapySDR::Device::getGainRange(direction, channel);
}

SoapySDR::Range SoapyUSDR::getGainRange(const int direction, const size_t channel, const std::string &name) const
{
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

void SoapyUSDR::setFrequency(const int direction, const size_t channel, const std::string &name, const double frequency, const SoapySDR::Kwargs &/*args*/)
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setFrequency(%s, %d, %s, %g MHz)",
                   direction == SOAPY_SDR_RX ? "RX" : "TX",
                   int(channel), name.c_str(), frequency/1e6);
    int res;

    const char* dir = (direction == SOAPY_SDR_TX) ? "tx" : "rx";
    const char* pname = get_sdr_param(0, dir, "freqency", (name == "BB") ? "bb" : NULL);

    uint64_t val = (((uint64_t)channel) << 32) | (uint32_t)frequency;

    res = usdr_dme_set_uint(_dev->dev(), pname,
                            type == RFIC_AFE79XX ? (uint64_t)frequency : val);
    if (res)
        throw std::runtime_error(std::string("SoapyUSDR::setFrequency(") + pname + ", " + ")");

    _actual_frequency[direction] = val;
}

double SoapyUSDR::getFrequency(const int direction, const size_t channel, const std::string &name) const
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::getFrequency(%d, %s)", int(channel), name.c_str());

    return _actual_frequency[direction];
}

std::vector<std::string> SoapyUSDR::listFrequencies(const int /*direction*/, const size_t /*channel*/) const
{
    std::vector<std::string> opts;
    opts.push_back("RF");
    //opts.push_back("BB");
    return opts;
}

SoapySDR::RangeList SoapyUSDR::getFrequencyRange(const int /*direction*/, const size_t /*channel*/, const std::string &name) const
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    SoapySDR::RangeList ranges;
    if (name == "RF")
    {
        if (type == RFIC_AFE79XX) {
            ranges.push_back(SoapySDR::Range(5e6, 12.5e9));
        } else {
            ranges.push_back(SoapySDR::Range(1e5, 3.8e9));
        }
    }
    else if (name == "BB")
    {
        uint64_t out = 80e6;
        // if (res)
        //    ranges.push_back(SoapySDR::Range(-0.0, 0.0));
        //else
            ranges.push_back(SoapySDR::Range(-(double)out / 2, (double)out / 2));
    }
    return ranges;
}

SoapySDR::RangeList SoapyUSDR::getFrequencyRange(const int /*direction*/, const size_t /*channel*/) const
{
    SoapySDR::RangeList ranges;
    if (type == RFIC_AFE79XX) {
        ranges.push_back(SoapySDR::Range(5e6, 12.5e9));
    } else {
        ranges.push_back(SoapySDR::Range(0e6, 3.8e9));
    }
    return ranges;
}

/*******************************************************************
 * Sample Rate API
 ******************************************************************/

void SoapyUSDR::setSampleRate(const int direction, const size_t channel, const double rate)
{
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

    unsigned rates[4] = { _actual_rx_rate, _actual_tx_rate, 0, 0 };
    int res = usdr_dme_set_uint(_dev->dev(), "/dm/rate/rxtxadcdac",
                            (uintptr_t)&rates[0]);


    if (res) {
        SoapySDR::logf(callLogLvl(), "SoapyUSDR::setSampleRate(%d, %s, %g MHz) - error %d",
                       int(channel), (direction == SOAPY_SDR_TX) ? "TX" : "RX", rate/1e6, res);

        throw std::runtime_error("SoapyUSDR::setSampleRate() unable to set samplerate!");
    }
}

double SoapyUSDR::getSampleRate(const int direction, const size_t /*channel*/) const
{
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

SoapySDR::RangeList SoapyUSDR::getSampleRateRange(const int /*direction*/, const size_t /*channel*/) const
{
    SoapySDR::RangeList ranges;
    ranges.push_back(SoapySDR::Range((type == RFIC_AFE79XX) ? 1.92e6 : 0.1e6,
                                     (type == RFIC_AFE79XX) ? 500e6 : (type == RFIC_AD45LB49) ? 130e6 : 80e6));
    return ranges;
}

std::vector<double> SoapyUSDR::listSampleRates(const int /*direction*/, const size_t /*channel*/) const
{
    std::vector<double> rates;
    for (int i = 2; i < 57; i++)
    {
        rates.push_back(i*1e6);
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
    (void)channel;

    if (bw == 0.0) return; //special ignore value

    const char* dir = (direction == SOAPY_SDR_TX) ? "tx" : "rx";
    const char* pname = get_sdr_param(0, dir, "bandwidth",  NULL);
    int res;
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setBandwidth(%s, %g MHz)",dir, bw/1e6);

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    res = usdr_dme_set_uint(_dev->dev(), pname, bw);
    if (res)
        throw std::runtime_error("SoapyUSDR::setBandwidth(" + std::string(pname) + ") error");

    // Read back actual bandwidth from device
    uint64_t actual_bw = 0;
    if (usdr_dme_get_uint(_dev->dev(), pname, &actual_bw) == 0) {
        _actual_bandwidth[direction] = actual_bw;
        SoapySDR::logf(SOAPY_SDR_DEBUG, "SoapyUSDR::setBandwidth() readback: requested=%g, actual=%g", bw, (double)actual_bw);
    } else {
        // Fallback to requested value if readback not supported
        _actual_bandwidth[direction] = bw;
    }
}

double SoapyUSDR::getBandwidth(const int direction, const size_t /*channel*/) const
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    return _actual_bandwidth[direction];
}

SoapySDR::RangeList SoapyUSDR::getBandwidthRange(const int /*direction*/, const size_t /*channel*/) const
{
    SoapySDR::RangeList bws;
    bws.push_back(SoapySDR::Range(0.5e6, 80e6));
    return bws;
}

/*******************************************************************
 * Clocking API
 ******************************************************************/

void SoapyUSDR::setMasterClockRate(const double rate)
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);

    // Try to set reference clock frequency if device supports it
    int res = usdr_dme_set_uint(_dev->dev(), "/dm/sdr/refclk/frequency", (uint64_t)rate);
    if (res) {
        SoapySDR::logf(SOAPY_SDR_WARNING, "SoapyUSDR::setMasterClockRate(%.3f MHz) not supported or failed: error %d", rate/1e6, res);
    } else {
        SoapySDR::logf(callLogLvl(), "SoapyUSDR::setMasterClockRate(%.3f MHz) set successfully", rate/1e6);
    }
}

double SoapyUSDR::getMasterClockRate(void) const
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    uint64_t rate = 0;

    // Try to get reference clock frequency
    int res = usdr_dme_get_uint(_dev->dev(), "/dm/sdr/refclk/frequency", &rate);
    if (res) {
        // Fallback: some devices don't expose refclk, return 0 for autodetect
        SoapySDR::logf(SOAPY_SDR_DEBUG, "SoapyUSDR::getMasterClockRate() not available, using autodetect");
        return 0;
    }

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::getMasterClockRate() => %.3f MHz", rate/1e6);
    return (double)rate;
}

SoapySDR::RangeList SoapyUSDR::getMasterClockRates(void) const
{
    SoapySDR::RangeList clks;
    clks.push_back(SoapySDR::Range(0, 0)); // means autodetect
    clks.push_back(SoapySDR::Range(10e6, 52e6));
    return clks;
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
    if (!what.empty()) {
        return false;
    }

    // Check if device supports hardware time by querying /dm/debug/rxtime
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    uint64_t dummy = 0;
    int res = usdr_dme_get_uint(_dev->dev(), "/dm/debug/rxtime", &dummy);

    return (res == 0);
}

long long SoapyUSDR::getHardwareTime(const std::string &what) const
{
    if (!what.empty()) {
        throw std::invalid_argument("SoapyUSDR::getHardwareTime("+what+") unknown argument");
    }

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    uint64_t hwtime = 0;
    int res = usdr_dme_get_uint(_dev->dev(), "/dm/debug/rxtime", &hwtime);
    if (res) {
        SoapySDR::logf(SOAPY_SDR_WARNING, "SoapyUSDR::getHardwareTime() failed: error %d", res);
        return 0;
    }

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::getHardwareTime() => %lld", (long long)hwtime);
    return (long long)hwtime;
}

void SoapyUSDR::setHardwareTime(const long long timeNs, const std::string &what)
{
    if (!what.empty()) {
        throw std::invalid_argument("SoapyUSDR::setHardwareTime("+what+") unknown argument");
    }

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);

    // For xMASS multi-board sync: timeNs=0 triggers a timestamp reset/sync
    // This synchronizes all boards to a common time reference (1PPS/SYSREF)
    if (timeNs == 0) {
        // Trigger sync to external reference (1PPS/SYSREF) - resets timestamp counters
        int res = usdr_dms_sync(_dev->dev(), "1pps", 0, NULL);
        if (res) {
            SoapySDR::logf(SOAPY_SDR_WARNING,
                "SoapyUSDR::setHardwareTime(0) sync trigger failed: error %d", res);
        } else {
            SoapySDR::logf(callLogLvl(),
                "SoapyUSDR::setHardwareTime(0) triggered timestamp sync to 1PPS/SYSREF");
        }
    } else {
        // Absolute timestamp setting is not supported by hardware
        // The hardware uses sync events (1PPS, SYSREF) for multi-board alignment
        SoapySDR::logf(SOAPY_SDR_WARNING,
            "SoapyUSDR::setHardwareTime(%lld) absolute time not supported, use 0 to trigger sync",
            timeNs);
    }
}

/*******************************************************************
 * Sensor API
 ******************************************************************/

std::vector<std::string> SoapyUSDR::listSensors(void) const
{
    std::vector<std::string> sensors;
    sensors.push_back("clock_locked");
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

std::vector<std::string> SoapyUSDR::listSensors(const int /*direction*/, const size_t /*channel*/) const
{
    std::vector<std::string> sensors;
    sensors.push_back("lo_locked");
    return sensors;
}

SoapySDR::ArgInfo SoapyUSDR::getSensorInfo(const int /*direction*/, const size_t /*channel*/, const std::string &name) const
{
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

std::string SoapyUSDR::readSensor(const int /*direction*/, const size_t /*channel*/, const std::string &name) const
{
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

void SoapyUSDR::writeSetting(const std::string &key, const std::string &value)
{
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::writeSetting(%s, %s)", key.c_str(), value.c_str());

    throw std::runtime_error("unknown setting key: " + key);
}

SoapySDR::ArgInfoList SoapyUSDR::getSettingInfo(const int direction, const size_t /*channel*/) const
{
    SoapySDR::ArgInfoList infos;

    // Auto Gain Control (RX only)
    if (direction == SOAPY_SDR_RX) {
        SoapySDR::ArgInfo agc_info;
        agc_info.key = "agc";
        agc_info.name = "Automatic Gain Control";
        agc_info.type = SoapySDR::ArgInfo::BOOL;
        agc_info.value = "false";
        agc_info.description = "Enable automatic gain control (AGC) for this channel";
        infos.push_back(agc_info);
    }

    // Digital Step Attenuator
    SoapySDR::ArgInfo dsa_info;
    dsa_info.key = "dsa";
    dsa_info.name = "Digital Step Attenuator";
    dsa_info.type = SoapySDR::ArgInfo::FLOAT;
    dsa_info.value = "0.0";
    dsa_info.range = SoapySDR::Range(0.0, 31.5, 0.5);
    dsa_info.units = "dB";
    dsa_info.description = "Digital step attenuator attenuation in dB";
    infos.push_back(dsa_info);

    // Baseband NCO Frequency Offset
    SoapySDR::ArgInfo nco_info;
    nco_info.key = "bb_freq";
    nco_info.name = "Baseband NCO Frequency";
    nco_info.type = SoapySDR::ArgInfo::FLOAT;
    nco_info.value = "0.0";
    nco_info.units = "Hz";
    nco_info.description = "Baseband NCO frequency offset for this channel";
    infos.push_back(nco_info);

    // DC Offset Correction Mode (RX only)
    if (direction == SOAPY_SDR_RX) {
        SoapySDR::ArgInfo dc_mode_info;
        dc_mode_info.key = "dc_corr_mode";
        dc_mode_info.name = "DC Offset Correction Mode";
        dc_mode_info.type = SoapySDR::ArgInfo::STRING;
        dc_mode_info.value = "auto";
        dc_mode_info.options.push_back("off");
        dc_mode_info.options.push_back("auto");
        dc_mode_info.optionNames.push_back("Off");
        dc_mode_info.optionNames.push_back("Automatic");
        dc_mode_info.description = "DC offset correction mode";
        infos.push_back(dc_mode_info);
    }

    return infos;
}

void SoapyUSDR::writeSetting(const int direction, const size_t channel,
                             const std::string &key, const std::string &value)
{
    SoapySDR::logf(callLogLvl(), "SoapyUSDR::writeSetting(%s, ch=%d, %s=%s)",
                   (direction == SOAPY_SDR_TX) ? "TX" : "RX", (int)channel, key.c_str(), value.c_str());

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    const char* dir = (direction == SOAPY_SDR_TX) ? "tx" : "rx";
    int res = 0;

    // Auto Gain Control
    if (key == "agc" && direction == SOAPY_SDR_RX) {
        bool enable = (value == "true" || value == "1");
        char path[128];
        snprintf(path, sizeof(path), "/dm/sdr/0/rx/gain/auto/%zu", channel);
        res = usdr_dme_set_uint(_dev->dev(), path, enable ? 1 : 0);
        if (res) {
            throw std::runtime_error("Failed to set AGC: error " + std::to_string(res));
        }
        return;
    }

    // Digital Step Attenuator
    if (key == "dsa") {
        double dsa_db = std::stod(value);
        char path[128];
        snprintf(path, sizeof(path), "/dm/sdr/0/%s/dsa/%zu", dir, channel);
        // DSA value is typically in 0.5dB steps, hardware expects integer
        res = usdr_dme_set_uint(_dev->dev(), path, (uint64_t)(dsa_db * 2.0));
        if (res) {
            throw std::runtime_error("Failed to set DSA: error " + std::to_string(res));
        }
        return;
    }

    // Baseband NCO Frequency Offset
    if (key == "bb_freq") {
        int32_t freq_hz = (int32_t)std::stoi(value);
        char path[128];
        snprintf(path, sizeof(path), "/dm/sdr/0/%s/freqency/bb", dir);
        // Hardware expects: (channel << 32) | freq
        uint64_t val = ((uint64_t)channel << 32) | (uint32_t)freq_hz;
        res = usdr_dme_set_uint(_dev->dev(), path, val);
        if (res) {
            throw std::runtime_error("Failed to set BB freq: error " + std::to_string(res));
        }
        return;
    }

    // DC Offset Correction Mode
    if (key == "dc_corr_mode" && direction == SOAPY_SDR_RX) {
        bool enable = (value == "auto");
        char path[128];
        snprintf(path, sizeof(path), "/dm/sdr/0/rx/dccorrmode");
        // Hardware expects: (channel << 32) | mode
        uint64_t val = ((uint64_t)channel << 32) | (enable ? 1 : 0);
        res = usdr_dme_set_uint(_dev->dev(), path, val);
        if (res) {
            throw std::runtime_error("Failed to set DC corr mode: error " + std::to_string(res));
        }
        return;
    }

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
 * Stream data structure
 ******************************************************************/
struct USDRConnectionStream
{
};

/*******************************************************************
 * Stream information
 ******************************************************************/
std::vector<std::string> SoapyUSDR::getStreamFormats(const int /*direction*/, const size_t /*channel*/) const
{
    std::vector<std::string> formats;
    formats.push_back(SOAPY_SDR_CF32);
    formats.push_back(SOAPY_SDR_CS16);
    return formats;
}

std::string SoapyUSDR::getNativeStreamFormat(const int /*direction*/, const size_t /*channel*/, double &fullScale) const
{
    fullScale = 32768;
    return SOAPY_SDR_CS16;
}

SoapySDR::ArgInfoList SoapyUSDR::getStreamArgsInfo(const int /*direction*/, const size_t /*channel*/) const
{
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
        info.value = _desired_rx_pkt;
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
    size_t num_channels = channels.size();
    size_t chmsk = 0;
    bool wire12bit = false;

    if (num_channels < 1) {
        num_channels = 1;
        chmsk = 1;
    } else {
        for (size_t ch: channels) {
            if (chmsk & (1 << ch)) {
                throw std::runtime_error(std::string("SoapyUSDR::setupStream channel ") + std::to_string(ch) + " is already in channels mask!");
            }
            chmsk |= 1 << ch;
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

    if (direction == SOAPY_SDR_RX) {
        // We need a better way to calculate packet size
        unsigned defbufsz =
            (_actual_rx_rate >= 7.6e6) ? 7680 : (_actual_rx_rate >= 3.8e6) ? 3840 : 1920;

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

    if (_actual_rx_rate == 0) {
        setSampleRate(SOAPY_SDR_RX, 0, 1.92e6);
    }

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
    while (!ustr->active )
        usleep(1000);

    int res;
    struct usdr_dms_recv_nfo nfo;

    //handle the one packet flag by clipping
    if ((flags & SOAPY_SDR_ONE_PACKET) != 0) {
        numElems = std::min(numElems, (size_t)ustr->nfo.pktsyms);
    }

    if (ustr->rxcbuf.size() > 0) {
        // Single channel mode only atm
        size_t req_bytes = numElems * ustr->nfo.pktbszie / ustr->nfo.pktsyms;
        do {
            // fprintf(stderr, "rxcb wpos=%lld rpos=%lld req_bytes=%lld\n",
            //         (long long)ustr->rxcbuf->wpos,
            //         (long long)ustr->rxcbuf->rpos,
            //         (long long)req_bytes);

            if (ring_circbuf_rspace(ustr->rxcbuf[0]) >= req_bytes) {
                for (unsigned i = 0; i < ustr->rxcbuf.size(); i++) {
                    ring_circbuf_read(ustr->rxcbuf[i], buffs[i], req_bytes);
                }

                flags &= ~SOAPY_SDR_HAS_TIME;
                timeNs = 0;
                return numElems;
            }

            // We don't have enough data here
            void* chans[64];
            for (unsigned i = 0; i < ustr->rxcbuf.size(); i++) {
                chans[i] = ring_circbuf_wptr(ustr->rxcbuf[i]);
            }

            res = usdr_dms_recv(ustr->strm, chans, timeoutUs / 1000, &nfo);
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

        res = usdr_dms_recv(ustr->strm, (void**)buffs, timeoutUs / 1000, &nfo);

        if (rd) {
            fwrite(buffs[0], nfo.totsyms * 8, 1, rd);

            // Marks
            float d[2] = { -2, 2 };
            fwrite((void*)d, 8, 1, rd);
        }

        flags |= SOAPY_SDR_HAS_TIME;
        timeNs = SoapySDR::ticksToTimeNs(nfo.fsymtime, _actual_rx_rate);

        last_recv_pkt_time = nfo.fsymtime;
        return (res) ? SOAPY_SDR_TIMEOUT : nfo.totsyms;
    }
}

int SoapyUSDR::writeStream(
        SoapySDR::Stream *stream,
        const void * const *buffs,
        const size_t numElems,
        int &flags,
        const long long timeNs,
        const long timeoutUs)
{
    USDRStream* ustr = (USDRStream*)(stream);
    long long ts = (flags & SOAPY_SDR_HAS_TIME) ?
                    SoapySDR::timeNsToTicks(timeNs, _actual_tx_rate) + _txcorr : -1;

    int64_t lag = ts - last_recv_pkt_time;
    if (tx_pkts == 0) {
        avg_gap = lag;
    } else {
        double alpha = 0.01;
        avg_gap = (1 - alpha) * avg_gap + alpha * lag;
    }

    SoapySDR::logf(SOAPY_SDR_DEBUG, "writeStream::writeStream(%s) @ %lld num %d should be %d\n", ustr->stream, ts, numElems, ustr->nfo.pktsyms);

    unsigned toSend = numElems;
    int res = usdr_dms_send(ustr->strm, (const void **)buffs, numElems, ts, timeoutUs / 1000);

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

    return SOAPY_SDR_TIMEOUT; //SOAPY_SDR_NOT_SUPPORTED;
}
