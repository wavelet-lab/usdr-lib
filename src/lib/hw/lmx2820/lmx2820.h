#ifndef LMX2820_H
#define LMX2820_H

#include <usdr_lowlevel.h>


struct lmx2820_input_chain_st
{
    uint8_t mash_order;
    uint8_t vco_core;
    uint64_t fosc_in;
    bool osc_2x;
    uint16_t pll_r_pre;
    uint8_t mult;
    uint8_t pll_r;
    uint16_t pll_n;
    uint32_t pll_num;
    uint32_t pll_den;
    double fvco;
    double fpd;
};
typedef struct lmx2820_input_chain_st lmx2820_input_chain_t;

struct lmx2820_output_chain_st
{
    uint8_t chdiva;
    uint8_t chdivb;
    uint8_t outa_mux;
    uint8_t outb_mux;
    double rfouta;
    double rfoutb;
};
typedef struct lmx2820_output_chain_st lmx2820_output_chain_t;

struct lmx2820_state {
    lldev_t dev;
    unsigned subdev;
    unsigned lsaddr;

    lmx2820_input_chain_t lmx2820_input_chain;
    lmx2820_output_chain_t lmx2820_output_chain;
};
typedef struct lmx2820_state lmx2820_state_t;


int lmx2820_create(lldev_t dev, unsigned subdev, unsigned lsaddr, lmx2820_state_t* st);
int lmx2820_destroy(lmx2820_state_t* st);
int lmx2820_solver(lmx2820_state_t* st, uint64_t osc_in, unsigned mash_order, unsigned force_mult, uint64_t rfouta, uint64_t rfoutb);
int lmx2820_tune(lmx2820_state_t* st, uint64_t osc_in, unsigned mash_order, unsigned force_mult, uint64_t rfouta, uint64_t rfoutb);

#endif // LMX2820_H
