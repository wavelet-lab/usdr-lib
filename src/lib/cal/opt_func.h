// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

/* Optimization function libraries */
/* minimal search using gloden ratio */


#ifndef OPT_FUNC_H
#define OPT_FUNC_H

#include "stdint.h"
#include "stdbool.h"
#include "time.h"

#define MAX(a, b) \
  ({ __typeof__ (a) _a = (a); \
     __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define MIN(a, b) \
  ({ __typeof__ (a) _a = (a); \
     __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

typedef int (*evaluate_fn_t)(void* param, int value, int* func);

/* find local minimum of fn() within [start; stop] range
 * px stored x
 * pfx stored fn(x)
 */
int find_golden_min(int start, int stop, void* param, evaluate_fn_t fn, int* px, int* pfx, int exparam);
int find_iterate_min(int start, int stop, void* param, evaluate_fn_t fn, int* px, int* pfx, int exparam);

typedef int (*search_function_t)(int start, int stop, void* param, evaluate_fn_t fn, int* px, int* pfx, int exparam);
typedef int (*evaluate_fnN_t)(void* param, int val_param, int val, int* func);

struct opt_par_limits
{
    int min;
    int max;
};
typedef struct opt_par_limits opt_par_limits_t;

enum search_function
{
    SF_ITERATE_MIN,
    SF_GOLDEN_MIN,
};

struct opt_iteration2d
{
    evaluate_fnN_t func;
    search_function_t sf;
    opt_par_limits_t limit[2];
    int exparam; //Extra tune parameter for optimization function
};

int find_best_2d(struct opt_iteration2d *ops, unsigned max_count, void* param, int stop_when,
                 int *px, int *py, int *pfxy);

uint64_t find_gcd(uint64_t a, uint64_t b);
void binary_print_u32(uint32_t x, char* s, bool reverse);

static inline uint64_t clock_get_time()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000LL + (uint64_t)ts.tv_nsec/1000LL;
}

#endif
