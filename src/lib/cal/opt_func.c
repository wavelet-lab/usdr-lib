// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "opt_func.h"
#include <limits.h>
#include <usdr_logging.h>
#include <stdio.h>

int find_golden_min(int start, int stop, void* param, evaluate_fn_t fn, int* px, int* pv, int exparam)
{
    float gr = 0.61803398875f;
    int a = start;
    int b = stop;
    int d = (b - a) * gr;
    int x1 = a + d;
    int x2 = b - d;
    int res;
    int f1, f2;
    res = fn(param, x1, &f1);
    if (res)
        return res;

    res = fn(param, x2, &f2);
    if (res)
        return res;

    while (d > 0) {
        if (f1 < f2) {
            a = x2;
            x2 = x1;
            f2 = f1;
            d = (b - a) * gr;
            if (d == 0)
                break;
            x1 = a + d;

            res = fn(param, x1, &f1);
            if (res)
                return res;

        } else {
            b = x1;
            x1 = x2;
            f1 = f2;
            d = (b - a) * gr;
            if (d == 0)
                break;
            x2 = b - d;

            res = fn(param, x2, &f2);
            if (res)
                return res;
        }
    }

    *px = x1;
    *pv = f1;
    return 0;
}


int find_iterate_min(int start, int stop, void* param, evaluate_fn_t fn, int* px, int* pv, int exparam)
{
    int delta = (start > stop) ? (-1 - exparam): (1 + exparam);
    int i, res;
    int ev;
    int best = INT_MAX;
    int best_i = -1;

    for (i = start; i < stop; i = i + delta) {
        res = fn(param, i, &ev);
        if (res)
            return res;

        if (ev < best) {
            best = ev;
            best_i = i;
        }
    }

    *px = best_i;
    *pv = best;
    return 0;
}


struct _helper_fn_data
{
    evaluate_fnN_t func;
    void* param;
    int idx;
   // int exparam; //Extra parameter
};

static int _helper_find_best_eval(void* param, int value, int* func)
{
    struct _helper_fn_data *fd = (struct _helper_fn_data *)param;
    int res = fd->func(fd->param, fd->idx, value, func);
    USDR_LOG("OPTF", USDR_LOG_ERROR, " iteration idx=%d i=%d f=%d\n",
             fd->idx, value, *func);
    return res;
}

int find_best_2d(struct opt_iteration2d *ops, unsigned max_count, void* param, int stop_when,
                 int *px, int *py, int *pfxy)
{
    int fxy = INT_MAX;
    int res;
    unsigned iter;
    int xmin = ops[0].limit[0].min;
    int xmax = ops[0].limit[0].max;
    int ymin = ops[0].limit[1].min;
    int ymax = ops[0].limit[1].max;
    int x = (xmin + xmax) / 2;
    int y = (ymin + ymax) / 2;
    struct _helper_fn_data fd;
    fd.param = param;
    int bx = x;
    int by = y;
    int bfxy;

    // Set initial Y as we're goint to iterate over X
    ops[0].func(fd.param, 1, y, NULL);
    bfxy = fxy;

    for (iter = 0; iter < max_count; iter++) {
        struct opt_iteration2d *c = &ops[iter];
        //fd.exparam = ops->exparam;
        fd.func = c->func;
        fd.idx = 0;
        res = c->sf(MAX(x + c->limit[0].min, xmin),
                    MIN(x + c->limit[0].max, xmax),
                    (void*)&fd, _helper_find_best_eval, &x, &fxy, c->exparam);
        if (res)
            return res;

        if (fxy < bfxy) {
            bx = x;
            bfxy = fxy;
        }
        c->func(fd.param, 0, bx, NULL);

        fd.idx = 1;
        res = c->sf(MAX(y + c->limit[1].min, ymin),
                    MIN(y + c->limit[1].max, ymax),
                    (void*)&fd, _helper_find_best_eval, &y, &fxy, c->exparam);
        if (res)
            return res;

        if (fxy < bfxy) {
            by = y;
            bfxy = fxy;
        }
        USDR_LOG("OPTF", USDR_LOG_ERROR, "Best2D Iteration %d: best X=%d Y=%d F=%d\n",
                 iter, bx, by, bfxy);

        c->func(fd.param, 1, by, NULL);

        if (fxy < stop_when)
            break;

        // Start over with the best
        x = bx;
        y = by;
    }


    *px = bx;
    *py = by;
    *pfxy = bfxy;
    return 0;
}

// Function to implement Stein's Algorithm
// Borrowed from: https://www.geeksforgeeks.org/steins-algorithm-for-finding-gcd/ (C)
//
uint64_t find_gcd(uint64_t a, uint64_t b)
{
    if (a == b)
        return a;

    // GCD(0, b) == b; GCD(a, 0) == a,
    // GCD(0, 0) == 0
    if (a == 0)
        return b;
    if (b == 0)
        return a;

    // look for factors of 2
    if (~a & 1) // a is even
    {
        if (b & 1) // b is odd
            return find_gcd(a >> 1, b);
        else // both a and b are even
            return find_gcd(a >> 1, b >> 1) << 1;
    }

    if (~b & 1) // a is odd, b is even
        return find_gcd(a, b >> 1);

    // reduce larger number
    if (a > b)
        return find_gcd((a - b) >> 1, b);

    return find_gcd((b - a) >> 1, a);
}

void binary_print_u32_reverse(uint32_t x, char* s)
{
    sprintf(s, "%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u",
           (x >>  0) & 0x1,
           (x >>  1) & 0x1,
           (x >>  2) & 0x1,
           (x >>  3) & 0x1,
           (x >>  4) & 0x1,
           (x >>  5) & 0x1,
           (x >>  6) & 0x1,
           (x >>  7) & 0x1,
           (x >>  8) & 0x1,
           (x >>  9) & 0x1,
           (x >> 10) & 0x1,
           (x >> 11) & 0x1,
           (x >> 12) & 0x1,
           (x >> 13) & 0x1,
           (x >> 14) & 0x1,
           (x >> 15) & 0x1,
           (x >> 16) & 0x1,
           (x >> 17) & 0x1,
           (x >> 18) & 0x1,
           (x >> 19) & 0x1,
           (x >> 20) & 0x1,
           (x >> 21) & 0x1,
           (x >> 22) & 0x1,
           (x >> 23) & 0x1,
           (x >> 24) & 0x1,
           (x >> 25) & 0x1,
           (x >> 26) & 0x1,
           (x >> 27) & 0x1,
           (x >> 28) & 0x1,
           (x >> 29) & 0x1,
           (x >> 30) & 0x1,
           (x >> 31) & 0x1
    );
}
