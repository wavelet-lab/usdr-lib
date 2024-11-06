#include "usb_ft601_generic.h"

usb_ft601_generic_t* get_ft601_generic(lldev_t dev);

int _ft601_cmd(lldev_t lld, uint8_t ep, uint8_t cmd, uint32_t param)
{
    uint8_t wbuffer[20];
    int res, actual;
    usb_ft601_generic_t* gen = get_ft601_generic(lld);

    fill_ft601_cmd(wbuffer, ++gen->ft601_counter, ep, 0x00, 0);
    res = gen->io_ops.io_write_fn(lld, wbuffer, 20, &actual, 1000);
    if(res || actual != 20)
        return -EIO;

    fill_ft601_cmd(wbuffer, ++gen->ft601_counter, ep, cmd, param);
    res = gen->io_ops.io_write_fn(lld, wbuffer, 20, &actual, 1000);
    if(res || actual != 20)
        return -EIO;

    return 0;
}

int usbft601_ctrl_transfer(lldev_t lld, uint8_t cmd, const uint8_t* data_in, unsigned length_in,
                           uint8_t* data_out, unsigned length_out, unsigned timeout_ms)
{
    int cnt, res;
    unsigned pkt_szb;
    // Compose packet
    if (length_in == 0)
        length_in = 1;

    usb_ft601_generic_t* gen = get_ft601_generic(lld);

    cnt = lms64c_fill_packet(cmd, 0, 0, data_in, length_in, gen->data_ctrl_out, MAX_CTRL_BURST);
    if (cnt < 0) {
        USDR_LOG(USBG_LOG_TAG, USDR_LOG_ERROR, "Too long command %d bytes; might be an error!\n", length_in);
        return -EINVAL;
    }
    pkt_szb = cnt * LMS64C_PKT_LENGTH;

    res = gen->io_ops.io_ctrl_out_pkt_fn(lld, pkt_szb, timeout_ms);
    if (res) {
        return res;
    }

    res = gen->io_ops.io_ctrl_in_pkt_fn(lld, pkt_szb, timeout_ms);
    if (res) {
        goto failed;
    }

    res = gen->io_ops.io_lock_wait(lld, timeout_ms * 1000 * 1000);
    if (res) {
        goto failed;
    }

    // Parse result
    if (length_out) {
        res = lms64c_parse_packet(cmd, gen->data_ctrl_in, cnt, data_out, length_out);
    }
    if(res) {
        goto failed;
    }

    // Return status of transfer
    switch (gen->data_ctrl_in[0].status) {
    case STATUS_COMPLETED_CMD: res = 0; break;
    case STATUS_UNKNOWN_CMD: res = -ENOENT; break;
    case STATUS_BUSY_CMD: res = -EBUSY; break;
    default: res = -EFAULT; break;
    }

failed:
    gen->io_ops.io_lock_post(lld);
    return res;
}

int usbft601_uram_ls_op(lldev_t dev, subdev_t subdev,
                        unsigned ls_op, lsopaddr_t ls_op_addr,
                        size_t meminsz, void* pin,
                        size_t memoutsz, const void* pout)
{
    int res = 0;
    unsigned timeout_ms = 3000;
    uint16_t tmpbuf_out[2048];

    switch (ls_op) {
    case USDR_LSOP_HWREG:
        if (pout == NULL || memoutsz == 0) {
            if (meminsz > sizeof(tmpbuf_out))
                return -E2BIG;

            // Register read
            for (unsigned i = 0; i < meminsz / 2; i++) {
                tmpbuf_out[i] = ls_op_addr + i;
            }

            res = usbft601_ctrl_transfer(dev, CMD_BRDSPI_RD, (const uint8_t* )tmpbuf_out, meminsz, (uint8_t* )pin, meminsz, timeout_ms);
        } else if (pin == NULL || meminsz == 0) {
            if (memoutsz * 2 > sizeof(tmpbuf_out))
                return -E2BIG;

            // Rgister write
            const uint16_t* out_s = (const uint16_t* )pout;
            for (unsigned i = 0; i < memoutsz / 2; i++) {
                tmpbuf_out[2 * i + 1] = ls_op_addr + i;
                tmpbuf_out[2 * i + 0] = out_s[i];
            }

            res = usbft601_ctrl_transfer(dev, CMD_BRDSPI_WR, (const uint8_t* )tmpbuf_out, memoutsz * 2, (uint8_t* )pin, meminsz, timeout_ms);
        } else {
            return -EINVAL;
        }
        break;

    case USDR_LSOP_SPI:
        // TODO split to RD / WR packets
        if (pin == NULL || meminsz == 0 || ((*(const uint32_t*)pout) & 0x80000000) != 0) {
            res = usbft601_ctrl_transfer(dev, CMD_LMS7002_WR, (const uint8_t* )pout, memoutsz, (uint8_t* )pin, meminsz, timeout_ms);
        } else {
            for (unsigned k = 0; k < memoutsz / 4; k++) {
                const uint32_t* pz = (const uint32_t*)((uint8_t*)pout + 4 * k);
                tmpbuf_out[k] = *pz >> 16;
            }
            res = usbft601_ctrl_transfer(dev, CMD_LMS7002_RD, (const uint8_t* )tmpbuf_out /*pout*/, memoutsz / 2, (uint8_t* )pin, meminsz, timeout_ms);
        }
        break;

    case USDR_LSOP_I2C_DEV:
        break;

    case USDR_LSOP_CUSTOM_CMD: {
        uint8_t cmd = LMS_RST_PULSE;
        res = usbft601_ctrl_transfer(dev, CMD_LMS7002_RST, (const uint8_t* )&cmd, 1, NULL, 0, timeout_ms);
        break;
    }
    default:
        return -EOPNOTSUPP;
    }

    return res;
}

int usbft601_uram_get_info(lldev_t lld, ft601_device_info_t* info)
{
    uint8_t tmpdata[32];
    int res;

    res = usbft601_ctrl_transfer(lld, CMD_GET_INFO, NULL, 0, tmpdata, sizeof(tmpdata), 3000);
    if (res)
        return res;

    info->firmware = tmpdata[0];
    info->device = tmpdata[1];
    info->protocol = tmpdata[2];
    info->hardware = tmpdata[3];
    info->expansion = tmpdata[4];
    info->boardSerialNumber = 0;

    return 0;
}

int usbft601_uram_generic_create_and_init(lldev_t lld, unsigned pcount, const char** devparam,
                                          const char** devval, device_id_t* pdevid)
{
    int res = 0;
    usb_ft601_generic_t* gen = get_ft601_generic(lld);
    device_bus_t* pdb = &gen->db;
    const char* devname = lowlevel_get_devname(lld);

    // FT601 initialization
    res = res ? res : ft601_flush_pipe(lld, EP_IN_CTRL);  //clear ctrl ep rx buffer
    res = res ? res : ft601_set_stream_pipe(lld, EP_IN_CTRL, CTRL_SIZE);
    res = res ? res : ft601_set_stream_pipe(lld, EP_OUT_CTRL, CTRL_SIZE);
    if (res) {
        return res;
    }

    res = gen->io_ops.io_async_start_fn(lld);
    if (res) {
        return res;
    }

    ft601_device_info_t nfo;
    res = usbft601_uram_get_info(lld, &nfo);
    if (res) {
        return res;
    }

    USDR_LOG(USBG_LOG_TAG, USDR_LOG_INFO, "Firmware: %d, Dev: %d, Proto: %d, HW: %d, Serial: %lld\n",
             nfo.firmware, nfo.device, nfo.protocol, nfo.hardware,
             (long long int)nfo.boardSerialNumber);

    if (nfo.device != LMS_DEV_LIMESDRMINI_V2) {
        USDR_LOG(USBG_LOG_TAG, USDR_LOG_INFO, "Unsupported device!\n");
        return -ENOTSUP;
    }

    // Device initialization
    res = usdr_device_create(lld, *pdevid);
    if (res) {
        USDR_LOG(USBG_LOG_TAG, USDR_LOG_ERROR,
                 "Unable to find device spcec for %s, uuid %s! Update software!\n",
                 devname, usdr_device_id_to_str(*pdevid));

        return res;
    }

    res = device_bus_init(lld->pdev, pdb);
    if (res) {
        USDR_LOG(USBG_LOG_TAG, USDR_LOG_ERROR,
                 "Unable to initialize bus parameters for the device %s!\n", devname);

        return res;
    }

    // Register operations are now available

    res = lld->pdev->initialize(lld->pdev, pcount, devparam, devval);
    if (res) {
        USDR_LOG(USBG_LOG_TAG, USDR_LOG_ERROR,
                 "Unable to initialize device, error %d\n", res);
        return res;
    }

    return 0;
}
