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

enum lmx2820_sysref_in_fmt
{
    SRREQ_CMOS    = 0,
    SRREQ_CMOS_AC = 1,
    SRREQ_LVDS_AC = 2,
    SRREQ_LVDS_DC = 3,
};
typedef enum lmx2820_sysref_in_fmt lmx2820_sysref_in_fmt_t;

struct lmx2820_sysref_chain_st
{
    uint64_t srout;
    bool enabled;
    bool master_mode; //true:master mode, false:repeater mode
    bool cont_pulse;  //true:continious, false:pulsed
    uint8_t pulse_cnt;
    uint8_t delay_ctrl;
    double delay;
    uint8_t div_pre;
    uint16_t div;
    double srout_fact;
    //
    uint8_t srreq_fmt;
};
typedef struct lmx2820_sysref_chain_st lmx2820_sysref_chain_t;

struct lmx2820_stats {
    float temperature;
    uint16_t vco_sel;
    uint16_t vco_capctrl;
    uint16_t vco_daciset;
    uint16_t lock_detect_status;
};
typedef struct lmx2820_stats lmx2820_stats_t;

struct lmx2820_state {
    lldev_t dev;
    unsigned subdev;
    unsigned lsaddr;

    uint16_t instcal_dly;
    lmx2820_input_chain_t lmx2820_input_chain;
    lmx2820_output_chain_t lmx2820_output_chain;
    lmx2820_sysref_chain_t lmx2820_sysref_chain;
};
typedef struct lmx2820_state lmx2820_state_t;

int lmx2820_solver(lmx2820_state_t* st, uint64_t osc_in, unsigned mash_order, unsigned force_mult, uint64_t rfouta, uint64_t rfoutb);
int lmx2820_solver_instcal(lmx2820_state_t* st, uint64_t rfouta, uint64_t rfoutb);

int lmx2820_create(lldev_t dev, unsigned subdev, unsigned lsaddr, lmx2820_state_t* st);
int lmx2820_destroy(lmx2820_state_t* st);
int lmx2820_get_temperature(lmx2820_state_t* st, float* value);
int lmx2820_read_status(lmx2820_state_t* st, lmx2820_stats_t* status);
int lmx2820_tune(lmx2820_state_t* st, uint64_t osc_in, unsigned mash_order, unsigned force_mult, uint64_t rfouta, uint64_t rfoutb);
int lmx2820_instant_calibration_init(lmx2820_state_t* st, uint64_t osc_in, unsigned mash_order, unsigned force_mult);
int lmx2820_tune_instcal(lmx2820_state_t* st, uint64_t rfouta, uint64_t rfoutb);
int lmx2820_sync(lmx2820_state_t* st);
int lmx2820_reset(lmx2820_state_t* st);
int lmx2820_wait_pll_lock(lmx2820_state_t* st, unsigned timeout);

#endif // LMX2820_H
