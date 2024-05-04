// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <stdio.h>

#include "webusb.h"

//static unsigned s_exp_evno;
//static unsigned s_exp_data;
//static unsigned s_seqnum;

static
int mock_io_write(uintptr_t param, uint8_t ep, unsigned bytes, const void *data)
{
    fprintf(stderr, "mock_io_write[%x: %d bytes]:\n", ep, bytes);
    return bytes;
}

static
int mock_io_read(uintptr_t param, uint8_t ep, unsigned maxbytes, void *data)
{
    fprintf(stderr, "mock_io_read[%x: %d bytes]:\n", ep, maxbytes);
    return maxbytes;
}

static
int mock_log(uintptr_t param, unsigned sevirity, const char* log)
{
    fprintf(stderr, "mock_log [%d]     :%s", sevirity, log);
    return 0;
}

static
struct webusb_ops s_mock_ops = {
    &mock_io_read,
    &mock_io_write,

    &mock_log,
};

static
    char s_rpc_debug_dump[] =
    "{"
    "\"req_method\":\"sdr_debug_dump\","
    "\"req_params\": {}"
    "}";

static
char s_rpc_revision[] =
        "{"
         "\"req_method\":\"sdr_get_revision\","
         "\"req_params\": {}"
        "}";

static
char s_rpc_init[] =
        "{"
        "\"req_method\":\"sdr_init_streaming\","
        "\"req_params\": {"
            "\"chans\":1,"
            "\"samplerate\":1000000,"
            "\"packetsize\":4096,"
            "\"mode\":2"
            "}"
        "}";

static
char s_rpc_set_freq[] =
        "{"
        "\"req_method\":\"sdr_set_rx_frequency\","
        "\"req_params\": {"
            "\"param_type\":\"rf\","
            "\"frequency\":306200000"
            "}"
        "}";
char s_rpc_flash[] =
        "{"
        "\"req_method\":\"flash_write_sector\","
        "\"req_params\": {"
            "\"offset\":0"
            "},"
        "\"req_data\": \"nmsanu3iqheq+w0-23489e!@12414\""
        "}";

char* test_strings[] = {
    s_rpc_debug_dump,
    s_rpc_revision,
    s_rpc_init,
    s_rpc_set_freq,
    s_rpc_flash,
};

#define MANUAL_MODE

int main(int argc, char** argv)
{
    char buffer[512];
    pdm_dev_t dev = NULL;
    int res = webusb_create(&s_mock_ops,
                      0,
                      99,
                      0x37271001,
                      &dev);

    if (res) {
        fprintf(stderr, "webusb_create err: %d\n", res);
        return res;
    }

    for (unsigned i = 0; i < sizeof(test_strings)/sizeof(test_strings[0]); i++) {
        fprintf(stderr,
                "=================================\n"
                "== processing[%d]: %s \n"
                "=================================\n"
#ifdef MANUAL_MODE
                "Press any key to run...\n"
#endif
                ,i, test_strings[i]);

#ifdef MANUAL_MODE
        getchar();
#endif

        *buffer = 0x00;

        res = webusb_process_rpc(
                    dev,
                    test_strings[i],
                    buffer,
                    sizeof(buffer));

        fprintf(stderr, " == result: %d == \n", res);
        fprintf(stderr, " == '%s' == \n", buffer);
    }


    return 0;
}
