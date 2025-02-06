#ifndef LIBCAPI79XX_API_H
#define LIBCAPI79XX_API_H

#include <stdint.h>
#include <stdio.h>

// Callback functions
enum afe79xx_chip_type {
    AFE7900 = 7900,
    AFE7901 = 7901,
    AFE7903 = 7903,
    AFE7950 = 7950,
};

enum nco_type {
    NCO_RX = 0,
    NCO_TX = 1,
    NCO_FB = 2,
};

typedef struct libcapi79xx libcapi79xx_t;
struct libcapi79xx {
    // APIs
    int (*spi_reg_write)(libcapi79xx_t* dev, uint16_t addr, uint8_t data);
    int (*spi_reg_read)(libcapi79xx_t* dev, uint16_t addr, uint8_t *data);

    int (*give_sysref_pulse)(libcapi79xx_t* dev);

    void *user;
    void *driver_handle; // Do not touch
};

typedef int (*libcapi79xx_create_fn_t)(libcapi79xx_t* o, enum afe79xx_chip_type chip);
typedef int (*libcapi79xx_destroy_fn_t)(libcapi79xx_t* o);
typedef int (*libcapi79xx_init_fn_t)(libcapi79xx_t* o, const char* configuration);

typedef int (*libcapi79xx_upd_nco_fn_t)(libcapi79xx_t* o, unsigned type, unsigned ch, uint64_t freq, unsigned ncono, unsigned band);
typedef int (*libcapi79xx_get_nco_fn_t)(libcapi79xx_t* o, unsigned type, unsigned ch, uint64_t* freq, unsigned ncono, unsigned band);

typedef int (*libcapi79xx_check_health_fn_t)(libcapi79xx_t* o, int *ok, unsigned sz, char* buf);

#define LIBCAPI79XX_CREATE_FN "libcapi79xx_create"
#define LIBCAPI79XX_DESTROY_FN "libcapi79xx_destroy"
#define LIBCAPI79XX_INIT_FN "libcapi79xx_init"
#define LIBCAPI79XX_UPD_NCO_FN "libcapi79xx_upd_nco"
#define LIBCAPI79XX_GET_NCO_FN "libcapi79xx_get_nco"

#define LIBCAPI79XX_CHECK_HEALTH_FN  "libcapi79xx_check_health"

#endif
