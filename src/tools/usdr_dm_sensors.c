// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <dm_dev.h>
#include <dm_rate.h>
#include <dm_stream.h>

#include <usdr_logging.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <math.h>

#define MAX_BUFFER 512
#define MAX_SENSORS 16

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

    while ((opt = getopt(argc, argv, "d:i:l:s:S:r:c:t:")) != -1) {
        switch (opt) {
        case 't':
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
        case 'd':
            device = optarg;
            break;
        case 'i':
            interval = atof(optarg) * 1000.0;
            break;
        case 'l':
            usdrlog_setlevel(NULL, atoi(optarg));
            break;
        case 's':
            strncpy(sensors, optarg, MAX_BUFFER);  sensors[MAX_BUFFER - 1] = 0;
            break;
        case 'S':
            set = true;
            set_val = atoi(optarg);
            break;
        case 'r':
            rate = true;
            rate_val = atof(optarg);
            break;
        case 'c':
            count = atoi(optarg);
            break;
        }
    }

    res = usdr_dmd_create_string(device, &dev);
    if (res) {
        fprintf(stderr, "Unable to create device: errno %d\n", res);
        return 1;
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
