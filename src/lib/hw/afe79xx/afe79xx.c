#include "afe79xx.h"
#include <stdlib.h>
#include <usdr_logging.h>
#include <dlfcn.h>
#include <stdlib.h>


enum afe79xx_regs {
    CHIP_TYPE = 3,
    CHIP_ID_LO = 4,
    CHIP_ID_HI = 5,
    CHIP_VER = 6,
    VENDOR_ID_LO = 7,
    VENDOR_ID_HI = 8,
};

static int afe79xx_wr(afe79xx_state_t* d, uint16_t regno, uint8_t data)
{
    return lowlevel_spi_tr32(d->dev, d->subdev, d->addr,
                             ((regno & 0x7fff) << 8) | data, NULL);
}

static int afe79xx_rd(afe79xx_state_t* d, uint16_t regno, uint8_t* odata)
{
    uint32_t od;
    int res = lowlevel_spi_tr32(d->dev, d->subdev, d->addr,
                                (0x800000 | ((regno & 0x7fff) << 8)),
                                &od);
    if (res)
        return res;

    *odata = od;
    return 0;
}

int capi79xx_spi_reg_write(libcapi79xx_t* dev, uint16_t addr, uint8_t data)
{
    //USDR_LOG("79xx", USDR_LOG_DEBUG, "AFE_WR[%04x] <= %02x\n", addr, data);
    afe79xx_state_t* d = container_of(dev, afe79xx_state_t, capi);
    return afe79xx_wr(d, addr, data);
}

int capi79xx_spi_reg_read(libcapi79xx_t* dev, uint16_t addr, uint8_t *data)
{
    afe79xx_state_t* d = container_of(dev, afe79xx_state_t, capi);
    return afe79xx_rd(d, addr, data);
}

int capi79xx_give_sysref_pulse(libcapi79xx_t* dev)
{
    USDR_LOG("79xx", USDR_LOG_WARNING, "NEED SYSREF PULSE!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    return 0;
}


static int _dummy_create(libcapi79xx_t* o, enum afe79xx_chip_type chip)
{
    return 0;
}
static int _dummy_destroy(libcapi79xx_t* o)
{
    return 0;
}
static int _dummy_init(libcapi79xx_t* o, const char* configuration)
{
    return 0;
}
static int _dummy_upd_nco(libcapi79xx_t* o, unsigned type, unsigned ch, uint64_t freq, unsigned ncono, unsigned band)
{
    return 0;
}
static int _dummy_get_nco(libcapi79xx_t* o, unsigned type, unsigned ch, uint64_t* freq, unsigned ncono, unsigned band)
{
    *freq = 700e6;
    return 0;
}


int afe79xx_create_dummy(afe79xx_state_t* out)
{
    out->libcapi79xx_create = &_dummy_create;
    out->libcapi79xx_destroy = &_dummy_destroy;
    out->libcapi79xx_init = &_dummy_init;

    out->libcapi79xx_upd_nco = _dummy_upd_nco;
    out->libcapi79xx_get_nco = _dummy_get_nco;

    return 0;
}

int afe79xx_create(lldev_t dev, unsigned subdev, unsigned lsaddr, unsigned chipType, afe79xx_state_t* out)
{
    int res;
    const char* afe79xxlib = "liblibcapi79xx.so";
    unsigned g;

    out->dev = dev;
    out->subdev = subdev;
    out->addr = lsaddr;

    uint8_t reg_id[VENDOR_ID_HI - CHIP_TYPE + 1];
    for (unsigned j = CHIP_TYPE; j <= VENDOR_ID_HI; j++) {
        res = afe79xx_rd(out, j, reg_id + j - CHIP_TYPE);
        if (res)
            return res;
    }

    const char* nl = getenv("AFECAPI");
    if (nl) {
        afe79xxlib = nl;
    }

    USDR_LOG("79xx", USDR_LOG_WARNING, "AFE79XX Type = %02x ChipID = %02x%02x Ver = %02x VendorID = %02x%02x LIB=`%s`\n",
             reg_id[0], reg_id[1], reg_id[2], reg_id[3], reg_id[4], reg_id[5], afe79xxlib);

    out->capi.spi_reg_write = &capi79xx_spi_reg_write;
    out->capi.spi_reg_read = &capi79xx_spi_reg_read;
    out->capi.give_sysref_pulse = &capi79xx_give_sysref_pulse;
    out->capi.log_vout = &usdrlog_vout;
    out->capi.user = NULL;
    out->capi.driver_handle = NULL;

    out->dl_handle = dlopen(afe79xxlib, RTLD_NOW);
    if (out->dl_handle == NULL) {
        USDR_LOG("79xx", USDR_LOG_ERROR, "Couldn't load CAPI AFE79XX NDA LIB wrapper `%s`: %s!\n",
                 afe79xxlib, dlerror());
        return -EFAULT;
    }

    out->libcapi79xx_create = (libcapi79xx_create_fn_t)dlsym(out->dl_handle, LIBCAPI79XX_CREATE_FN);
    out->libcapi79xx_destroy = (libcapi79xx_destroy_fn_t)dlsym(out->dl_handle, LIBCAPI79XX_DESTROY_FN);
    out->libcapi79xx_init = (libcapi79xx_init_fn_t)dlsym(out->dl_handle, LIBCAPI79XX_INIT_FN);

    out->libcapi79xx_upd_nco = (libcapi79xx_upd_nco_fn_t)dlsym(out->dl_handle, LIBCAPI79XX_UPD_NCO_FN);
    out->libcapi79xx_get_nco = (libcapi79xx_get_nco_fn_t)dlsym(out->dl_handle, LIBCAPI79XX_GET_NCO_FN);

    out->libcapi79xx_set_dsa = (libcapi79xx_set_dsa_fn_t)dlsym(out->dl_handle, LIBCAPI79XX_SET_DSA_FN);

    out->libcapi79xx_check_health = (libcapi79xx_check_health_fn_t)dlsym(out->dl_handle, LIBCAPI79XX_CHECK_HEALTH_FN);

    if (!out->libcapi79xx_create || !out->libcapi79xx_destroy || !out->libcapi79xx_init ||
        !out->libcapi79xx_set_dsa ||
        !out->libcapi79xx_upd_nco || !out->libcapi79xx_get_nco || !out->libcapi79xx_check_health) {
        USDR_LOG("79xx", USDR_LOG_ERROR, "Broken CAPI AFE79XX NDA LIB wrapper `%s`!\n",
                 afe79xxlib);

        dlclose(out->dl_handle);
        return -EFAULT;
    }

    // TODO: version check
    return out->libcapi79xx_create(&out->capi, (enum afe79xx_chip_type)chipType);
}

int afe79xx_init(afe79xx_state_t* afe, const char* configuration)
{
    return afe->libcapi79xx_init(&afe->capi, configuration);
}
