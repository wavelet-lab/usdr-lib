#include "cal_filt.h"
#include "opt_func.h"
#include "math.h"

void convert_dump12_to_cfloat(const uint32_t* in, cfolat64_t* o, unsigned size)
{
    for (unsigned i = 0; i < size; i++) {
        int16_t ii = (uint16_t)((in[i] & 0xfff) << 4);
        int16_t iq = (uint16_t)(((in[i] >> 12) & 0xfff) << 4);
        o[i].i = ii / 32768.0;
        o[i].q = iq / 32768.0;
    }
}

void cal_remove_dc(cfolat64_t* d, const unsigned size, float* pavg_i, float* pavg_q)
{
    float ai = 0, aq = 0;
    for (unsigned i = 0; i < size; i++) {
        ai += d[i].i;
        aq += d[i].q;
    }
    ai /= size;
    aq /= size;

    for (unsigned i = 0; i < size; i++) {
        d[i].i -= ai;
        d[i].q -= aq;
    }

    if (pavg_i)
        *pavg_i = ai;
    if (pavg_q)
        *pavg_q = aq;
}


void calculate_mean_s_std(const cfolat64_t* d, const unsigned size, float* m, float* di)
{
    float tmp[size];
    float mean_s = 0;
    float std_s = 0;

    for (unsigned i = 0; i < size; i++) {
        float v = sqrt(d[i].i * d[i].i + d[i].q * d[i].q);
        tmp[i] = v;
        mean_s += v;
    }
    mean_s /= size;

    for (unsigned i = 0; i < size; i++) {
        float v = tmp[i];
        std_s += (v - mean_s) * (v - mean_s);
    }
    std_s /= size;

    *m = mean_s;
    *di = sqrt(std_s);
}

void cal_generate_iq(cfolat64_t* d, const unsigned size, float amp, float period, float ph)
{
    for (unsigned i = 0; i < size; i++) {
        float s, c;
        float phase = ph + 2 * M_PI * i / period;
        sincosf(phase, &s, &c);

        d[i].i = s * amp;
        d[i].q = c * amp;
    }
}

float _cal_mse_fit(const cfolat64_t* d, const unsigned size, float amp, float period, float ph)
{
    cfolat64_t sig[size];
    cal_generate_iq(sig, size, amp, period, ph);

    float err = 0;
    for (unsigned i = 0; i < size; i++) {
        err += sqrt((d[i].i - sig[i].i) * (d[i].i - sig[i].i) + (d[i].q - sig[i].q) * (d[i].q - sig[i].q));
    }
    return err;
}

struct evaluate_mse_fit {
    const cfolat64_t* orig;
    unsigned size;
    float amp;
    float period;
    float phase;

    float scale_phase;
    float scale_period;
    float scale_result;
};
typedef struct evaluate_mse_fit evaluate_mse_fit_t;

static int _evaluate_mse_fit_fn(void* param, int value, int* func)
{
    evaluate_mse_fit_t* p = (evaluate_mse_fit_t*)param;
    float ph = value * p->scale_phase;
    float res = _cal_mse_fit(p->orig, p->size, p->amp, p->period, ph);
    *func = res * p->scale_result;
    return 0;
}

void cal_find_best_fit(const cfolat64_t* d, const unsigned size, float amp, float period, float* mse_min, float* phase_opt)
{
    const unsigned ph_steps = 1024;
    evaluate_mse_fit_t fd;
    int phase;
    int mse_val;

    fd.orig = d;
    fd.size = size;
    fd.amp = amp;
    fd.period = period;

    fd.scale_phase = 2 * M_PI / ph_steps;
    fd.scale_result = 1000;

    find_golden_min(0, ph_steps - 1, &fd, &_evaluate_mse_fit_fn, &phase, &mse_val, 0);

    *mse_min = mse_val / fd.scale_result;
    *phase_opt = phase * fd.scale_phase;
}


static int _evaluate_mse_fit_pp_fn(void* param, int val_param, int value, int* func)
{
    evaluate_mse_fit_t* p = (evaluate_mse_fit_t*)param;
    if (val_param == 0) {
        p->phase = value * p->scale_phase;
    } else {
        p->period = value / p->scale_period;
    }

    if (func == NULL)
        return 0;

    float res = _cal_mse_fit(p->orig, p->size, p->amp, p->period, p->phase);
    *func = res * p->scale_result;
    return 0;
}

void cal_find_best_fit_ph_per(const cfolat64_t* d, const unsigned size, float amp, float *period_opt, float* mse_min, float* phase_opt)
{
    const unsigned ph_steps = 1024;
    const unsigned STEPS = 2;
    evaluate_mse_fit_t fd;
    struct opt_iteration2d o[STEPS];

    int phase;
    int period;
    int mse_val;

    fd.orig = d;
    fd.size = size;
    fd.amp = amp;
    fd.period = 0;
    fd.phase = 0;

    fd.scale_phase = 2 * M_PI / ph_steps;
    fd.scale_result = 10000;
    fd.scale_period = 100;

    for (unsigned i = 0; i < STEPS; i++) {
        o[i].limit[0].min = 0;
        o[i].limit[0].max = ph_steps - 1;
        o[i].limit[1].min = 8 * fd.scale_period;
        o[i].limit[1].max = 384 * fd.scale_period;
        o[i].func = _evaluate_mse_fit_pp_fn;
        o[i].sf = &find_golden_min;
        o[i].exparam = 0;
    }

    find_best_2d(&o[0], SIZEOF_ARRAY(o), &fd, 0, &phase, &period, &mse_val);

    *mse_min = mse_val / fd.scale_result;
    *phase_opt = phase * fd.scale_phase;
    *period_opt = period / fd.scale_period;
}

float cal_max_amp_error(const cfolat64_t* d, const unsigned size, float amp, float period, float ph)
{
    cfolat64_t sig[size];
    cal_generate_iq(sig, size, amp, period, ph);

    float err = 0;
    for (unsigned i = 0; i < size; i++) {
        float d_i = (d[i].i - sig[i].i);
        float d_q = (d[i].q - sig[i].q);

        if (d_i < 0)
            d_i = -d_i;
        if (d_q < 0)
            d_q = -d_q;

        if (err < d_i)
            err = d_i;
        if (err < d_q)
            err = d_q;
    }
    return err;
}

