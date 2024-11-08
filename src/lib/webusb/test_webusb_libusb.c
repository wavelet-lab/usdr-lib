// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "webusb.h"
#include <stdio.h>
#include <libusb-1.0/libusb.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define MANUAL_MODE

struct libusb_websdr {
    pdm_dev_t dev;

    libusb_context* ctx;
    libusb_device_handle *dh;

    unsigned run;
    unsigned rx_burts;
    unsigned rx_blocksize;

    unsigned to;
};

static
int libusb_websdr_write_raw(uintptr_t param, uint8_t ep, unsigned bytes, const void *data)
{
    struct libusb_websdr* dev = (struct libusb_websdr*)param;
    int actual_length, res;

    res = libusb_bulk_transfer(dev->dh,
                               ep | LIBUSB_ENDPOINT_OUT,
                               (unsigned char*)data,
                               bytes,
                               &actual_length,
                               dev->to);
    if (res)
        return -res;

    return actual_length;
}

static
int libusb_websdr_read_raw(uintptr_t param, uint8_t ep, unsigned bytes, void *data)
{
    struct libusb_websdr* dev = (struct libusb_websdr*)param;
    int actual_length, res;

    res = libusb_bulk_transfer(dev->dh,
                               ep | LIBUSB_ENDPOINT_IN,
                               (unsigned char*)data,
                               bytes,
                               &actual_length,
                               dev->to);
    if (res)
        return -res;

    return actual_length;
}

int libusb_websdr_log(uintptr_t param, unsigned sevirity, const char* log)
{
    fprintf(stderr, "mock_log [%d]     :%s", sevirity, log);
    int len = strlen(log);
    if ((len > 0) && log[len - 1] != '\n')
        fprintf(stderr, "\n");

    return 0;
}

static
struct webusb_ops s_ops = {
    libusb_websdr_read_raw,
    libusb_websdr_write_raw,
    libusb_websdr_log,
};


//API
int libusb_websdr_create(unsigned loglevel, struct libusb_websdr** out)
{
    libusb_context* ctx;
    libusb_device_handle *dh;
    struct libusb_websdr* wu;
    int res;

    // Init ctx & open device
    if (libusb_init(&ctx)) {
        return -EFAULT;
    }

    unsigned i;
    uint32_t vidpids[] = {
        0xfaefdea9,
        0xfaeedea9,
        0x0403601f,
        0x37271001,
        0x37271011,
    };

    for (i = 0; i < SIZEOF_ARRAY(vidpids); i++) {
        fprintf(stderr, "Probing %08x\n", vidpids[i]);
        dh = libusb_open_device_with_vid_pid(ctx, vidpids[i] >> 16, vidpids[i] & 0xFFFF);
        if (dh)
            break;
    }

    if (i < SIZEOF_ARRAY(vidpids)) {
        fprintf(stderr, " *** Created device %08x\n", vidpids[i]);
    } else {
        fprintf(stderr, "No device found\n");
        return 1;
    }

    wu = (struct libusb_websdr* )malloc(sizeof(*wu));
    memset(wu, 0, sizeof(*wu));
    wu->ctx = ctx;
    wu->dh = dh;
    wu->run = 0;
    wu->rx_blocksize = 0;
    wu->rx_burts = 0;
    wu->to = 5000;

    res = webusb_create(&s_ops, (uintptr_t)(void*)wu, loglevel, vidpids[i], &wu->dev);
    if (res)
        return res;

    *out = wu;
    return 0;
}

static
    char s_rpc_temperature[] =
    "{"
    "\"req_method\":\"sdr_get_sensor\","
        "\"id\":\"abyr_valg123456\","
        "\"req_params\": {\"sensor\":\"sdr_temp\"}"
    "}";

static
    char s_rpc_get_parameter[] =
    "{"
    "\"req_method\":\"sdr_get_parameter\","
        "\"id\":\"abyr_valg123457\","
        "\"req_params\": {\"path\":\"/dm/sdr/refclk/frequency\"}"
    "}";

static
    char s_rpc_set_parameter[] =
    "{"
    "\"req_method\":\"sdr_set_parameter\","
        "\"id\":\"abyr_valg123459\","
        "\"req_params\": {\"path\":\"/dm/sdr/refclk/frequency\",\"value\":26000000}"
    "}";

static
char s_rpc_init[] =
        "{"
        "\"req_method\":\"sdr_init_streaming\","
        "\"req_params\": {"
            "\"chans\":1,"
            "\"samplerate\":1000000,"
            "\"packetsize\":4080,"
            "\"throttleon\":990000"
            "}"
        "}";

static
char s_rpc_revision[] =
        "{"
        "\"req_method\":\"sdr_get_revision\","
        "\"id\":\"abyr_valg123458\","
        "\"req_params\": {}"
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
//{"req_method":"sdr_set_rx_frequency", "req_params":{"param_type":"rf", "frequency":433000000}}

int libusb_websdr_run(struct libusb_websdr* d, unsigned samplerate)
{
    char buffer[512];

    int res = webusb_process_rpc(d->dev,
                                 s_rpc_revision,
                                 buffer,
                                 sizeof(buffer));

    printf("RES=%d RPC_Result: `%s`\n", res, buffer);
    if(res)
        return res;

#ifdef MANUAL_MODE
    printf("press any key to continue...\n");
    getchar();
#endif

    res = webusb_process_rpc(d->dev,
                             s_rpc_temperature,
                             buffer,
                             sizeof(buffer));

    printf("RES=%d RPC_Result: `%s`\n", res, buffer);
    if(res)
        return res;

#ifdef MANUAL_MODE
    printf("press any key to continue...\n");
    getchar();
#endif

    res = webusb_process_rpc(d->dev,
                             s_rpc_get_parameter,
                             buffer,
                             sizeof(buffer));

    printf("RES=%d RPC_Result: `%s`\n", res, buffer);
    if(res)
        return res;

#ifdef MANUAL_MODE
    printf("press any key to continue...\n");
    getchar();
#endif

    res = webusb_process_rpc(d->dev,
                             s_rpc_set_parameter,
                             buffer,
                             sizeof(buffer));

    printf("RES=%d RPC_Result: `%s`\n", res, buffer);
    if(res)
        return res;

#ifdef MANUAL_MODE
    printf("press any key to continue...\n");
    getchar();
#endif

    res = webusb_process_rpc(d->dev,
            s_rpc_init,
            buffer,
            sizeof(buffer));

    printf("RES=%d RPC_Result: `%s`\n", res, buffer);
    if(res)
        return res;

#ifdef MANUAL_MODE
    printf("press any key to continue...\n");
    getchar();
#endif

    res = webusb_process_rpc(d->dev,
                             s_rpc_set_freq,
                             buffer,
                             sizeof(buffer));

    printf("RES=%d RPC_Result: `%s`\n", res, buffer);
    if(res)
        return res;

#ifdef MANUAL_MODE
    printf("press any key to continue...\n");
    getchar();
#endif

    return res;

}

uint8_t dummy_buffer[1024 * 1024];

int main(int argc, char** argv)
{
    struct libusb_websdr* wsdr;
    int res, actual_length = 0;

    res = libusb_websdr_create(99, &wsdr);
    if (res) {
        fprintf(stderr, "Unable to create: %d\n", res);
        return 1;
    }

    res = libusb_websdr_run(wsdr, 4e6);
    if (res) {
        fprintf(stderr, "Unable to run: %d\n", res);
        return 1;
    }

    // Recieve test packet
    res = libusb_bulk_transfer(wsdr->dh,
                               0x03 | LIBUSB_ENDPOINT_IN,
                               (unsigned char*)dummy_buffer,
                               sizeof(dummy_buffer),
                               &actual_length,
                               5000);
    if (res) {
        fprintf(stderr, "Unable to get data: %d '%s'\n", res, libusb_error_name(res));
        return 1;
    }

    fprintf(stderr, "Obtained %d bytes!\n", actual_length);

    libusb_close(wsdr->dh);

    res = webusb_destroy(wsdr->dev);
    if(res)
        fprintf(stderr, "ERROR on webusb_destroy() : %d\n", res);
    else
        fprintf(stderr, "Closed ok\n");

    return 0;
}

