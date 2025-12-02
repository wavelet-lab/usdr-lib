// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <dm_dev.h>
#include <dm_rate.h>
#include <dm_stream.h>
#include <usdr_logging.h>

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include <poll.h>

// PID parameters
#define KP 0.75    // Proportional gain
#define KI 0.02    // Integral gain
#define KD 0.0075  // Derivative gain
#define DT 1.0     // Update interval (1 second)
#define HZ_PER_STEP 0.01
#define INTEGRAL_MAX 1e6
#define OUTPUT_MAX 1e-3
#define DEVIATION_MAX 7e-6

typedef struct {
    uint32_t bits;
    uint32_t center;
    uint32_t min_value;
    uint32_t max_value;
    double hz_per_step; // Hz per DAC step
} DAC;

int dac_init(DAC *dac, uint32_t dac_bits, uint32_t freq)
{
    if (!dac)
        return 1;
    if (dac_bits < 8 || dac_bits > 16)
        return 2;
    dac->bits = dac_bits;
    dac->min_value = 0;
    dac->max_value = (1 << dac_bits) - 1;
    dac->center = dac->max_value >> 1;
    dac->hz_per_step = 2 * DEVIATION_MAX * (double)freq / dac->max_value;
    return 0;
}

// DAC control
uint32_t set_dac_value(pdm_dev_t dev, const DAC *dac, uint32_t dac_offset, double step)
{
    static const char *dac_vctcxo_path = "/dm/sdr/0/dac_vctcxo";
    static uint64_t old_dac_value = 0;
    // double dac_delta = freq_offset / dac_gain; // Δdac = Δf / gain
    // uint32_t dac_value = (uint32_t) (dac_mid + dac_delta);
    uint32_t dac_value = (uint32_t) lround(dac_offset + step);
    if (dac_value < 0)
        dac_value = 0;
    if (dac_value > dac->max_value)
        dac_value = dac->max_value;
    
    if (dac_value != old_dac_value) {
        // Send to DAC
        printf(
            "DAC value: %d (%d bits, dac_offset %d, step %f)\n", dac_value, dac->bits, dac_offset, step);
        const int res = usdr_dme_set_uint(dev, dac_vctcxo_path, (uint64_t)dac_value);
        if (res) {
            fprintf(stderr, "Unable to set vctcxo dac value: errno %d\n", res);
            return res;
        }
        old_dac_value = dac_value;
    }
    return dac_value;
}

typedef struct
{
    double kp;
    double ki;
    double kd;
    double integral;
    double prev_error;
    double integral_max;
    double output_max;
} PID;

void pid_init(PID *p, double kp, double ki, double kd, double integral_max, double output_max)
{
    p->kp = kp;
    p->ki = ki;
    p->kd = kd;
    p->integral = 0.0;
    p->prev_error = 0.0;
    p->integral_max = integral_max;
    p->output_max = output_max;
}

double pid_update(PID *p, double error, double dt)
{
    double P = p->kp * error;
    p->integral += error * dt;
    if (p->integral > p->integral_max)
        p->integral = p->integral_max;
    if (p->integral < -p->integral_max)
        p->integral = -p->integral_max;
    double I = p->ki * p->integral;
    double D = p->kd * (error - p->prev_error) / dt;
    p->prev_error = error;
    double out = P + I + D;
    if (out > p->output_max)
        out = p->output_max;
    if (out < -p->output_max)
        out = -p->output_max;
    return out;
}

// Stub for getting PPS count and measured frequency from FPGA
uint8_t get_pps_count(uint64_t ppsparm)
{
    return (ppsparm >> 28) & 0xf;
}

uint32_t get_measured_freq(uint64_t ppsparm)
{
    return ppsparm & 0xfffffff;
}

void show_usage(const char *procname)
{
    fprintf(stderr, "Usage %s:\n", procname);
    fprintf(stderr, "  Options:\n");
    fprintf(stderr, "    -d <device>           - device\n");
    // fprintf(stderr, "    -f <target_frequency> - target frequency of oscillator [26e6] Hz\n");
    fprintf(stderr, "    -o <frequency>        - external oscilator frequency [25e6] Hz\n");
    fprintf(stderr, "    -r <target_rate>      - target samplerate [4e6] Samples\n");
    fprintf(stderr, "    -h                    - this help\n");
}

int main(int argc, char **argv)
{
    const char *pps_path = "/dm/sensor/freqpps";//"/dm/sdr/0/clkmeas";
    const char *ext_osc_path = "/dm/sdr/refclk/path";
    const char *ext_osc_parm = "external";
    const char *ext_freq_path = "/dm/sdr/refclk/frequency";
    const uint64_t ext_freq_value = 25000000ull;
    const double dt = 1.0;    // Update interval (1 second)
    const int dac_bits = 16;  // DAC bitness
    
    int res, opt;
    uint64_t ppsparm = 0;
    pdm_dev_t dev;
    const char *device = "";//"fe=exm2pe:gps_on:osc_on";
    // double target_freq = 26e6; // Target frequency, 26 MHz
    double osc_freq = (double)ext_freq_value;
    double target_freq = 4e6; // Target samplerate, 4 Msps
    bool new_holdover_msg = true;

    while ((opt = getopt(argc, argv, "d:f:o:r:h")) != -1) {
        switch (opt) {
        case 'd':
            device = optarg;
            break;
        // case 'f':
        //     target_freq = atof(optarg);
        //     if (target_freq < 100.0)
        //         target_freq *= 1e6;
        //     break;
        case 'o':
            osc_freq = atof(optarg);
            if (osc_freq < 100.0)
                osc_freq *= 1e6;
            break;
        case 'r':
            target_freq = atof(optarg);
            if (target_freq < 100.0)
                target_freq *= 1e6;
            break;
        case 'h':
        default:
            show_usage(argv[0]);
            return 1;
        }
    }
    
    usdrlog_setlevel(NULL, USDR_LOG_WARNING);
    usdrlog_enablecolorize(NULL);
    
    res = usdr_dmd_create_string(device, &dev);
    if (res) {
        fprintf(stderr, "Unable to create device: errno %d\n", res);
        return 1;
    }
    
    res = usdr_dme_set_uint(dev, ext_freq_path, (uint64_t)osc_freq);
    if (res) {
        fprintf(stderr, "Unable to setup external oscilator frequency %" PRId64 ": errno %d\n", ext_freq_value, res);
        usdr_dmd_close(dev);
        return 1;
    }
    
    res = usdr_dme_set_uint(dev, ext_osc_path, (uint64_t) ext_osc_parm);
    if (res) {
        fprintf(stderr, "Unable to select external oscilator: errno %d\n", res);
        usdr_dmd_close(dev);
        return 1;
    }
    
    res = usdr_dmr_rate_set(dev, NULL, (uint64_t) target_freq);
    if (res) {
        fprintf(stderr, "Unable to set device rate: errno %d", res);
        usdr_dmd_close(dev);
        return 1;
    }
    
    pusdr_dms_t usds_rx = NULL;
    res = usdr_dms_create(dev, "/ll/srx/0", "ci16", 1, 4096, &usds_rx);
    if (res) {
        fprintf(stderr, "Unable to initialize RX data stream: errno %d", res);
        usdr_dmd_close(dev);
        return 1;
    }
    
    res = usdr_dms_sync(dev, "any", 1, &usds_rx);
    if (res) {
        fprintf(stderr, "Unable to sync data streams: errno %d", res);
        usdr_dmd_close(dev);
        return 1;
    }
    
    res = usds_rx ? usdr_dms_op(usds_rx, USDR_DMS_START, 0) : -EPROTONOSUPPORT;
    if (res) {
        fprintf(stderr, "Unable to start RX data stream: errno %d", res);
        usdr_dmd_close(dev);
        return 1;
    }
    
    res = usdr_dme_get_uint(dev, pps_path, &ppsparm);
    if (res) {
        fprintf(stderr, "Unable to get pps: errno %d\n", res);
        usdr_dmd_close(dev);
        return 1;
    }
    // Previous PPS count for trigger detection
    int prev_pps_count = get_pps_count(ppsparm);
    
    DAC dac;
    res = dac_init(&dac, dac_bits, target_freq);
    if (res) {
        fprintf(stderr, "Unable to init DAC for dac bits %d: errno %d", dac_bits, res);
        usdr_dmd_close(dev);
        return 1;
    }

    PID pid;
    pid_init(&pid, KP, KI, KD, INTEGRAL_MAX, OUTPUT_MAX);


    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // Create timerfd for 100 ms polling
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (timer_fd == -1) {
        perror("timerfd_create");
        return 1;
    }

    struct itimerspec timer_spec;
    timer_spec.it_value.tv_sec = 0;
    timer_spec.it_value.tv_nsec = 100000000; // 100 ms
    timer_spec.it_interval = timer_spec.it_value; // Repeat every 100 ms
    if (timerfd_settime(timer_fd, 0, &timer_spec, NULL) == -1) {
        perror("timerfd_settime");
        
        return 1;
    }

    struct pollfd fds[1];
    fds[0].fd = timer_fd;
    fds[0].events = POLLIN;

    const double holdover_interval = 2 * dt;
    uint32_t dac_offset = dac.center; // initial
    double measured_freq = 0.0, error = 0.0;

    while (true) {
        // Wait for timer event
        int ret = poll(fds, 1, -1);
        if (ret == -1) {
            perror("poll");
            break;
        }

        if (fds[0].revents & POLLIN) {
            uint64_t expirations;
            read(timer_fd, &expirations, sizeof(expirations)); // Clear timer event

            res = usdr_dme_get_uint(dev, pps_path, &ppsparm);
            if (res) {
                fprintf(stderr, "Unable to get pps: errno %d\n", res);
                break;
            }

            const uint8_t current_pps_count = get_pps_count(ppsparm);
            
            // int current_pps_count = get_pps_count();
            
            // Check if PPS count changed (trigger for update)
            if (current_pps_count != prev_pps_count) {
                const uint8_t pps_delta = current_pps_count < prev_pps_count
                                              ? current_pps_count + 0x10 - prev_pps_count
                                              : current_pps_count - prev_pps_count;
                measured_freq = (double)get_measured_freq(ppsparm);
                error = (measured_freq - target_freq) / target_freq;
                
                printf("PPS=0x%" PRIx64 ", pps_count=%d, measured_freq = %.0f, error = %.3f ppm\n",
                    ppsparm,
                    current_pps_count,
                    measured_freq,
                    error * 1e6);
                
                if (error > 0.02) {
                    printf("Error more than 2%%, skip iteration\n");
                    continue;
                }

                double pout = pid_update(&pid, error, dt);
                // map pid output (fractional) to Hz correction
                double freq_corr_hz = -pout * target_freq; // desired Hz change
                double steps_cmd = freq_corr_hz / dac.hz_per_step;
                printf("POUT=%f, freq_corr_hz=%f, steps_cmd=%f\n", pout, freq_corr_hz, steps_cmd);
                // limit steps
                if (steps_cmd > 2000.0)
                    steps_cmd = 2000.0;
                if (steps_cmd < -2000.0)
                    steps_cmd = -2000.0;
                dac_offset = set_dac_value(dev, &dac, dac_offset, steps_cmd);
                
                prev_pps_count = current_pps_count;

                // Reset timer for holdover tracking
                clock_gettime(CLOCK_MONOTONIC, &start_time);
            }

            // Check elapsed time for holdover
            struct timespec current_time;
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            double elapsed = (current_time.tv_sec - start_time.tv_sec)
                             + (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
            if (elapsed > holdover_interval) {
                // Apply last known offset or extrapolate
                if (new_holdover_msg) {
                    printf("No PPS trigger for more than %.1f seconds - entering holdover mode\n", holdover_interval);
                    new_holdover_msg = false;
                }
            } else {
                if (!new_holdover_msg) {
                    printf("Found PPS trigger\n");
                    new_holdover_msg = true;
                }
            }
        }
    }

    close(timer_fd);
    usdr_dmd_close(dev);

    return 0;
}
