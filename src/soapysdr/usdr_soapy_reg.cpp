// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "usdr_soapy.h"
#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Logger.hpp>
#include <cstring>

static SoapySDR::KwargsList findIConnection(const SoapySDR::Kwargs &matchArgs)
{
    SoapySDR::KwargsList results;

    char buffer[4096];
    int count = usdr_dmd_discovery("", sizeof(buffer), buffer);
    char* dptr = buffer;

    // TODO skip incompatible module

    for (int i = 0; i < count; i++) {
        const char* uniqname = dptr;
        char* end = strchr(dptr, '\n');
        if (end) {
            *end = 0;
        }
        // const char* s = strchr(dptr, '\t');
        // TODO parse params
        // TODO filter by matchArgs

        SoapySDR::Kwargs usdrArgs = matchArgs;
        usdrArgs["type"] = "usdr";
        usdrArgs["dev"] = uniqname;

        usdrArgs["module"] = "usdr_soapy";
        usdrArgs["media"] = "usdr";
        usdrArgs["name"] = "usdr";
        usdrArgs["addr"] = uniqname;
        usdrArgs["serial"] = "012345678";

        usdrArgs["driver"] = "usdr";
        usdrArgs["label"] = std::string("USDR: ") + uniqname;
        results.push_back(usdrArgs);

        dptr = end + 1;
    }

    return results;
}

static SoapySDR::Device *makeIConnection(const SoapySDR::Kwargs &args)
{
    return new SoapyUSDR(args);
}

static SoapySDR::Registry registerIConnection("usdr", &findIConnection, &makeIConnection, SOAPY_SDR_ABI_VERSION);


