// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "device_fe.h"
#include "device_vfs.h"
#include "device_impl.h"

#include "ext_exm2pe/board_exm2pe.h"
#include "ext_pciefe/ext_pciefe.h"
#include "ext_supersync/ext_supersync.h"
#include "ext_simplesync/ext_simplesync.h"
#include "ext_fe_100_5000/ext_fe_100_5000.h"

#include <stdlib.h>
#include <string.h>
#include <usdr_logging.h>

static int _debug_pciefe_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int _debug_pciefe_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int _debug_pciefe_cmd_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int _debug_pciefe_cmd_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int _debug_ext_fe_100_5000_cmd_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int _debug_ext_fe_100_5000_cmd_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);


static int _debug_lmk05318_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int _debug_lmk05318_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int _debug_lmk05318_calfreq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int _debug_lmk5c33216_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int _debug_lmk5c33216_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int _debug_typefe_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static int _debug_ll_mdev_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static
const usdr_dev_param_func_t s_fe_params[] = {
    { "/ll/fe/type",            { NULL, _debug_typefe_reg_get }},
    { "/ll/mdev",               { _debug_ll_mdev_set, NULL}},
};

static
const usdr_dev_param_func_t s_fe_pcie_params[] = {
    { "/debug/hw/pciefe/0/reg", { _debug_pciefe_reg_set, _debug_pciefe_reg_get }},
    { "/debug/hw/pciefe_cmd/0/reg", { _debug_pciefe_cmd_set, _debug_pciefe_cmd_get }},
};

static
const usdr_dev_param_func_t s_lmk05318_params[] = {
    { "/debug/hw/lmk05318/0/reg", { _debug_lmk05318_reg_set, _debug_lmk05318_reg_get }},
    { "/dm/sync/cal/freq", { _debug_lmk05318_calfreq_set, NULL }},
};

static
const usdr_dev_param_func_t s_lmk5c33216_params[] = {
    { "/debug/hw/lmk5c33216/0/reg", { _debug_lmk5c33216_reg_set, _debug_lmk5c33216_reg_get }},
};


static
const usdr_dev_param_func_t s_ext_fe_100_5000_params[] = {
    { "/debug/hw/fe_100_5000_cmd/0/reg", { _debug_ext_fe_100_5000_cmd_set, _debug_ext_fe_100_5000_cmd_get }},
};


enum fe_type {
    FET_PCIE_DEVBOARD,
    FET_PCIE_SUPER_SYNC,
    FET_PCIE_SIMPLE_SYNC,
    FET_PICE_BREAKOUT,
    FET_PCIE_FE1005000,

    FET_COUNT
};
typedef enum fe_type fe_type_t;

static const char* s_fe_names[] = {
    "pciefe",
    "supersync",
    "simplesync",
    "exm2pe",
    "fe1005000",
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
    } fe;

    uint32_t debug_pciefe_last;
    uint32_t debug_pciefe_cmd_last;
    uint32_t debug_ext_fe_100_5000_cmd_last;

    // TODO: check for a proper storage
    usdr_vfs_obj_base_t vfs_cfg_obj[SIZEOF_ARRAY(s_fe_params) + SIZEOF_ARRAY(s_fe_pcie_params) + SIZEOF_ARRAY(s_lmk05318_params) + SIZEOF_ARRAY(s_lmk5c33216_params)];
};
typedef struct dev_fe dev_fe_t;


static
int _usdr_ext_i2c(lldev_t dev, subdev_t subdev, unsigned ls_op, lsopaddr_t ls_op_addr,
                  size_t meminsz, void* pin, size_t memoutsz,
                  const void* pout)
{
    int res;
    uint32_t addr = (ls_op_addr >> 16) & 0x7f;
    if (addr == 0)
        return -EINVAL;

    res = usdr_device_vfs_obj_val_set_by_path(dev->pdev, "/ll/i2c/0/addr_ext", addr);
    if (res)
        return res;

    res = lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                       USDR_LSOP_I2C_DEV, 3,
                                       meminsz, pin, memoutsz, pout);

    return res;
}


int _debug_ll_mdev_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    int res;
    dev_fe_t* o = (dev_fe_t* )((usdr_vfs_obj_base_t*)obj)->param;
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


int device_fe_probe(device_t* base, const char* compat, const char* fename, dev_fe_t** out)
{
    lldev_t dev = base->dev;
    unsigned i;
    int res;
    dev_fe_t dfe;
    unsigned vfidx = 0;
    const char* hint = fename;

    if (hint == NULL) {
        hint = getenv("USDR_FE_TYPE");
        if (hint) {
            USDR_LOG("DEFE", USDR_LOG_WARNING, "Detected USDR_FE_TYPE variable `%s`", hint);
        }
    }

    // TODO obtain gpiobase
    const unsigned gpiobase = 3;
    memset(&dfe, 0, sizeof(dfe));

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
        case FET_PICE_BREAKOUT: res = board_exm2pe_init(dev, 0, gpiobase, hint_strip, compat, &_usdr_ext_i2c, &dfe.fe.exm2pe); break;
        case FET_PCIE_DEVBOARD: res = board_ext_pciefe_init(dev, 0, gpiobase, hint_strip, compat, &_usdr_ext_i2c, &dfe.fe.devboard); break;
        case FET_PCIE_SUPER_SYNC: res = board_ext_supersync_init(dev, 0, gpiobase, &_usdr_ext_i2c, &dfe.fe.supersync); break;
        case FET_PCIE_SIMPLE_SYNC: res = board_ext_simplesync_init(dev, 0, gpiobase, compat, &_usdr_ext_i2c, &dfe.fe.simplesync); break;
        case FET_PCIE_FE1005000: res = ext_fe_100_5000_init(dev, 0, gpiobase, 58, 4, hint_strip, compat, &dfe.fe.fe_100_5000); break;
        default: return -EIO;
        }

        if (res == 0) {
            break;
        } else if (res != -ENODEV) {
            USDR_LOG("DEFE", USDR_LOG_ERROR, "Unable to initialize %s, error %d\n", s_fe_names[i], res);
            if (hint != NULL)
                return res;
        }
    }

    if (i == FET_COUNT) {
        if (hint == NULL) {
            USDR_LOG("DEFE", USDR_LOG_NOTE, "No external FE was detected, provide fe=`frontend` for a strong hint\n");
            *out = NULL;
            return 0;
        }

        USDR_LOG("DEFE", USDR_LOG_WARNING, "No external FE was detected with `%s` hint and %s filter\n", hint, compat);
        return -ENODEV;
    }

    dev_fe_t* n = (dev_fe_t*)malloc(sizeof(dev_fe_t));
    *n = dfe;
    n->type = (fe_type_t)i;

    USDR_LOG("DEFE", USDR_LOG_WARNING, "Detected external FE: %s\n", s_fe_names[i]);
    res = usdr_vfs_obj_param_init_array_param(base,
                                              base->impl->objcount,
                                              &n->vfs_cfg_obj[vfidx],
                                              (void*)n,
                                              s_fe_params,
                                              SIZEOF_ARRAY(s_fe_params));
    if (res)
        return res;

    vfidx += SIZEOF_ARRAY(s_fe_params);

    switch (n->type) {
    case FET_PCIE_DEVBOARD:
        res = usdr_vfs_obj_param_init_array_param(base,
                                                  base->impl->objcount,
                                                  &n->vfs_cfg_obj[vfidx],
                                                  (void*)n,
                                                  s_fe_pcie_params,
                                                  SIZEOF_ARRAY(s_fe_pcie_params));
        break;
    case FET_PCIE_SIMPLE_SYNC:
        res = usdr_vfs_obj_param_init_array_param(base,
                                                  base->impl->objcount,
                                                  &n->vfs_cfg_obj[vfidx],
                                                  (void*)n,
                                                  s_lmk05318_params,
                                                  SIZEOF_ARRAY(s_lmk05318_params));
        break;
    case FET_PCIE_SUPER_SYNC:
        res = usdr_vfs_obj_param_init_array_param(base,
                                                  base->impl->objcount,
                                                  &n->vfs_cfg_obj[vfidx],
                                                  (void*)n,
                                                  s_lmk5c33216_params,
                                                  SIZEOF_ARRAY(s_lmk5c33216_params));
        break;
    case FET_PCIE_FE1005000:
        res = usdr_vfs_obj_param_init_array_param(base,
                                                  base->impl->objcount,
                                                  &n->vfs_cfg_obj[vfidx],
                                                  (void*)n,
                                                  s_ext_fe_100_5000_params,
                                                  SIZEOF_ARRAY(s_ext_fe_100_5000_params));
        break;
    default:
        break;
    }

    *out = n;
    return res;
}

void* device_fe_to(struct dev_fe* obj, const char* type)
{
    if (strcmp(s_fe_names[obj->type], type) != 0) {
        return NULL;
    }

    return (void*)&obj->fe;
}

int _debug_typefe_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dev_fe_t* o = (dev_fe_t* )((usdr_vfs_obj_base_t*)obj)->param;
    *ovalue = (uintptr_t)s_fe_names[o->type];
    return 0;
}

int device_fe_destroy(struct dev_fe* obj)
{
    //TODO deinit

    free(obj);
    return 0;
}


int _debug_pciefe_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dev_fe_t* o = (dev_fe_t* )((usdr_vfs_obj_base_t*)obj)->param;
    *ovalue = o->debug_pciefe_last;
    return 0;
}


int _debug_pciefe_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_fe_t* o = (dev_fe_t* )((usdr_vfs_obj_base_t*)obj)->param;
    int res;
    unsigned addr = (value >> 16) & 0x7f;
    unsigned data = value & 0xffff;

    o->debug_pciefe_last = ~0u;

    if (value & 0x800000) {
        res = board_ext_pciefe_ereg_wr(&o->fe.devboard, addr, data);
        USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug PCIEFE WR REG %04x => %04x\n",
                 lowlevel_get_devname(ud->dev), (unsigned)addr, data);
    } else {
        res = board_ext_pciefe_ereg_rd(&o->fe.devboard, addr, &o->debug_pciefe_last);
        USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug PCIEFE RD REG %04x <= %04x\n",
                 lowlevel_get_devname(ud->dev), (unsigned)addr,
                 o->debug_pciefe_last);
    }

    return res;
}

int _debug_ext_fe_100_5000_cmd_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dev_fe_t* o = (dev_fe_t* )((usdr_vfs_obj_base_t*)obj)->param;
    *ovalue = o->debug_ext_fe_100_5000_cmd_last;
    return 0;
}

int _debug_ext_fe_100_5000_cmd_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_fe_t* o = (dev_fe_t* )((usdr_vfs_obj_base_t*)obj)->param;
    int res;
    unsigned addr = (value >> 24) & 0x7f;
    unsigned data = value & 0xffffff;

    o->debug_ext_fe_100_5000_cmd_last = ~0u;

    if (value & 0x80000000) {
        res = ext_fe_100_5000_cmd_wr(&o->fe.fe_100_5000, addr, data);
        USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug FE_100_5000 WR CMD %04x => %06x\n",
                 lowlevel_get_devname(ud->dev), (unsigned)addr, data);
    } else {
        res = ext_fe_100_5000_cmd_rd(&o->fe.fe_100_5000, addr, &o->debug_ext_fe_100_5000_cmd_last);
        USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug FE_100_5000 RD CMD %04x <= %06x\n",
                 lowlevel_get_devname(ud->dev), (unsigned)addr,
                 o->debug_ext_fe_100_5000_cmd_last);
    }
    return res;
}

int _debug_pciefe_cmd_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dev_fe_t* o = (dev_fe_t* )((usdr_vfs_obj_base_t*)obj)->param;
    *ovalue = o->debug_pciefe_cmd_last;
    return 0;
}


int _debug_pciefe_cmd_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_fe_t* o = (dev_fe_t* )((usdr_vfs_obj_base_t*)obj)->param;
    int res;
    unsigned addr = (value >> 16) & 0x7f;
    unsigned data = value & 0xffff;

    o->debug_pciefe_cmd_last = ~0u;

    if (value & 0x800000) {
        res = board_ext_pciefe_cmd_wr(&o->fe.devboard, addr, data);

        USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug PCIEFE WR CMD %04x => %04x\n",
                 lowlevel_get_devname(ud->dev), (unsigned)addr, data);
    } else {
        res = board_ext_pciefe_cmd_rd(&o->fe.devboard, addr, &o->debug_pciefe_cmd_last);

        USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: Debug PCIEFE RD CMD %04x <= %04x\n",
                 lowlevel_get_devname(ud->dev), (unsigned)addr,
                 o->debug_pciefe_cmd_last);
    }

    return res;
}


int _debug_lmk05318_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dev_fe_t* o = (dev_fe_t* )((usdr_vfs_obj_base_t*)obj)->param;
    *ovalue = o->debug_pciefe_last;
    return 0;
}


int _debug_lmk05318_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_fe_t* o = (dev_fe_t* )((usdr_vfs_obj_base_t*)obj)->param;
    int res;
    unsigned addr = (value >> 8) & 0x7fff;
    unsigned data = value & 0xff;
    uint8_t d;

    o->debug_pciefe_last = ~0u;

    if (value & 0x800000) {
        //res = lmk05318_reg_wr(&_usdr_ext_i2c, ud->dev, 0, 0x65 << 16, addr, data);
        res = lmk05318_reg_wr(&o->fe.simplesync.lmk, addr, data);

        USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: LMK05318 WR REG %04x => %04x\n",
                 lowlevel_get_devname(ud->dev), (unsigned)addr, data);
    } else {
        d = 0xff;
        //res = lmk_05318b_reg_rd(&_usdr_ext_i2c, ud->dev, 0, 0x65 << 16, addr, &d);
        res = lmk05318_reg_rd(&o->fe.simplesync.lmk, addr, &d);
        o->debug_pciefe_last = d;

        USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: LMK05318 RD REG %04x <= %04x\n",
                 lowlevel_get_devname(ud->dev), (unsigned)addr,
                 o->debug_pciefe_last);
    }

    return res;
}

int _debug_lmk05318_calfreq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_fe_t* o = (dev_fe_t* )((usdr_vfs_obj_base_t*)obj)->param;

    return simplesync_tune_lo(&o->fe.simplesync, value);
}


int _debug_lmk5c33216_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    dev_fe_t* o = (dev_fe_t* )((usdr_vfs_obj_base_t*)obj)->param;
    *ovalue = o->debug_pciefe_last;
    return 0;
}


int _debug_lmk5c33216_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    dev_fe_t* o = (dev_fe_t* )((usdr_vfs_obj_base_t*)obj)->param;
    int res;
    unsigned addr = (value >> 8) & 0x7fff;
    unsigned data = value & 0xff;
    uint8_t d;

    o->debug_pciefe_last = ~0u;

    if (value & 0x800000) {
        res = lmk_5c33216_reg_wr(&o->fe.supersync.lmk, addr, data);

        USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: LMK5C33216 WR REG %04x => %04x\n",
                 lowlevel_get_devname(ud->dev), (unsigned)addr, data);
    } else {
        d = 0xff;
        res = lmk_5c33216_reg_rd(&o->fe.supersync.lmk, addr, &d);
        o->debug_pciefe_last = d;

        USDR_LOG("XDEV", USDR_LOG_WARNING, "%s: 5C33216 RD REG %04x <= %04x\n",
                 lowlevel_get_devname(ud->dev), (unsigned)addr,
                 o->debug_pciefe_last);
    }

    return res;
}

