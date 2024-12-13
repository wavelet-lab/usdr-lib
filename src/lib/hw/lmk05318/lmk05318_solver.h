#ifndef LMK05318_SOLVER_H
#define LMK05318_SOLVER_H

#include <stdint.h>

#define DIAP_MAX 6

#define MAX(a, b) (a) > (b) ? (a) : (b)
#define MIN(a, b) (a) < (b) ? (a) : (b)

struct range
{
    uint64_t min, max;
};
typedef struct range range_t;

struct intersection
{
    unsigned diap_idxs[DIAP_MAX];
    unsigned count;
    range_t range;
};
typedef struct intersection intersection_t;

struct range_solution
{
    unsigned count;
    intersection_t is[DIAP_MAX * 2];
};
typedef struct range_solution range_solution_t;

#endif // LMK05318_SOLVER_H
