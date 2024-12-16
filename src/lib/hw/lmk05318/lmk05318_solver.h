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

#endif // LMK05318_SOLVER_H
