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
#include <cstring>
#include <cstdio>

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

const char* SoapyUSDR::get_sdr_param(int sdridx, const char* dir, const char* par, const char* subpar) const
{
    if (subpar) {
        std::snprintf(_param_name, sizeof(_param_name), "/dm/sdr/%d/%s/%s/%s", sdridx, dir, par, subpar);
    } else {
        std::snprintf(_param_name, sizeof(_param_name), "/dm/sdr/%d/%s/%s", sdridx, dir, par);
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

    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::SoapyUSDR\n");

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

    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::SoapyUSDR: return\n");
}

SoapyUSDR::~SoapyUSDR(void)
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::~SoapyUSDR\n");
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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getNumChannels\n");
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

void SoapyUSDR::setDCOffset(const int direction, const size_t /*channel*/, const std::complex<double> &/*offset*/)
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    if (direction == SOAPY_SDR_TX) {
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

void SoapyUSDR::setIQBalance(const int /*direction*/, const size_t /*channel*/, const std::complex<double> &/*balance*/)
{
    //TODO
}

std::complex<double> SoapyUSDR::getIQBalance(const int /*direction*/, const size_t /*channel*/) const
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    return std::complex<double>(0,0);
}

/*******************************************************************
 * IQ Balance Mode API
 ******************************************************************/

bool SoapyUSDR::hasIQBalanceMode(const int /*direction*/, const size_t /*channel*/) const
{
    return false; // IQ Balance mode not supported
}

void SoapyUSDR::setIQBalanceMode(const int /*direction*/, const size_t /*channel*/, const bool /*automatic*/)
{
    // Not supported
}

bool SoapyUSDR::getIQBalanceMode(const int /*direction*/, const size_t /*channel*/) const
{
    return false;
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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getGainRange\n");
    if (direction == SOAPY_SDR_RX)
    {
        //make it so gain of 0.0 sets PGA at its mid-range
        return SoapySDR::Range(-12.0, 19.0+12.0+30.0);
    }
    return SoapySDR::Device::getGainRange(direction, channel);
}

SoapySDR::Range SoapyUSDR::getGainRange(const int direction, const size_t channel, const std::string &name) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getGainRange\n");
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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getFrequencyArgsInfo\n");
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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::setFrequency\n");

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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::listFrequencies\n");
    std::vector<std::string> opts;
    opts.push_back("RF");
    //opts.push_back("BB");
    return opts;
}

SoapySDR::RangeList SoapyUSDR::getFrequencyRange(const int /*direction*/, const size_t /*channel*/, const std::string &name) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getFrequencyRange\n");
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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getFrequencyRange\n");
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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getSampleRate\n");
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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getSampleRateRange\n");
    SoapySDR::RangeList ranges;
    ranges.push_back(SoapySDR::Range((type == RFIC_AFE79XX) ? 1.92e6 : 0.1e6,
                                     (type == RFIC_AFE79XX) ? 500e6 : (type == RFIC_AD45LB49) ? 130e6 : 80e6));
    return ranges;
}

std::vector<double> SoapyUSDR::listSampleRates(const int /*direction*/, const size_t /*channel*/) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::listSampleRates\n");
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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::setUParam\n");
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

    // TODO readback
    _actual_bandwidth[direction] = bw;
}

double SoapyUSDR::getBandwidth(const int direction, const size_t /*channel*/) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getBandwidth\n");
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    return _actual_bandwidth[direction];
}

SoapySDR::RangeList SoapyUSDR::getBandwidthRange(const int /*direction*/, const size_t /*channel*/) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getBandwidthRange\n");
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
    // TODO: get reference clock in case of autodetection

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setMasterClockRate(%.3f)", rate/1e6);
}

double SoapyUSDR::getMasterClockRate(void) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getMasterClockRate\n");
    double rate = 0;

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::getMasterClockRate() => %.3f", rate/1e6);
    return rate;
}

SoapySDR::RangeList SoapyUSDR::getMasterClockRates(void) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getMasterClockRates\n");
    SoapySDR::RangeList clks;
    clks.push_back(SoapySDR::Range(0, 0)); // means autodetect
    clks.push_back(SoapySDR::Range(10e6, 52e6));
    return clks;
}

std::vector<std::string> SoapyUSDR::listClockSources(void) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::listClockSources\n");
    return { "internal", "external" };
}

void SoapyUSDR::setClockSource(const std::string &source)
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::setClockSource\n");

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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getClockSource\n");
    return _clk_source;
}

/*******************************************************************
 * Time API
 ******************************************************************/

bool SoapyUSDR::hasHardwareTime(const std::string &what) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::hasHardwareTime\n");
    //assume hardware time when no argument is specified
    //some boards may not ever support hw time, so TODO

    return what.empty();
}

long long SoapyUSDR::getHardwareTime(const std::string &what) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getHardwareTime\n");
    long long hwtime = 0;

    if (!what.empty()) {
        throw std::invalid_argument("SoapyUSDR::getHardwareTime("+what+") unknown argument");
    }

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::getHardwareTime() => %lld", hwtime);
    return hwtime;
}

void SoapyUSDR::setHardwareTime(const long long timeNs, const std::string &what)
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::setHardwareTime\n");
    if (!what.empty()) {
        throw std::invalid_argument("SoapyUSDR::setHardwareTime("+what+") unknown argument");
    }

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::setHardwareTime(%lld)", timeNs);
}

/*******************************************************************
 * Sensor API
 ******************************************************************/

std::vector<std::string> SoapyUSDR::listSensors(void) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::listSensors\n");
    std::vector<std::string> sensors;
    sensors.push_back("clock_locked");
    sensors.push_back("board_temp");
    return sensors;
}

SoapySDR::ArgInfo SoapyUSDR::getSensorInfo(const std::string &name) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getSensorInfo\n");
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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::readSensor\n");
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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::listSensors\n");
    std::vector<std::string> sensors;
    sensors.push_back("lo_locked");
    return sensors;
}

SoapySDR::ArgInfo SoapyUSDR::getSensorInfo(const int /*direction*/, const size_t /*channel*/, const std::string &name) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getSensorInfo\n");
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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::readSensor\n");
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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::writeRegister\n");
    throw std::runtime_error(
                "SoapyUSDR::WriteRegister("+std::to_string(addr)+") FAIL");
}

unsigned SoapyUSDR::readRegister(const unsigned addr) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::readRegister\n");
    throw std::runtime_error(
                "SoapyUSDR::ReadRegister("+std::to_string(addr)+") FAIL");
}

/*******************************************************************
 * Settings API
 ******************************************************************/
SoapySDR::ArgInfoList SoapyUSDR::getSettingInfo(void) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getSettingInfo\n");
    SoapySDR::ArgInfoList infos;

    return infos;
}

void SoapyUSDR::writeSetting(const std::string &key, const std::string &value)
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::writeSetting\n");

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::writeSetting(%s, %s)", key.c_str(), value.c_str());

    throw std::runtime_error("unknown setting key: " + key);
}

SoapySDR::ArgInfoList SoapyUSDR::getSettingInfo(const int direction, const size_t channel) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getSettingInfo\n");
    // TODO
    (void)direction;
    (void)channel;

    SoapySDR::ArgInfoList infos;
    return infos;
}

void SoapyUSDR::writeSetting(const int direction, const size_t channel,
                             const std::string &key, const std::string &value)
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::writeSetting\n");
    // TODO
    (void)direction;
    (void)channel;
    (void)key;
    (void)value;

    SoapySDR::logf(callLogLvl(), "SoapyUSDR::writeSetting(%d, %d, %s, %s)", direction, (int)channel, key.c_str(), value.c_str());

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    throw std::runtime_error("unknown setting key: "+key);
}

/*******************************************************************
 * I2C API
 ******************************************************************/
void SoapyUSDR::writeI2C(const int addr, const std::string &/*data*/)
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::writeI2C\n");
    throw std::runtime_error(
                "SoapyUSDR::writeI2C("+std::to_string(addr)+") FAIL");
}

std::string SoapyUSDR::readI2C(const int addr, const size_t /*numBytes*/)
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::readI2C\n");
    throw std::runtime_error(
                "SoapyUSDR::readI2C("+std::to_string(addr)+") FAIL");
}

/*******************************************************************
 * SPI API
 ******************************************************************/
unsigned SoapyUSDR::transactSPI(const int addr, const unsigned /*data*/, const size_t /*numBits*/)
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::transactSPI\n");
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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getStreamFormats\n");
    std::vector<std::string> formats;
    formats.push_back(SOAPY_SDR_CF32);
    formats.push_back(SOAPY_SDR_CS16);
    return formats;
}

std::string SoapyUSDR::getNativeStreamFormat(const int /*direction*/, const size_t /*channel*/, double &fullScale) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getNativeStreamFormat\n");
    fullScale = 32768;
    return SOAPY_SDR_CS16;
}

SoapySDR::ArgInfoList SoapyUSDR::getStreamArgsInfo(const int /*direction*/, const size_t /*channel*/) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getStreamArgsInfo\n");
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

    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::setupStream\n");

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

    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::SoapyUSDR: sync call\n");
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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::closeStream\n");
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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getStreamMTU\n");

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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::activateStream\n");

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
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::deactivateStream\n");

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
    //USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::readStreamStatus\n");
    (void)stream;
    (void)chanMask;
    (void)flags;
    (void)timeNs;
    (void)timeoutUs;

    return SOAPY_SDR_TIMEOUT; //SOAPY_SDR_NOT_SUPPORTED;
}

/*******************************************************************
 * Frontend mapping and channel info API
 ******************************************************************/
void SoapyUSDR::setFrontendMapping(const int direction, const std::string &mapping)
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::setFrontendMapping\n");
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    _frontend_mapping[direction] = mapping;
}

std::string SoapyUSDR::getFrontendMapping(const int direction) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getFrontendMapping\n");
    return _frontend_mapping[direction];
}

SoapySDR::Kwargs SoapyUSDR::getChannelInfo(const int direction, const size_t channel) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getChannelInfo\n");
    SoapySDR::Kwargs info;
    return info;
}

/*******************************************************************
 * GPIO API
 ******************************************************************/
std::vector<std::string> SoapyUSDR::listGPIOBanks() const
{
    return std::vector<std::string>();
}

void SoapyUSDR::writeGPIO(const std::string &bank, const unsigned value)
{
    throw std::runtime_error("GPIO interface not supported");
}

void SoapyUSDR::writeGPIO(const std::string &bank, const unsigned value, const unsigned mask)
{
    throw std::runtime_error("GPIO interface not supported");
}

unsigned SoapyUSDR::readGPIO(const std::string &bank) const
{
    throw std::runtime_error("GPIO interface not supported");
}

void SoapyUSDR::writeGPIODir(const std::string &bank, const unsigned dir)
{
    throw std::runtime_error("GPIO interface not supported");
}

void SoapyUSDR::writeGPIODir(const std::string &bank, const unsigned dir, const unsigned mask)
{
    throw std::runtime_error("GPIO interface not supported");
}

unsigned SoapyUSDR::readGPIODir(const std::string &bank) const
{
    throw std::runtime_error("GPIO interface not supported");
}

/*******************************************************************
 * UART API
 ******************************************************************/
std::vector<std::string> SoapyUSDR::listUARTs() const
{
    return std::vector<std::string>();
}

void SoapyUSDR::writeUART(const std::string &name, const std::string &data)
{
    throw std::runtime_error("UART interface not supported");
}

std::string SoapyUSDR::readUART(const std::string &name, const long timeoutUs) const
{
    throw std::runtime_error("UART interface not supported");
}

/*******************************************************************
 * Native handle API
 ******************************************************************/
void* SoapyUSDR::getNativeDeviceHandle() const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getNativeDeviceHandle\n");
    return _dev->dev();
}

/*******************************************************************
 * Direct buffer access API
 ******************************************************************/
size_t SoapyUSDR::getNumDirectAccessBuffers(SoapySDR::Stream */*stream*/)
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getNumDirectAccessBuffers\n");
    return 0; // Direct buffer access not supported
}

int SoapyUSDR::getDirectAccessBufferAddrs(SoapySDR::Stream */*stream*/, const size_t /*handle*/, void **/*buffs*/)
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getDirectAccessBufferAddrs\n");
    return SOAPY_SDR_NOT_SUPPORTED;
}

int SoapyUSDR::acquireReadBuffer(
    SoapySDR::Stream */*stream*/,
    size_t &/*handle*/,
    const void **/*buffs*/,
    int &/*flags*/,
    long long &/*timeNs*/,
    const long /*timeoutUs*/)
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::acquireReadBuffer\n");
    return SOAPY_SDR_NOT_SUPPORTED;
}

void SoapyUSDR::releaseReadBuffer(
    SoapySDR::Stream */*stream*/,
    const size_t /*handle*/)
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::releaseReadBuffer\n");
    // Not supported
}

int SoapyUSDR::acquireWriteBuffer(
    SoapySDR::Stream */*stream*/,
    size_t &/*handle*/,
    void **/*buffs*/,
    const long /*timeoutUs*/)
{
    return SOAPY_SDR_NOT_SUPPORTED;
}

void SoapyUSDR::releaseWriteBuffer(
    SoapySDR::Stream */*stream*/,
    const size_t /*handle*/,
    const size_t /*numElems*/,
    int &/*flags*/,
    const long long /*timeNs*/)
{
    // Not supported
}

/*******************************************************************
 * Reference Clock API
 ******************************************************************/

void SoapyUSDR::setReferenceClockRate(const double /*rate*/)
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::setReferenceClockRate\n");
    // Reference clock rate setting not supported
}

double SoapyUSDR::getReferenceClockRate(void) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getReferenceClockRate\n");
    return 0.0; // Reference clock rate not available
}

SoapySDR::RangeList SoapyUSDR::getReferenceClockRates(void) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getReferenceClockRates\n");
    return SoapySDR::RangeList(); // No reference clock rates available
}

/*******************************************************************
 * Time Source API
 ******************************************************************/

std::vector<std::string> SoapyUSDR::listTimeSources(void) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::listTimeSources\n");
    return {"internal"}; // Only internal time source supported
}

void SoapyUSDR::setTimeSource(const std::string &source)
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::setTimeSource\n");
    if (source != "internal") {
        throw std::runtime_error("SoapyUSDR::setTimeSource(" + source + ") - only internal time source supported");
    }
    _time_source = source;
}

std::string SoapyUSDR::getTimeSource(void) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getTimeSource\n");
    return _time_source;
}

void SoapyUSDR::setCommandTime(const long long timeNs, const std::string &what)
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::setCommandTime\n");
    // Command time setting not supported
    throw std::runtime_error("Command time setting not supported");
}

/*******************************************************************
 * Frontend corrections API continued
 ******************************************************************/

bool SoapyUSDR::hasFrequencyCorrection(const int /*direction*/, const size_t /*channel*/) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::hasFrequencyCorrection\n");
    return false; // Frequency correction not supported
}

void SoapyUSDR::setFrequencyCorrection(const int /*direction*/, const size_t /*channel*/, const double /*value*/)
{
    throw std::runtime_error("Frequency correction not supported");
}

double SoapyUSDR::getFrequencyCorrection(const int /*direction*/, const size_t /*channel*/) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getFrequencyCorrection\n");
    return 0.0; // Return default value since correction is not supported
}

/*******************************************************************
 * Gain Mode API
 ******************************************************************/

bool SoapyUSDR::hasGainMode(const int /*direction*/, const size_t /*channel*/) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::hasGainMode\n");
    return false; // Automatic gain mode not supported
}

void SoapyUSDR::setGainMode(const int /*direction*/, const size_t /*channel*/, const bool /*automatic*/)
{
    throw std::runtime_error("Automatic gain mode not supported");
}

bool SoapyUSDR::getGainMode(const int /*direction*/, const size_t /*channel*/) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getGainMode\n");
    return false; // Always in manual gain mode
}

double SoapyUSDR::getGain(const int direction, const size_t /*channel*/) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getGain\n");

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    const char* dir = (direction == SOAPY_SDR_TX) ? "tx" : "rx";
    const char* pname = get_sdr_param(0, dir, "gain", nullptr);
    uint64_t val;
    int res = usdr_dme_get_uint(_dev->dev(), pname, &val);
    if (res) {
        throw std::runtime_error("Failed to get gain value");
    }
    return (double)val;
}

void SoapyUSDR::setFrequency(const int direction, const size_t channel, const double frequency, const SoapySDR::Kwargs &args)
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::setFrequency\n");
    // Use the existing implementation that takes a name parameter, with empty name for RF frequency
    this->setFrequency(direction, channel, "RF", frequency, args);
}

double SoapyUSDR::getFrequency(const int direction, const size_t channel) const
{
    USDR_LOG("SOAP", USDR_LOG_ERROR, "======DBG: SoapyUSDR::getFrequency\n");
    // Use the existing implementation that takes a name parameter, with empty name for RF frequency
    return this->getFrequency(direction, channel, "RF");
}

/*******************************************************************
 * Register Interface API
 ******************************************************************/

std::vector<std::string> SoapyUSDR::listRegisterInterfaces(void) const
{
    return std::vector<std::string>(); // No register interfaces available
}

void SoapyUSDR::writeRegister(const std::string &/*name*/, const unsigned addr, const unsigned value)
{
    // Delegate to the unnamed register interface
    this->writeRegister(addr, value);
}

unsigned SoapyUSDR::readRegister(const std::string &/*name*/, const unsigned addr) const
{
    // Delegate to the unnamed register interface
    return this->readRegister(addr);
}

void SoapyUSDR::writeRegisters(const std::string &/*name*/, const unsigned addr, const std::vector<unsigned> &value)
{
    for (size_t i = 0; i < value.size(); i++) {
        this->writeRegister(addr + i, value[i]);
    }
}

std::vector<unsigned> SoapyUSDR::readRegisters(const std::string &/*name*/, const unsigned addr, const size_t length) const
{
    std::vector<unsigned> values(length);
    for (size_t i = 0; i < length; i++) {
        values[i] = this->readRegister(addr + i);
    }
}

/*******************************************************************
 * Settings API continued
 ******************************************************************/

std::string SoapyUSDR::readSetting(const std::string &/*key*/) const
{
    throw std::runtime_error("Settings interface not supported");
}

std::string SoapyUSDR::readSetting(const int /*direction*/, const size_t /*channel*/, const std::string &/*key*/) const
{
    throw std::runtime_error("Settings interface not supported");
}

/*******************************************************************
 * Bandwidth API continued
 ******************************************************************/

std::vector<double> SoapyUSDR::listBandwidths(const int /*direction*/, const size_t /*channel*/) const
{
    return std::vector<double>(); // No fixed bandwidth options available
}
