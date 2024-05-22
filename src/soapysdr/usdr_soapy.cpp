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



SoapyUSDR::SoapyUSDR(const SoapySDR::Kwargs &args)
 : _actual_tx_rate(0)
 , _actual_rx_rate(0)
 , _desired_rx_pkt(0)
 , last_recv_pkt_time(0)
 , avg_gap(0)
 , rx_pkts(0)
 , tx_pkts(0)
 , rd(NULL)
{
    SoapySDR::logf(SOAPY_SDR_ERROR, "Make connection: '%s'", args.count("dev") ? args.at("dev").c_str() : "*");

    for (auto i: args) {
        SoapySDR::logf(SOAPY_SDR_ERROR, "Param %s => %s", i.first.c_str(), i.second.c_str());
    }

    unsigned loglevel = 3;
#ifdef __linux
    const char* lenv = getenv("SOAPY_USDR_LOGLEVEL");
    if (lenv) {
        loglevel = atoi(lenv);
    }
#endif
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

    usdrlog_setlevel(NULL, loglevel);

    _dev = usdr_handle::get(dev);

    if (args.count("refclk")) {
        // TODO:
        SoapySDR::logf(SOAPY_SDR_DEBUG, "SoapyUSDR::SoapyUSDR() set ref to internal clock");
    }
    if (args.count("extclk")) {
        // TODO:
        SoapySDR::logf(SOAPY_SDR_DEBUG, "SoapyUSDR::SoapyUSDR() set ref to external clock");
    }
    if (args.count("desired_rx_pkt")) {
        _desired_rx_pkt = atoi(args.at("desired_rx_pkt").c_str());

        SoapySDR::logf(SOAPY_SDR_INFO, "SoapyUSDR::SoapyUSDR() set `desired_rx_pkt` to %d", _desired_rx_pkt);
    }
    if (args.count("rxdump")) {
        const char* filename = args.at("rxdump").c_str();
        rd = fopen(filename, "wb+");
        if (rd == NULL)
            throw std::runtime_error("SoapyUSDR::SoapyUSDR() - unable to create rx dump file");

        SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::SoapyUSDR() dumping recieve to %s", filename);
    }
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
    unsigned chans = 1;

    // TODO check channels count
    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::getNumChannels(%d) => %d", direction, chans);
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
    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::setAntenna(%d, %d, %s)", direction, int(channel), name.c_str());

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
}

std::string SoapyUSDR::getAntenna(const int direction, const size_t channel) const
{
    std::string antenna = "";

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);

    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::setAntenna(%d, %d, %s)", direction, int(channel), antenna.c_str());
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
        //rfic->SetRxDCRemoval(automatic);
    }
}

bool SoapyUSDR::getDCOffsetMode(const int direction, const size_t /*channel*/) const
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    if (direction == SOAPY_SDR_RX) {
        // return rfic->GetRxDCRemoval();
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
        //rfic->SetTxDCOffset(offset.real(), offset.imag());
    }
}

std::complex<double> SoapyUSDR::getDCOffset(const int /*direction*/, const size_t /*channel*/) const
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    double I = 0.0, Q = 0.0;
    //if (direction == SOAPY_SDR_TX) rfic->GetTxDCOffset(I, Q);
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
 * Gain API
 ******************************************************************/

std::vector<std::string> SoapyUSDR::listGains(const int direction, const size_t /*channel*/) const
{
    std::vector<std::string> gains;
    if (direction == SOAPY_SDR_RX)
    {
        gains.push_back("LNA");
        gains.push_back("TIA");
        gains.push_back("PGA");
    }
    else if (direction == SOAPY_SDR_TX)
    {
        gains.push_back("PAD");
    }
    return gains;
}

void SoapyUSDR::setGain(const int direction, const size_t channel, const double value)
{
    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::setGain(%d, %d, %g dB)", direction, int(channel), value);

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
}

void SoapyUSDR::setGain(const int direction, const size_t channel, const std::string &name, const double value)
{
    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::setGain(%d, %d, %s, %g dB)", direction, int(channel), name.c_str(), value);

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
}

double SoapyUSDR::getGain(const int direction, const size_t channel, const std::string &name) const
{
    double ret = 0;

    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::getGain(%d, %d, %s) => %g dB", direction, int(channel), name.c_str(), ret);

    return ret;
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
    if (direction == SOAPY_SDR_RX and name == "LNA") return SoapySDR::Range(0.0, 30.0);
    if (direction == SOAPY_SDR_RX and name == "TIA") return SoapySDR::Range(0.0, 12.0);
    if (direction == SOAPY_SDR_RX and name == "PGA") return SoapySDR::Range(-12.0, 19.0);
    if (direction == SOAPY_SDR_TX and name == "PAD") return SoapySDR::Range(-52.0, 0.0);

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
    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::setFrequency(%d, %s, %g MHz)", int(channel), name.c_str(), frequency/1e6);
    int res;

    const char* dir = (direction == SOAPY_SDR_TX) ? "tx" : "rx";
    const char* pname = get_sdr_param(0, dir, "freqency", (name == "BB") ? "bb" : NULL);

    uint64_t val = (((uint64_t)channel) << 32) | (uint32_t)frequency;

    res = usdr_dme_set_uint(_dev->dev(), pname,
                            val);
    if (res)
        throw std::runtime_error(std::string("SoapyUSDR::setFrequency(") + pname + ", " + ")");

}

double SoapyUSDR::getFrequency(const int /*direction*/, const size_t channel, const std::string &name) const
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    SoapySDR::logf(SOAPY_SDR_DEBUG, "SoapyUSDR::getFrequency(%d, %s)", int(channel), name.c_str());

    return 0;
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
        ranges.push_back(SoapySDR::Range(30e6, 3.8e9));
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
    ranges.push_back(SoapySDR::Range(0e6, 3.8e9));
    return ranges;
}

/*******************************************************************
 * Sample Rate API
 ******************************************************************/

void SoapyUSDR::setSampleRate(const int direction, const size_t channel, const double rate)
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::setSampleRate(%d, %s, %g MHz)", int(channel), (direction == SOAPY_SDR_TX) ? "TX" : "RX", rate/1e6);


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
        SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::setSampleRate(%d, %s, %g MHz) - error %d",
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
    ranges.push_back(SoapySDR::Range(0.1e6, 80e6));
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
    if (bw == 0.0) return; //special ignore value

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::setBandwidth(, %d, %g MHz)",  int(channel), bw/1e6);
    int res;

    const char* dir = (direction == SOAPY_SDR_TX) ? "tx" : "rx";
    const char* pname = get_sdr_param(0, dir, "bandwidth",  NULL);

    res = usdr_dme_set_uint(_dev->dev(), pname,
                            bw);
    if (res)
        throw std::runtime_error("SoapyUSDR::setBandwidth(" + std::string(pname) + ") error");

}

double SoapyUSDR::getBandwidth(const int /*direction*/, const size_t /*channel*/) const
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);

#if 0
    if (direction == SOAPY_SDR_RX)
    {
        return _actual_rx_bandwidth[channel];
    }
    else if (direction == SOAPY_SDR_TX)
    {
        return _actual_tx_bandwidth[channel];
    }

#endif

    return 0;
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
    // TODO: get reference clock in case of autodetection

    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::setMasterClockRate(%.3f)", rate/1e6);
}

double SoapyUSDR::getMasterClockRate(void) const
{
    double rate = 0;

    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::getMasterClockRate() => %.3f", rate/1e6);
    return rate;
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
    return { "internal" };
}

void SoapyUSDR::setClockSource(const std::string &source)
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);

    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::setClockSource(%s)", source.c_str());

    if (source == "internal")
        return;
}

std::string SoapyUSDR::getClockSource(void) const
{
    return "internal";
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

    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::getHardwareTime() => %lld", hwtime);
    return hwtime;
}

void SoapyUSDR::setHardwareTime(const long long timeNs, const std::string &what)
{
    if (!what.empty()) {
        throw std::invalid_argument("SoapyUSDR::setHardwareTime("+what+") unknown argument");
    }

    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::setHardwareTime(%lld)", timeNs);
}

/*******************************************************************
 * Sensor API
 ******************************************************************/

std::vector<std::string> SoapyUSDR::listSensors(void) const
{
    std::vector<std::string> sensors;
    sensors.push_back("clock_locked");
    sensors.push_back("lms7_temp");
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
    else if (name == "lms7_temp")
    {
        info.key = "lms7_temp";
        info.name = "LMS7 Temperature";
        info.type = SoapySDR::ArgInfo::FLOAT;
        info.value = "0.0";
        info.units = "C";
        info.description = "The temperature of the LMS7002M in degrees C.";
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
    else if (name == "lms7_temp")
    {
        return "0.0";
    }
    else if (name == "board_temp")
    {
        return "0.0";
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
    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::writeSetting(%s, %s)", key.c_str(), value.c_str());

    throw std::runtime_error("unknown setting key: " + key);
}

SoapySDR::ArgInfoList SoapyUSDR::getSettingInfo(const int direction, const size_t channel) const
{
    // TODO
    (void)direction;
    (void)channel;

    SoapySDR::ArgInfoList infos;
    return infos;
}

void SoapyUSDR::writeSetting(const int direction, const size_t channel,
                             const std::string &key, const std::string &value)
{
    // TODO
    (void)direction;
    (void)channel;
    (void)key;
    (void)value;

    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::writeSetting(%d, %d, %s, %s)", direction, (int)channel, key.c_str(), value.c_str());

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    throw std::runtime_error("unknown setting key: "+key);
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
        info.value = SOAPY_SDR_CS16;
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
    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::setupStream(%s, %s, %d)\n",
                   direction == SOAPY_SDR_RX ? "RX" : "TX", format.c_str(), (unsigned)channels.size());

    //TODO: multi stream
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    size_t num_channels = channels.size();
    if (num_channels < 1)
        num_channels = 1;
    size_t chmsk = (num_channels == 1) ? 0x1 : 0x3;
    const char* uformat = (format == SOAPY_SDR_CF32) ? "cf32" :
                          (format == SOAPY_SDR_CS16) ? "ci16" : NULL;


    if (args.count("linkFormat")) {
        const std::string& link_fmt = args.at("linkFormat");
        if (link_fmt != SOAPY_SDR_CS16) {
            throw std::runtime_error("SoapyUSDR::setupStream([linkFormat="+link_fmt+"]) unsupported link format");
        }
    }

    if (args.count("floatScale")) {
        const std::string& float_scale = args.at("floatScale");
        float scale = std::atof(float_scale.c_str());
        if (scale != 1.0f) {
            throw std::runtime_error("SoapyUSDR::setupStream([floatScale="+float_scale+") unsupported scale");
        }
    }

    _streams[direction].fmt = uformat;
    _streams[direction].chmsk = chmsk;
    _streams[direction].stream = direction == SOAPY_SDR_RX ? "/ll/srx/0" : "/ll/stx/0";

    // FIXUP (TODO get MTU size)
    if (direction == SOAPY_SDR_RX) {
        _streams[direction].nfo.pktsyms = (_desired_rx_pkt == 0) ? 1920 : _desired_rx_pkt;
    } else {
        _streams[direction].nfo.pktsyms = 8192;
    }

    _streams[direction].self = this;

//////////////////////////////////////////////////////////////////////////

    // srsran do this
    //
    // [ERROR] SoapyUSDR::setupStream(RX, CF32, 1)
    // [ERROR] SoapyUSDR::setSampleRate(0, RX, 1.92 MHz)
    // [ERROR] SoapyUSDR::getStreamMTU(/ll/srx/0) => 1920
    // [ERROR] SoapyUSDR::setupStream(TX, CF32, 1)
    // [ERROR] SoapyUSDR::getStreamMTU(/ll/stx/0) => 8192
    // [ERROR] SoapyUSDR::setSampleRate(0, RX, 1.92 MHz)
    // [ERROR] SoapyUSDR::setSampleRate(0, TX, 1.92 MHz)
    // [ERROR] SoapyUSDR::setAntenna(1, 0, )
    // [ERROR] SoapyUSDR::setAntenna(0, 0, )
    // [ERROR] SoapyUSDR::setGain(1, 0, 40 dB)
    // [ERROR] SoapyUSDR::setGain(0, 0, 100 dB)
    // [ERROR] SoapyUSDR::setSampleRate(0, RX, 3.84 MHz)
    // [ERROR] SoapyUSDR::setSampleRate(0, TX, 3.84 MHz)
    // [ERROR] SoapyUSDR::setFrequency(0, RF, 2680 MHz)
    // [ERROR] SoapyUSDR::setFrequency(0, RF, 2560 MHz)
    // [ERROR] SoapyUSDR::activateStream(/ll/srx/0, @ 0 ns, 0 samples, 00000000)
    //   recv calls()
    // [ERROR] SoapyUSDR::activateStream(/ll/stx/0, @ 0 ns, 0 samples, 00000000)
    //   recv & send calls()


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

    SoapySDR::logf(SOAPY_SDR_INFO, "SoapyUSDR::setupStream(%s) %d Samples per packet, burst size %d * %d chs; res = %d",
                   ustr->stream, numElems, ustr->nfo.pktsyms, ustr->nfo.channels, res);

    if (_actual_rx_rate == 0) {
        setSampleRate(SOAPY_SDR_RX, 0, 1.92e6);
    }

    // TODO: time
    //res = usdr_dms_sync(_dev->dev(), "off", 1, &ustr->strm);
    res = usdr_dms_sync(_dev->dev(), "rx", 1, &ustr->strm);
    if (res) {
        throw std::runtime_error("SoapyUSDR::setupStream failed!");
    }

    res = usdr_dms_op(ustr->strm, USDR_DMS_START, 0);
    ustr->active = true;

    if (ustr->self->_streams[0].active && ustr->self->_streams[1].active) {
        pusdr_dms_t pstr[2] = { ustr->self->_streams[0].strm, ustr->self->_streams[1].strm };
        SoapySDR::logf(SOAPY_SDR_INFO, "SoapyUSDR::setupStream() -> resync!!!");
        res = usdr_dms_sync(_dev->dev(), "rx", 2, pstr);
    }
/////////////////////////////////////////////////////////////////////////////

    return (SoapySDR::Stream *)&_streams[direction];
}

void SoapyUSDR::closeStream(SoapySDR::Stream *stream)
{
    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    USDRStream* ustr = (USDRStream*)(stream);

    if (ustr->strm) {
        usdr_dms_op(ustr->strm, USDR_DMS_STOP, 0);
        usdr_dms_destroy(ustr->strm);
        ustr->strm = NULL;
    }

    if (ustr->rxcbuf) {
        ring_circbuf_destroy(ustr->rxcbuf);
        ustr->rxcbuf = NULL;
    }
}

size_t SoapyUSDR::getStreamMTU(SoapySDR::Stream *stream) const
{
    USDRStream* ustr = (USDRStream*)(stream);

    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::getStreamMTU(%s) => %d",
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
    int res;

    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::activateStream(%s, @ %lld ns, %d samples, %08x)",
                   ustr->stream, timeNs, (unsigned)numElems, flags);

    // Todo set bandwidth
    setBandwidth(SOAPY_SDR_TX, 0, 20e6);
    setBandwidth(SOAPY_SDR_RX, 0, 20e6);

    setUParam(SOAPY_SDR_TX, "gain", NULL, 25); //After filter

    setUParam(SOAPY_SDR_RX, "gain", "vga", 30);
    setUParam(SOAPY_SDR_RX, "gain", "pga", 0);

#if 0
    res = usdr_dms_create_ex(_dev->dev(), ustr->stream, ustr->fmt, ustr->chmsk,
                             (numElems == 0) ? ustr->nfo.pktsyms : numElems,
                             0, &ustr->strm);
    if (res) {
        return SOAPY_SDR_NOT_SUPPORTED;
    }

    res = usdr_dms_info(ustr->strm, &ustr->nfo);
    if (res) {
        throw std::runtime_error("SoapyUSDR::setupStream failed!");
    }

    SoapySDR::logf(SOAPY_SDR_INFO, "SoapyUSDR::activateStream(%s) %d Samples per packet, burst size %d * %d chs; res = %d",
                   ustr->stream, numElems, ustr->nfo.pktsyms, ustr->nfo.channels, res);

    // TODO: time
    res = usdr_dms_sync(_dev->dev(), "off", 1, &ustr->strm);
    if (res) {
        throw std::runtime_error("SoapyUSDR::setupStream failed!");
    }

    res = usdr_dms_op(ustr->strm, USDR_DMS_START, 0);
    ustr->active = true;

    if (ustr->self->_streams[0].active && ustr->self->_streams[1].active) {
        pusdr_dms_t pstr[2] = { ustr->self->_streams[0].strm, ustr->self->_streams[1].strm };
        SoapySDR::logf(SOAPY_SDR_INFO, "SoapyUSDR::activateStream() -> resync!!!");
        res = usdr_dms_sync(_dev->dev(), "rx", 2, pstr);
    }

#else
    res = 0;
#endif

    return (res) ? SOAPY_SDR_NOT_SUPPORTED : 0;
}

int SoapyUSDR::deactivateStream(
        SoapySDR::Stream *stream,
        const int flags,
        const long long timeNs)
{
    USDRStream* ustr = (USDRStream*)(stream);

    SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::deactivateStream(%s, @ %lld ns, %08x)",
                   ustr->stream, timeNs, flags);

    if (ustr->rxcbuf) {
        ring_circbuf_destroy(ustr->rxcbuf);
        ustr->rxcbuf = nullptr;
    }

    std::unique_lock<std::recursive_mutex> lock(_dev->accessMutex);
    usdr_dms_op(ustr->strm, USDR_DMS_STOP, 0);
    usdr_dms_destroy(ustr->strm);
    ustr->strm = NULL;

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
    // SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyUSDR::readStream(%s)", ustr->stream);

    int res;
    struct usdr_dms_recv_nfo nfo;

    //handle the one packet flag by clipping
    if ((flags & SOAPY_SDR_ONE_PACKET) != 0) {
        numElems = std::min(numElems, (size_t)ustr->nfo.pktsyms);
    }

    if (ustr->rxcbuf) {
        // Single channel mode only atm
        size_t req_bytes = numElems * ustr->nfo.pktbszie / ustr->nfo.pktsyms;
        do {
            // fprintf(stderr, "rxcb wpos=%lld rpos=%lld req_bytes=%lld\n",
            //         (long long)ustr->rxcbuf->wpos,
            //         (long long)ustr->rxcbuf->rpos,
            //         (long long)req_bytes);

            if (ring_circbuf_rspace(ustr->rxcbuf) >= req_bytes) {
                ring_circbuf_read(ustr->rxcbuf, buffs[0], req_bytes);

                flags &= ~SOAPY_SDR_HAS_TIME;
                timeNs = 0;
                return numElems;
            }

            // We don't have enough data here
            void* chans[2] = { ring_circbuf_wptr(ustr->rxcbuf), ring_circbuf_wptr(ustr->rxcbuf) };
            res = usdr_dms_recv(ustr->strm, chans, timeoutUs, &nfo);
            if (res == 0) {
                ustr->rxcbuf->wpos += ustr->nfo.pktbszie;
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
            ustr->rxcbuf = ring_circbuf_create(blksz_bytes);

            // Reenter
            return readStream(stream, buffs, numElems, flags, timeNs, timeoutUs);
        }

        res = usdr_dms_recv(ustr->strm, (void**)buffs, timeoutUs, &nfo);

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
                    SoapySDR::timeNsToTicks(timeNs, _actual_tx_rate) : -1;

    int64_t lag = ts - last_recv_pkt_time;
    if (tx_pkts == 0) {
        avg_gap = lag;
    } else {
        double alpha = 0.01;
        avg_gap = (1 - alpha) * avg_gap + alpha * lag;
    }

    SoapySDR::logf(SOAPY_SDR_DEBUG, "writeStream::writeStream(%s) @ %lld num %d should be %d\n", ustr->stream, ts, numElems, ustr->nfo.pktsyms);

    unsigned toSend = numElems;
    int res = usdr_dms_send(ustr->strm, (const void **)buffs, numElems, ts, timeoutUs);

    if (tx_pkts % 1000 == 0) {
        SoapySDR::logf(SOAPY_SDR_ERROR, "TX %lld / %d -> lag %f (%d)", timeNs, numElems, avg_gap, lag);
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
