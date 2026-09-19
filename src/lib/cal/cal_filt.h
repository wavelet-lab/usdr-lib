#ifndef CAL_FILT_H
#define CAL_FILT_H

#include <usdr_port.h>

struct cfolat64 {
    float i, q;
};
typedef struct cfolat64 cfolat64_t;

void convert_dump12_to_cfloat(const uint32_t* in, cfolat64_t* o, unsigned size);
void calculate_mean_s_std(const cfolat64_t* d, const unsigned size, float* m, float* di);

void cal_generate_iq(cfolat64_t* d, const unsigned size, float amp, float period, float ph);
void cal_remove_dc(cfolat64_t* d, const unsigned size, float* pavg_i, float* pavg_q);

void cal_find_best_fit(const cfolat64_t* d, const unsigned size, float amp, float period, float* mse_min, float *phase);
void cal_find_best_fit_ph_per(const cfolat64_t* d, const unsigned size, float amp, float *period_opt, float* mse_min, float* phase_opt);


float cal_max_amp_error(const cfolat64_t* d, const unsigned size, float amp, float period, float ph);


#endif
