#ifndef DSDR_HIPER
#define DSDR_HIPER

#include "../device.h"
#include "../hw/lms8001/lms8001.h"

#define HIPER_MAX_HW_CHANS 4

enum rx_filterbank {
    RX_FB_400_1000M,
    RX_FB_1000_2000M,
    RX_FB_2000_3500M,
    RX_FB_2500_5000M,
    RX_FB_3500_7100M,

    RX_FB_AUTO = 8,
};

enum antenna_cfg {
    ANT_RX_TRX,   // RX connected to RX antenna and TX connected to TRX antenna
    ANT_TRX_TERM, // RX connected to TRX antenna and TX terminated
    ANT_RX_TERM,  // RX connected to RX antenna and TX terminated
    ANT_LOOPBACK, // RX connected to TX port through attenuator

    AND_HW_TDD,   // TRX antenna is dynamically switched to TX/RX ports based on burst information
};

// IF band path for RX & TX
enum if_band {
    IFBAND_400_3500,
    IFBAND_2200_7200,
    IFBAND_R2RX_1580_2760,
    IFBAND_R2RX_OFF,

    IFBAND_AUTO = 2,
    IFBAND_RX_AUTO = 4,
};

struct fe_chan_config {
    uint8_t rx_ifamp_bp;
    uint8_t rx_band;
    uint8_t rx_fb_sel;
    uint8_t rx_dsa;

    uint8_t tx_band;
    uint8_t ant_sel;

    uint8_t tx_en; // Channel enabled on device side
    uint8_t rx_en; // Channel enabled on device side

    // For auto band & filter selection
    uint64_t rx_freq;
    uint64_t rx_nco;

    uint64_t tx_freq;
    uint64_t tx_nco;

    uint8_t pa_2stage_bypass; // For Rev.2 PA second stage bypass

    uint8_t lms8_pa_gain;
    uint8_t lms8_lna_gain;
    uint8_t lms8_rx_hlmix_gain;
    uint8_t lms8_tx_hlmix_gain;
};
typedef struct fe_chan_config fe_chan_config_t;

#define FE_GPO_REGS 9
#define FE_CTRL_REGS 10

enum {
    HIPER_REV0,
    HIPER_REV2,
};

struct dsdr_hiper_fe {
    lldev_t dev;
    subdev_t subdev;

    unsigned rev;

    lms8001_state_t lms8[6];
    uint64_t lo_lms8_freq[6];

    uint32_t debug_lms8001_last[6];

    uint8_t fe_gpo_regs[FE_GPO_REGS];
    uint32_t fe_ctrl_regs[FE_CTRL_REGS]; // I2C expanders cached registers

    uint32_t debug_fe_reg_last;
    uint32_t debug_exp_reg_last;
    uint32_t debug_usr_reg_last;

    uint32_t adf4002_spiidx;
    uint32_t adf4002_regs[4];
    uint32_t debug_adf4002_reg_last;

    uint32_t ref_int_osc;
    uint32_t ref_gps_osc;


    // LMS8001 smart tune parameter
    uint32_t lms8st_loopbw;
    uint32_t lms8st_phasemargin;
    uint32_t lms8st_bwef_1000;
    uint32_t lms8st_flock_n;
    uint32_t lms8st_iq_gen;
    uint32_t lms8st_int_mod;
    uint32_t lms8st_enabled;

    // High level control
    fe_chan_config_t ucfg[HIPER_MAX_HW_CHANS];
};
typedef struct dsdr_hiper_fe dsdr_hiper_fe_t;


int dsdr_hiper_fe_create(lldev_t dev, unsigned spix_num, unsigned int *lms8_mpw_mask, dsdr_hiper_fe_t* dfe);
int dsdr_hiper_fe_destroy(dsdr_hiper_fe_t* dfe);

int dsdr_hiper_fe_rx_freq_set(dsdr_hiper_fe_t* def, unsigned chno, uint64_t freq, uint64_t* ncotune, bool *p_swap_rxiq);
int dsdr_hiper_fe_tx_freq_set(dsdr_hiper_fe_t* def, unsigned chno, uint64_t freq, uint64_t* ncotune, bool* p_swap_txiq);


int dsdr_hiper_fe_rx_gain_set(dsdr_hiper_fe_t* def, unsigned chno, unsigned gain, unsigned* actual_gain);
int dsdr_hiper_fe_tx_gain_set(dsdr_hiper_fe_t* def, unsigned chno, unsigned gain, unsigned* actual_gain);


int dsdr_hiper_fe_rx_chan_en(dsdr_hiper_fe_t* def, unsigned ch_fe_mask_rx);
int dsdr_hiper_fe_tx_chan_en(dsdr_hiper_fe_t* def, unsigned ch_fe_mask_tx);



#endif
