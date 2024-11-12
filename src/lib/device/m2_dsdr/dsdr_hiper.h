#ifndef DSDR_HIPER
#define DSDR_HIPER

#include "../device.h"
#include "../hw/lms8001/lms8001.h"


struct dsdr_hiper_fe {
    lldev_t dev;
    subdev_t subdev;

    lms8001_state_t lms8[6];
    uint64_t lo_lms8_freq[6];

    uint32_t debug_lms8001_last[6];

    uint8_t fe_gpo_regs[9];
    uint32_t debug_fe_reg_last;

    uint32_t debug_exp_reg_last;

    uint32_t adf4002_regs[4];
    uint32_t debug_adf4002_reg_last;

    uint32_t ref_int_osc;
    uint32_t ref_gps_osc;
};
typedef struct dsdr_hiper_fe dsdr_hiper_fe_t;


int dsdr_hiper_fe_create(lldev_t dev, unsigned spix_num, dsdr_hiper_fe_t* dfe);
int dsdr_hiper_fe_destroy(dsdr_hiper_fe_t* dfe);

#endif
