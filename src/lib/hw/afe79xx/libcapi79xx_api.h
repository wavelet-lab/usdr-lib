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
typedef int (*libcapi79xx_init_fn_t)(libcapi79xx_t* o);

#define LIBCAPI79XX_CREATE_FN "libcapi79xx_create"
#define LIBCAPI79XX_DESTROY_FN "libcapi79xx_destroy"
#define LIBCAPI79XX_INIT_FN "libcapi79xx_init"


#endif
