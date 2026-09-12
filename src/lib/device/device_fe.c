// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "device_fe.h"
#include "device_vfs.h"

#include "ext_exm2pe/board_exm2pe.h"
#include "ext_pciefe/ext_pciefe.h"
#include "ext_supersync/ext_supersync.h"
#include "ext_simplesync/ext_simplesync.h"
#include "ext_fe_100_5000/ext_fe_100_5000.h"
#include "ext_xmass/ext_xmass.h"
#include "ext_fe_ch4_400_7200/ext_fe_ch4_400_7200.h"

#include <stdlib.h>
#include <string.h>
#include <usdr_logging.h>

static int _debug_pciefe_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int _debug_pciefe_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int _debug_pciefe_cmd_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int _debug_pciefe_cmd_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int _debug_ext_fe_100_5000_cmd_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int _debug_ext_fe_100_5000_cmd_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);


static int _debug_simplesync_lmk05318_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int _debug_simplesync_lmk05318_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int _debug_xmass_lmk05318_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int _debug_xmass_lmk05318_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int _debug_lmk05318_calfreq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int _debug_lmk5c33216_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int _debug_lmk5c33216_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int _debug_xmass_ctrl_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int _debug_xmass_ctrl_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int _debug_xmass_calfreq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int _debug_xmass_calfreq_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int _debug_xmass_calpath_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int _debug_typefe_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int _debug_ll_mdev_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static
const usdr_dev_param_func_t s_fe_params[] = {
    { "/ll/fe/type",            { NULL, _debug_typefe_reg_get }},
    { "/ll/mdev",               { _debug_ll_mdev_set, NULL}},
};

static const usdr_dev_param_func_t s_fe_pcie_params[] = {
    { "/debug/hw/pciefe/0/reg", { _debug_pciefe_reg_set, _debug_pciefe_reg_get }},
    { "/debug/hw/pciefe_cmd/0/reg", { _debug_pciefe_cmd_set, _debug_pciefe_cmd_get }},
};

static const usdr_dev_param_func_t s_simplesync_params[] = {
    { "/debug/hw/lmk05318/0/reg", { _debug_simplesync_lmk05318_reg_set, _debug_simplesync_lmk05318_reg_get }},
    { "/dm/sync/cal/freq", { _debug_lmk05318_calfreq_set, NULL }},
};

static const usdr_dev_param_func_t s_lmk5c33216_params[] = {
    { "/debug/hw/lmk5c33216/0/reg", { _debug_lmk5c33216_reg_set, _debug_lmk5c33216_reg_get }},
};

static const usdr_dev_param_func_t s_ext_fe_100_5000_params[] = {
    { "/debug/hw/fe_100_5000_cmd/0/reg", { _debug_ext_fe_100_5000_cmd_set, _debug_ext_fe_100_5000_cmd_get }},
};

static const usdr_dev_param_func_t s_xmass_params[] = {
    { "/debug/hw/lmk05318/0/reg", { _debug_xmass_lmk05318_reg_set, _debug_xmass_lmk05318_reg_get }},
    { "/debug/hw/xmass_ctrl/0/reg", { _debug_xmass_ctrl_reg_set, _debug_xmass_ctrl_reg_get }},
    { "/dm/sync/cal/freq", { _debug_xmass_calfreq_set, _debug_xmass_calfreq_get }},
    { "/dm/sdr/0/sync/cal/freq", { _debug_xmass_calfreq_set, _debug_xmass_calfreq_get }},
    { "/dm/sdr/0/sync/cal/path", { _debug_xmass_calpath_set, NULL }},
};


enum fe_type {
    FET_PCIE_DEVBOARD,
    FET_PCIE_XMASS,
    FET_PCIE_SUPER_SYNC,
    FET_PCIE_SIMPLE_SYNC,
    FET_PICE_BREAKOUT,
    FET_PCIE_FE1005000,
    FET_PCIE_FE4CH4007200,
    FET_COUNT
};
typedef enum fe_type fe_type_t;

static const char* s_fe_names[] = {
    "pciefe",
    "xmass",
    "supersync",
    "simplesync",
    "exm2pe",
    "fe1005000",
    "fe4ch4007200",
};


struct dev_fe {
    lldev_t parent_dev;
    fe_type_t type;

    union {
        board_exm2pe_t exm2pe;
        board_ext_pciefe_t devboard;
        board_ext_simplesync_t simplesync;
        board_ext_supersync_t supersync;
        ext_fe_100_5000_t fe_100_5000;
        board_xmass_t xmass;
        ext_fe_ch4_400_7200_t fe_4ch_400_7200;
    } fe;

    uint32_t debug_pciefe_last;
    uint32_t debug_pciefe_cmd_last;
    uint32_t debug_ext_fe_100_5000_cmd_last;
    uint32_t debug_lmk05318_last;
    uint32_t debug_lmk5c33216_last;
    uint32_t debug_xmass_ctrl_last;
};
typedef struct dev_fe dev_fe_t;

int _debug_ll_mdev_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    int res;
    dev_fe_t* o = (dev_fe_t*)obj->object;
    o->parent_dev = (lldev_t)value;


    // Do trigger events
    // Set freq params
    if (o->type == FET_PCIE_SIMPLE_SYNC) {
        USDR_LOG("DEFE", USDR_LOG_ERROR, "Configuring all sdrs in the array!\n");

        res = usdr_device_vfs_obj_val_set_by_path(o->parent_dev->pdev, "/dm/sdr/refclk/path", (uintptr_t)"external");
        if (res)
            return res;

        res = usdr_device_vfs_obj_val_set_by_path(o->parent_dev->pdev, "/dm/sdr/refclk/frequency", 50000000);
        if (res)
            return res;
    }

    return 0;
}


int device_fe_probe(device_t* base, const char* compat, const char* fename, unsigned def_i2c_loc, dev_fe_t** out)
{
    lldev_t dev = base->dev;
    unsigned i;
    int res;
    dev_fe_t *dfe = (dev_fe_t*)malloc(sizeof(dev_fe_t));
    unsigned vfidx = 0;
    const char* hint = fename;

    if (hint == NULL) {
        hint = getenv("USDR_FE_TYPE");
        if (hint) {
            USDR_LOG("DEFE", USDR_LOG_WARNING, "Detected USDR_FE_TYPE variable `%s`", hint);
        }
    }

    // TODO obtain cores base address
    const unsigned gpiobase = 3;
    const unsigned uartbase = 54;
    const unsigned spiext_cfg = 58;

    memset(dfe, 0, sizeof(dev_fe_t));

    usdr_core_info_t fe_gpio;
    usdr_core_info_t fe_uart;
    uint64_t spi_busno  = ~0ul;
    uint64_t i2c_busno  = ~0ul;

    usdr_device_vfs_link_get_corenfo(base, "/ll/fe/0/gpio_busno/0", "/ll/gpio", &fe_gpio);
    usdr_device_vfs_link_get_corenfo(base, "/ll/fe/0/uart_busno/0", "/ll/uart", &fe_uart);
    usdr_device_vfs_obj_val_get_u64(base, "/ll/fe/0/spi_busno/0", &spi_busno);
    usdr_device_vfs_obj_val_get_u64(base, "/ll/fe/0/i2c_busno/0", &i2c_busno);

    for (i = 0; i < FET_COUNT; i++) {
        const char* hint_strip = NULL;
        if (hint) {
            size_t szhint = strlen(hint);
            size_t szfe = strlen(s_fe_names[i]);
            if (szhint < szfe)
                continue;

            if (strncmp(hint, s_fe_names[i], szfe))
                continue;

            hint_strip = hint + szfe;
        }

        switch (i) {
        case FET_PICE_BREAKOUT: res = board_exm2pe_init(dev, 0, gpiobase, uartbase, hint_strip, compat, def_i2c_loc, &dfe->fe.exm2pe); break;
        case FET_PCIE_DEVBOARD: res = board_ext_pciefe_init(dev, 0, gpiobase, uartbase, hint_strip, compat, def_i2c_loc, &dfe->fe.devboard); break;
        case FET_PCIE_SUPER_SYNC: res = board_ext_supersync_init(dev, 0, gpiobase, compat, def_i2c_loc, &dfe->fe.supersync); break;
        case FET_PCIE_SIMPLE_SYNC: res = board_ext_simplesync_init(dev, 0, gpiobase, compat, def_i2c_loc, &dfe->fe.simplesync); break;
        case FET_PCIE_FE1005000: res = ext_fe_100_5000_init(dev, 0, gpiobase, spiext_cfg, 4, hint_strip, compat, &dfe->fe.fe_100_5000); break;
        case FET_PCIE_XMASS: res = board_xmass_init(dev, 0, gpiobase, compat, def_i2c_loc, &dfe->fe.xmass); break;
        case FET_PCIE_FE4CH4007200: res = ext_fe_ch4_400_7200_init(dev, 0, gpiobase, hint_strip, compat, &dfe->fe.fe_4ch_400_7200); break;
        default: res = -EIO; goto failed;
        }

        if (res == 0) {
            break;
        } else if (res != -ENODEV) {
            USDR_LOG("DEFE", USDR_LOG_ERROR, "Unable to initialize %s, error %d\n", s_fe_names[i], res);
            if (hint != NULL)
                goto failed;
        }
    }

    if (i == FET_COUNT) {
        if (hint == NULL) {
            USDR_LOG("DEFE", USDR_LOG_NOTE, "No external FE was detected, provide fe=`frontend` for a strong hint\n");
            res = 0;
            goto failed;
        }

        USDR_LOG("DEFE", USDR_LOG_WARNING, "No external FE was detected with `%s` hint and %s filter\n", hint, compat);
        return -ENODEV;
    }

    dfe->type = (fe_type_t)i;

    USDR_LOG("DEFE", USDR_LOG_WARNING, "Detected external FE: %s\n", s_fe_names[i]);
    res = usdr_vfs_obj_param_init_array_param(base,
                                              (void*)dfe,
                                              s_fe_params,
                                              SIZEOF_ARRAY(s_fe_params));
    if (res)
        goto failed;

    vfidx += SIZEOF_ARRAY(s_fe_params);

    switch (dfe->type) {
    case FET_PCIE_DEVBOARD:
        res = usdr_vfs_obj_param_init_array_param(base,
                                                  (void*)dfe,
                                                  s_fe_pcie_params,
                                                  SIZEOF_ARRAY(s_fe_pcie_params));
        break;
    case FET_PCIE_SIMPLE_SYNC:
        res = usdr_vfs_obj_param_init_array_param(base,
                                                  (void*)dfe,
                                                  s_simplesync_params,
                                                  SIZEOF_ARRAY(s_simplesync_params));
        break;
    case FET_PCIE_SUPER_SYNC:
        res = usdr_vfs_obj_param_init_array_param(base,
                                                  (void*)dfe,
                                                  s_lmk5c33216_params,
                                                  SIZEOF_ARRAY(s_lmk5c33216_params));
        break;
    case FET_PCIE_FE1005000:
        res = usdr_vfs_obj_param_init_array_param(base,
                                                  (void*)dfe,
                                                  s_ext_fe_100_5000_params,
                                                  SIZEOF_ARRAY(s_ext_fe_100_5000_params));
        break;
    case FET_PCIE_XMASS:
        res = usdr_vfs_obj_param_init_array_param(base,
                                                  (void*)dfe,
                                                  s_xmass_params,
                                                  SIZEOF_ARRAY(s_xmass_params));
        break;
    default:
        break;
    }

    if (res)
        goto failed;

    *out = dfe;
    return res;

failed:
    *out = NULL;
    free(dfe);
    return res;
}

void* device_fe_to(struct dev_fe* obj, const char* type)
{
    if (!obj || strcmp(s_fe_names[obj->type], type) != 0) {
        return NULL;
    }

    return (void*)&obj->fe;
}

int _debug_typefe_reg_get(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dev_fe_t* o = (dev_fe_t*)obj->object;
    *ovalue = (uintptr_t)s_fe_names[o->type];
    return 0;
}

int device_fe_destroy(struct dev_fe* obj)
{
    //TODO deinit

    free(obj);
    return 0;
}


int _debug_pciefe_reg_get(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dev_fe_t* o = (dev_fe_t*)obj->object;
    *ovalue = o->debug_pciefe_last;
    return 0;
}


int _debug_pciefe_reg_set(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_fe_t* o = (dev_fe_t*)obj->object;
    int res;
    unsigned addr = (value >> 16) & 0x7f;
    unsigned data = value & 0xffff;

    o->debug_pciefe_last = ~0u;

    if (value & 0x800000) {
        res = board_ext_pciefe_ereg_wr(&o->fe.devboard, addr, data);
        USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug PCIEFE WR REG %04x => %04x\n",
                 lowlevel_get_devname(o->fe.devboard.dev), (unsigned)addr, data);
    } else {
        res = board_ext_pciefe_ereg_rd(&o->fe.devboard, addr, &o->debug_pciefe_last);
        USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug PCIEFE RD REG %04x <= %04x\n",
                 lowlevel_get_devname(o->fe.devboard.dev), (unsigned)addr,
                 o->debug_pciefe_last);
    }

    return res;
}

int _debug_ext_fe_100_5000_cmd_get(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dev_fe_t* o = (dev_fe_t*)obj->object;
    *ovalue = o->debug_ext_fe_100_5000_cmd_last;
    return 0;
}

int _debug_ext_fe_100_5000_cmd_set(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_fe_t* o = (dev_fe_t*)obj->object;
    int res;
    unsigned addr = (value >> 24) & 0x7f;
    unsigned data = value & 0xffffff;

    o->debug_ext_fe_100_5000_cmd_last = ~0u;

    if (value & 0x80000000) {
        res = ext_fe_100_5000_cmd_wr(&o->fe.fe_100_5000, addr, data);
        USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug FE_100_5000 WR CMD %04x => %06x\n",
                 lowlevel_get_devname(o->fe.fe_100_5000.dev), (unsigned)addr, data);
    } else {
        res = ext_fe_100_5000_cmd_rd(&o->fe.fe_100_5000, addr, &o->debug_ext_fe_100_5000_cmd_last);
        USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug FE_100_5000 RD CMD %04x <= %06x\n",
                 lowlevel_get_devname(o->fe.fe_100_5000.dev), (unsigned)addr,
                 o->debug_ext_fe_100_5000_cmd_last);
    }
    return res;
}

int _debug_pciefe_cmd_get(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dev_fe_t* o = (dev_fe_t*)obj->object;
    *ovalue = o->debug_pciefe_cmd_last;
    return 0;
}


int _debug_pciefe_cmd_set(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_fe_t* o = (dev_fe_t*)obj->object;
    int res;
    unsigned addr = (value >> 16) & 0x7f;
    unsigned data = value & 0xffff;

    o->debug_pciefe_cmd_last = ~0u;

    if (value & 0x800000) {
        res = board_ext_pciefe_cmd_wr(&o->fe.devboard, addr, data);

        USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug PCIEFE WR CMD %04x => %04x\n",
                 lowlevel_get_devname(o->fe.devboard.dev), (unsigned)addr, data);
    } else {
        res = board_ext_pciefe_cmd_rd(&o->fe.devboard, addr, &o->debug_pciefe_cmd_last);

        USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug PCIEFE RD CMD %04x <= %04x\n",
                 lowlevel_get_devname(o->fe.devboard.dev), (unsigned)addr,
                 o->debug_pciefe_cmd_last);
    }

    return res;
}

static int _debug_lmk05318_reg_get(dev_fe_t* o, uint64_t* ovalue)
{
    *ovalue = o->debug_lmk05318_last;
    return 0;
}

int _debug_simplesync_lmk05318_reg_get(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    return _debug_lmk05318_reg_get((dev_fe_t*)obj->object, ovalue);
}

int _debug_xmass_lmk05318_reg_get(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    return _debug_lmk05318_reg_get((dev_fe_t*)obj->object, ovalue);
}

int _debug_lmk05318_reg_set(dev_fe_t* o, lmk05318_state_t* lmk, uint64_t value)
{
    int res;
    unsigned addr = (value >> 8) & 0x7fff;
    unsigned data = value & 0xff;
    uint8_t d;

    o->debug_lmk05318_last = ~0u;

    if (!(value & 0x800000)) {
        res = lmk05318_reg_wr(lmk, addr, data);

        USDR_LOG("XDEV", USDR_LOG_WARNING, "LMK05318 WR REG %04x => %04x\n",
                 (unsigned)addr, data);
    } else {
        d = 0xff;
        res = lmk05318_reg_rd(lmk, addr, &d);
        o->debug_lmk05318_last = d;

        USDR_LOG("XDEV", USDR_LOG_WARNING, "LMK05318 RD REG %04x <= %04x\n",
                 (unsigned)addr,
                 o->debug_lmk05318_last);
    }

    return res;
}

int _debug_simplesync_lmk05318_reg_set(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_fe_t* o = (dev_fe_t*)obj->object;
    return _debug_lmk05318_reg_set(o, &o->fe.simplesync.lmk, value);
}

int _debug_xmass_lmk05318_reg_set(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_fe_t* o = (dev_fe_t*)obj->object;
    return _debug_lmk05318_reg_set(o, &o->fe.xmass.lmk, value);
}

int _debug_xmass_ctrl_reg_set(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_fe_t* o = (dev_fe_t*)obj->object;
    unsigned addr = (value >> 24) & 0x7f;
    unsigned data = value & 0xffffff;
    int res;
    board_xmass_t* board = &o->fe.xmass;
    uint32_t d;
    o->debug_xmass_ctrl_last = ~0u;

    if (value & 0x80000000) {
        res = board_xmass_ctrl_cmd_wr(board, addr, data);

        USDR_LOG("XDEV", USDR_LOG_WARNING, "XMASS_CTRL WR REG %04x => %04x\n",
                 (unsigned)addr, data);
    } else {
        d = 0xffffff;
        res = board_xmass_ctrl_cmd_rd(board, addr, &d);
        o->debug_xmass_ctrl_last = d;

        USDR_LOG("XDEV", USDR_LOG_WARNING, "XMASS_CTRL RD REG %04x <= %04x\n",
                 (unsigned)addr,
                 o->debug_xmass_ctrl_last);
    }

    return res;
}

int _debug_xmass_ctrl_reg_get(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dev_fe_t* o = (dev_fe_t*)obj->object;
    *ovalue = o->debug_xmass_ctrl_last;
    return 0;
}

int _debug_xmass_calfreq_set(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_fe_t* o = (dev_fe_t*)obj->object;
    return board_xmass_tune_cal_lo(&o->fe.xmass, value);
}

int _debug_xmass_calpath_set(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_fe_t* o = (dev_fe_t*)obj->object;
    return board_xmass_sync_source(&o->fe.xmass, value);
}

int _debug_xmass_calfreq_get(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dev_fe_t* o = (dev_fe_t*)obj->object;
    *ovalue = o->fe.xmass.calfreq;
    return 0;
}

int _debug_lmk05318_calfreq_set(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_fe_t* o = (dev_fe_t*)obj->object;
    return simplesync_tune_lo(&o->fe.simplesync, value);
}


int _debug_lmk5c33216_reg_get(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dev_fe_t* o = (dev_fe_t*)obj->object;
    *ovalue = o->debug_lmk5c33216_last;
    return 0;
}


int _debug_lmk5c33216_reg_set(pdevice_t ud_x, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_fe_t* o = (dev_fe_t*)obj->object;
    int res;
    unsigned addr = (value >> 8) & 0x7fff;
    unsigned data = value & 0xff;
    uint8_t d;

    o->debug_lmk5c33216_last = ~0u;

    if (value & 0x800000) {
        res = lmk_5c33216_reg_wr(&o->fe.supersync.lmk, addr, data);

        USDR_LOG("XDEV", USDR_LOG_WARNING, "LMK5C33216 WR REG %04x => %04x\n",
                 (unsigned)addr, data);
    } else {
        d = 0xff;
        res = lmk_5c33216_reg_rd(&o->fe.supersync.lmk, addr, &d);
        o->debug_lmk5c33216_last = d;

        USDR_LOG("XDEV", USDR_LOG_WARNING, "5C33216 RD REG %04x <= %04x\n",
                 (unsigned)addr,
                 o->debug_lmk5c33216_last);
    }

    return res;
}

