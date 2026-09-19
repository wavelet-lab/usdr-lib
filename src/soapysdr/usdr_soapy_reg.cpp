// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "usdr_soapy.h"
#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Logger.hpp>
#include <cstring>
#include <set>

static bool kwargsMatch(const SoapySDR::Kwargs &deviceArgs, const SoapySDR::Kwargs &matchArgs)
{
    static const std::set<std::string> registryKeys = {
        "driver",
        "type",
        "addr",
        "label",
        "media",
        "module",
        "name",
    };

    for (const auto &matchArg : matchArgs) {
        if (!usdrSoapyIsDeviceArg(matchArg.first) && registryKeys.count(matchArg.first) == 0) {
            continue;
        }

        const auto arg = deviceArgs.find(matchArg.first);
        if (arg == deviceArgs.end() || arg->second != matchArg.second) {
            return false;
        }
    }

    return true;
}

static SoapySDR::KwargsList findIConnection(const SoapySDR::Kwargs &matchArgs)
{
    SoapySDR::KwargsList results;

    char buffer[4096];
    const std::string discoveryArgs = usdrSoapyDeviceString(matchArgs);
    int count = usdr_dmd_discovery(discoveryArgs.c_str(), sizeof(buffer), buffer);
    if (count <= 0) {
        return results;
    }

    char* dptr = buffer;

    for (int i = 0; i < count; i++) {
        const char* uniqname = dptr;
        char* end = strchr(dptr, '\n');
        if (end) {
            *end = 0;
        } else if (*dptr == 0) {
            break;
        }

        SoapySDR::Kwargs usdrArgs = SoapySDR::KwargsFromString(uniqname);
        usdrArgs["type"] = "usdr";
        usdrArgs["dev"] = uniqname;

        usdrArgs["module"] = "usdr_soapy";
        usdrArgs["media"] = "usdr";
        usdrArgs["name"] = "usdr";
        usdrArgs["addr"] = uniqname;
        usdrArgs["serial"] = "012345678";

        usdrArgs["driver"] = "usdr";
        usdrArgs["label"] = std::string("USDR: ") + uniqname;
        if (kwargsMatch(usdrArgs, matchArgs)) {
            results.push_back(usdrArgs);
        }

        if (!end) {
            break;
        }
        dptr = end + 1;
    }

    return results;
}

static SoapySDR::Device *makeIConnection(const SoapySDR::Kwargs &args)
{
    return new SoapyUSDR(args);
}

static SoapySDR::Registry registerIConnection("usdr", &findIConnection, &makeIConnection, SOAPY_SDR_ABI_VERSION);
