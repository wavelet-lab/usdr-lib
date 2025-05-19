// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "device_bus.h"
#include "device_names.h"
#include "device_vfs.h"
#include "device_cores.h"
#include <stdio.h>

#include <usdr_logging.h>

int device_bus_init(pdevice_t dev, struct device_bus* pdb)
{
    uint64_t v;
    int res;
    unsigned i, j;
    char buffer[64];

    struct enum_params {
        const char* count_path;
        unsigned max_count;
        unsigned* storage_count;
        const char* base_path_pat;
        unsigned* base_array;
    } params[] = {
        { DNLL_IDX_REGSP_COUNT, SIZEOF_ARRAY(pdb->idxreg_base), &pdb->idx_regsps, DNLLFP_BASE(DN_IDX_REGSP, "%d"), pdb->idxreg_base },

        { DNLL_SPI_COUNT, SIZEOF_ARRAY(pdb->spi_base), &pdb->spi_count, DNLLFP_BASE(DN_BUS_SPI, "%d"), pdb->spi_base },
        { DNLL_I2C_COUNT, SIZEOF_ARRAY(pdb->i2c_base), &pdb->i2c_count, DNLLFP_BASE(DN_BUS_I2C, "%d"), pdb->i2c_base },

        { DNLL_SPI_COUNT, SIZEOF_ARRAY(pdb->spi_core), &pdb->spi_count, DNLLFP_CORE(DN_BUS_SPI, "%d"), pdb->spi_core },
        { DNLL_I2C_COUNT, SIZEOF_ARRAY(pdb->i2c_core), &pdb->i2c_count, DNLLFP_CORE(DN_BUS_I2C, "%d"), pdb->i2c_core },

        { DNLL_SRX_COUNT, SIZEOF_ARRAY(pdb->srx_base), &pdb->srx_count, DNLLFP_BASE(DN_SRX, "%d"), pdb->srx_base },
        { DNLL_STX_COUNT, SIZEOF_ARRAY(pdb->stx_base), &pdb->stx_count, DNLLFP_BASE(DN_STX, "%d"), pdb->stx_base },

        { DNLL_SRX_COUNT, SIZEOF_ARRAY(pdb->srx_cfg_base), &pdb->srx_count, DNLLFP_CFG_BASE(DN_SRX, "%d"), pdb->srx_cfg_base },
        { DNLL_STX_COUNT, SIZEOF_ARRAY(pdb->stx_cfg_base), &pdb->stx_count, DNLLFP_CFG_BASE(DN_STX, "%d"), pdb->stx_cfg_base },

        { DNLL_SRX_COUNT, SIZEOF_ARRAY(pdb->srx_core), &pdb->srx_count, DNLLFP_CORE(DN_SRX, "%d"), pdb->srx_core },
        { DNLL_STX_COUNT, SIZEOF_ARRAY(pdb->stx_core), &pdb->stx_count, DNLLFP_CORE(DN_STX, "%d"), pdb->stx_core },

        { DNLL_SRX_COUNT, SIZEOF_ARRAY(pdb->rfe_base), &pdb->rfe_count, DNLLFP_BASE(DN_RFE, "%d"), pdb->rfe_base },
        { DNLL_TFE_COUNT, SIZEOF_ARRAY(pdb->tfe_base), &pdb->tfe_count, DNLLFP_BASE(DN_TFE, "%d"), pdb->tfe_base },

        { DNLL_RFE_COUNT, SIZEOF_ARRAY(pdb->rfe_core), &pdb->rfe_count, DNLLFP_CORE(DN_RFE, "%d"), pdb->rfe_core },
        { DNLL_TFE_COUNT, SIZEOF_ARRAY(pdb->tfe_core), &pdb->tfe_count, DNLLFP_CORE(DN_TFE, "%d"), pdb->tfe_core },

        { DNLL_DRP_COUNT, SIZEOF_ARRAY(pdb->drp_core), &pdb->drp_count, DNLLFP_CORE(DN_DRP, "%d"), pdb->drp_core },
        { DNLL_DRP_COUNT, SIZEOF_ARRAY(pdb->drp_base), &pdb->drp_count, DNLLFP_BASE(DN_DRP, "%d"), pdb->drp_base },

        { DNLL_BUCKET_COUNT, SIZEOF_ARRAY(pdb->bucket_core), &pdb->bucket_count, DNLLFP_CORE(DN_BUCKET, "%d"), pdb->bucket_core },
        { DNLL_BUCKET_COUNT, SIZEOF_ARRAY(pdb->bucket_base), &pdb->bucket_count, DNLLFP_BASE(DN_BUCKET, "%d"), pdb->bucket_base },

        { DNLL_GPI_COUNT, SIZEOF_ARRAY(pdb->gpi_core), &pdb->gpi_count, DNLLFP_CORE(DN_GPI, "%d"), pdb->gpi_core },
        { DNLL_GPI_COUNT, SIZEOF_ARRAY(pdb->gpi_base), &pdb->gpi_count, DNLLFP_BASE(DN_GPI, "%d"), pdb->gpi_base },

        { DNLL_GPO_COUNT, SIZEOF_ARRAY(pdb->gpo_core), &pdb->gpo_count, DNLLFP_CORE(DN_GPO, "%d"), pdb->gpo_core },
        { DNLL_GPO_COUNT, SIZEOF_ARRAY(pdb->gpo_base), &pdb->gpo_count, DNLLFP_BASE(DN_GPO, "%d"), pdb->gpo_base },
    };

    struct single_params {
        const char* path;
        unsigned* storage;
        unsigned not_found_val;
    } sparams[] = {
        { "/ll/poll_event/in", (unsigned*)&pdb->poll_event_rd, ~0u},
        { "/ll/poll_event/out", (unsigned*)&pdb->poll_event_wr, ~0u},
    };

    for (i = 0; i < SIZEOF_ARRAY(sparams); i++) {
        res = usdr_device_vfs_obj_val_get_u64(dev, sparams[i].path, &v);
        if (res) {
            if (res == -ENOENT)
                v = sparams[i].not_found_val;
            else
                return res;
        }

        *sparams[i].storage = (unsigned)v;
    }

    for (i = 0; i < SIZEOF_ARRAY(params); i++) {
        res = usdr_device_vfs_obj_val_get_u64(dev, params[i].count_path, &v);
        if (res) {
            if (res == -ENOENT)
                v = 0;
            else
                return res;
        }

        *params[i].storage_count = (unsigned)v;
        if (v > params[i].max_count)
            return -EINVAL;

        for (j = 0; j < *params[i].storage_count; j++) {
            snprintf(buffer, sizeof(buffer), params[i].base_path_pat, j);
            res = usdr_device_vfs_obj_val_get_u64(dev, buffer, &v);
            if (res) {
                return res;
            }

            params[i].base_array[j] = (unsigned)v;
        }
    }

    // Initialize indexed registers
    for (j = 0; j < *params[0].storage_count; j++) {
        snprintf(buffer, sizeof(buffer), DNLLFP_NAME(DN_IDX_REGSP, "%d", DNP_VIRT_BASE), j);
        res = usdr_device_vfs_obj_val_get_u64(dev, buffer, &v);
        if (res) {
            return res;
        }

        pdb->idxreg_virt_base[j] = (unsigned)v;
    }

    return 0;
}


int device_bus_drp_generic_op(lldev_t dev, subdev_t subdev, const device_bus_t* db,
                              lsopaddr_t ls_op_addr,
                              size_t meminsz, void* pin,
                              size_t memoutsz, const void* pout)
{
    int res;
    unsigned bus = ls_op_addr >> 16;
    unsigned cmd;
    if (bus > db->drp_count) {
        return -EINVAL;
    }

    if (meminsz == 2 && memoutsz == 0) {
        cmd = 0x01000000 | ((ls_op_addr & 0xff) << 16);
    } else if (meminsz == 0 && memoutsz == 2) {
        uint16_t data = *(uint16_t*)pout;
        cmd = 0x81000000 | ((ls_op_addr & 0xff) << 16) | data;
    } else {
        return -EINVAL;
    }

    res = lowlevel_reg_wr32(dev, subdev, db->drp_base[bus], cmd);
    if (res)
        return res;

    if (meminsz) {
        usleep(1000);
        uint32_t data;
        res = lowlevel_reg_rd32(dev, subdev, db->drp_base[bus], &data);
        if (res)
            return res;

        *((uint16_t*)pin) = data;
    }

    return 0;
}

int device_bus_gpi_generic_op(lldev_t dev, subdev_t subdev, const device_bus_t* db,
                              lsopaddr_t ls_op_addr,
                              size_t meminsz, void* pin,
                              size_t memoutsz, const void* pout)
{
    unsigned gpidx = ls_op_addr >> 24;
    unsigned bank = ls_op_addr & 0xffffff;
    if (gpidx >= db->gpi_count)
        return -EINVAL;

    if (db->gpi_core[gpidx] != USDR_MAKE_COREID(USDR_CS_GPI, USDR_GPI_32BIT_12))
        return -EINVAL;

    if (memoutsz != 0 || meminsz != 4)
        return -EINVAL;

    if (bank >= 12 * 4)
        return -EFAULT;

    return lowlevel_reg_rd32(dev, subdev, db->gpi_base[gpidx] + (bank / 4), (uint32_t*)pin);
}

int device_bus_gpo_generic_op(lldev_t dev, subdev_t subdev, const device_bus_t* db,
                              lsopaddr_t ls_op_addr,
                              size_t meminsz, void* pin,
                              size_t memoutsz, const void* pout)
{
    unsigned gpodx = ls_op_addr >> 24;
    unsigned bank = ls_op_addr & 0xffffff;
    if (gpodx >= db->gpo_count)
        return -EINVAL;
    if (db->gpo_core[gpodx] != USDR_MAKE_COREID(USDR_CS_GPO, USDR_GPO_8BIT))
        return -EINVAL;
    if (bank > 127)
        return -EINVAL;
    if (memoutsz != 1 || meminsz != 0)
        return -EINVAL;

    uint8_t data = *(uint8_t*)pout;
    USDR_LOG("DGPO", USDR_LOG_DEBUG, "GPO[%d] <= %02x\n", bank, data);
    return lowlevel_reg_wr32(dev, subdev,  db->gpo_base[gpodx], ((bank & 0x7f) << 24) | (data & 0xff));
}
