// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <dm_dev.h>
#include <dm_rate.h>
#include <dm_stream.h>

#include <usdr_logging.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <math.h>
#include "cli_parse.h"

#define MAX_BUFFER 512
#define MAX_SENSORS 16

#define SENSORS_CLI_OPTIONS(X) \
    X(OPT_DEVICE, "device", 'd', required_argument, "DEVICE", "Device parameters") \
    X(OPT_INTERVAL, "interval", 'i', required_argument, "SECONDS", "Polling interval in seconds") \
    X(OPT_LOG_LEVEL, "log-level", 'l', required_argument, "LEVEL", "Logging level") \
    X(OPT_SENSORS, "sensors", 's', required_argument, "PATHS", "Semicolon-separated sensor paths") \
    X(OPT_SET, "set", 'S', required_argument, "VALUE", "Set the first sensor to an unsigned value") \
    X(OPT_SAMPLE_RATE, "sample-rate", 'r', required_argument, "SPS", "Set the device sample rate") \
    X(OPT_COUNT, "count", 'c', required_argument, "COUNT", "Maximum number of samples [1]") \
    X(OPT_TYPE, "type", 't', required_argument, "TYPE", "Output type: raw, temp or clock [raw]") \
    X(OPT_LIST, "list", 'L', required_argument, "PATTERN", "List device parameters matching a pattern") \
    X(OPT_HELP, "help", 'h', no_argument, NULL, "Show this help and exit")

CLI_DEFINE_OPTIONS(sensors, SENSORS_CLI_OPTIONS)

int main(UNUSED int argc, UNUSED char** argv)
{
    int res, opt;
    pdm_dev_t dev;
    const char* device = "";
    int interval = 0;
    //char sensors[MAX_BUFFER] = "/dm/sensor/temp;/dm/sensor/temp2";
    char sensors[MAX_BUFFER] = "/dm/sensor/temp";
    char* st, *sptr;
    unsigned sc, i;
    const char* psname[MAX_SENSORS];
    //dm_dev_entity_t psidx[MAX_SENSORS];
    bool set = false;
    unsigned set_val = 0;

    bool rate = false;
    unsigned rate_val = 0;

    usdrlog_setlevel(NULL, USDR_LOG_CRITICAL_WARNING);
    usdrlog_enablecolorize(NULL);
    unsigned count = 1;

    enum sensor_type {
        ST_RAW = 0,
        ST_TEMP = 1,
        ST_CLOCK = 2,
    };
    enum sensor_type type = 0;
    const char *list_pattern = NULL;

    char short_options[3 * SIZEOF_ARRAY(sensors_long_options)];
    res = cli_build_short_options(sensors_long_options, short_options, sizeof(short_options));
    if (res) {
        fprintf(stderr, "Unable to build short option list: %s\n", strerror(-res));
        return 1;
    }

    while ((opt = getopt_long(argc, argv, short_options, sensors_long_options, NULL)) != -1) {
        switch (opt) {
        case OPT_TYPE:
            if (strcmp(optarg, "temp") == 0)
                type = ST_TEMP;
            else if (strcmp(optarg, "clock") == 0)
                type = ST_CLOCK;
            else if (strcmp(optarg, "raw") == 0)
                type = ST_RAW;
            else {
                fprintf(stderr, "Unknown sensor type `%s`\n", optarg);
                exit(1);
            }
            break;
        case OPT_DEVICE:
            device = optarg;
            break;
        case OPT_INTERVAL:
            interval = atof(optarg) * 1000.0;
            break;
        case OPT_LOG_LEVEL:
            usdrlog_setlevel(NULL, cli_parse_int_or_exit("log-level", optarg));
            break;
        case OPT_SENSORS:
            strncpy(sensors, optarg, MAX_BUFFER);  sensors[MAX_BUFFER - 1] = 0;
            break;
        case OPT_SET:
            set = true;
            set_val = cli_parse_unsigned_or_exit("set", optarg);
            break;
        case OPT_SAMPLE_RATE:
            rate = true;
            rate_val = cli_parse_si_unsigned_or_exit("sample-rate", optarg);
            break;
        case OPT_COUNT:
            count = cli_parse_unsigned_or_exit("count", optarg);
            break;
        case OPT_LIST:
            list_pattern = optarg;
            break;
        case OPT_HELP:
            cli_print_usage(stdout, argv[0], "[OPTIONS]",
                            sensors_long_options, sensors_options_help);
            cli_print_si_help(stdout);
            return 0;
        default:
            cli_print_usage(stderr, argv[0], "[OPTIONS]",
                            sensors_long_options, sensors_options_help);
            cli_print_si_help(stderr);
            return 1;
        }
    }

    res = usdr_dmd_create_string(device, &dev);
    if (res) {
        fprintf(stderr, "Unable to create device: errno %d\n", res);
        return 1;
    }

    if (list_pattern) {
        dme_param_t params[16384];
        res = usdr_dme_filter(dev, list_pattern, SIZEOF_ARRAY(params), params);
        for (i = 0; i < res; i++) {
            fprintf(stderr, "Param[%4d] `%s`\n", i, params[i].fullpath);
        }
        fprintf(stderr, "-------\nTotal number of params: %d\n\n", res);
    }

    for (sc = 0, st = sensors; sc < MAX_SENSORS; st = NULL) {
        psname[sc] = strtok_r(st, ";", &sptr);
        if (psname[sc] == NULL)
            break;
        sc++;
    }

    if (sc == 0)
        goto failed;

    for (i = 0; i < sc; i++) {
        fprintf(stderr, "Sensor %d [%32.32s]\n", i, psname[i]);
    }

    if (rate) {
        res = usdr_dmr_rate_set(dev, NULL, rate_val);
        if (res) {
            fprintf(stderr, "Unable to set device rate: errno %d\n", res);
            goto failed;
        }
    }

    if (set) {
        res = usdr_dme_set_uint(dev, psname[0], set_val);
        if (res) {
            goto failed;
        }
    }

    double avg = 0;
    unsigned acnt = 0;
    unsigned genprev = ~0;
    unsigned t, v;
    unsigned *pcm = malloc(sizeof(unsigned) * count);

    uint64_t temp;
    for (unsigned pi = 0 ; pi != count; pi++) {

        for (i = 0; i < sc; i++) {
            res = usdr_dme_get_uint(dev, psname[i], &temp);
            if (res) {
                fprintf(stderr, "Unable to get sensor %d: errno %d\n", i, res);
                break;
            }

            switch (type) {
            case ST_TEMP:
                fprintf(stderr, "Sensor %d: Temp = %.1f C (%08x/%08d)\n", i, temp / 256.0,
                        (unsigned)temp, (unsigned)temp & 0xfffffff);
                avg += temp / 256.0;
                acnt++;
                break;
            case ST_CLOCK:
                if (pi < 3)
                    break;

                t = temp >> 28;
                v = (unsigned)temp & 0xfffffff;
                fprintf(stderr, "Sensor %d: Gen %2d Clock %8d\n", i, t, v);

                if (genprev == ~0) {
                    genprev = t;
                } else if (genprev != t) {
                    genprev = t;
                    avg += v;

                    pcm[acnt] = v;
                    acnt++;
                }
                break;
            case ST_RAW:
                fprintf(stderr, "Sensor %d: Raw value %08x/%08d\n", i, (unsigned)temp, (unsigned)temp);
                break;
            }
        }

        if (interval <= 0)
            break;

        usleep(interval * 1000);
    }

    if (acnt > 0 && type == ST_CLOCK) {
        double div = 0;
        double mid = avg / acnt;
        for (unsigned p = 0; p < acnt; p++) {
            div += (pcm[p] - mid) * (pcm[p] - mid);
        }

        div = sqrt(div) / acnt;

        fprintf(stderr, "Average: %.3f\nDeviation: %.3f\nAvgCount: %d\n", mid, div, acnt);
    } else if (type == ST_CLOCK) {
        fprintf(stderr, "Error: NO_CLOCK\n");
        res = 2;
    }

    free(pcm);
failed:
    usdr_dmd_close(dev);
    return res;
}
