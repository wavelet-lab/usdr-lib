// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

//
// Created by irod on 29.01.24.
//

#ifndef _LIBUSB_VIDPID_MAP_H
#define _LIBUSB_VIDPID_MAP_H

#include <assert.h>
#include <string.h>
#include "../device/device_ids.h"

#define DEVICE_BUS_NAME_LEN 12

struct usb_pidvid_map {
    uint16_t pid;
    uint16_t vid;
    char deviceBusName[DEVICE_BUS_NAME_LEN];
    sdr_type_t sdrType;

    device_id_t deviceid;
};
typedef struct usb_pidvid_map usb_pidvid_map_t;

static const usb_pidvid_map_t s_known_devices[] = {
/* XSDR */          { 0xdea9, 0xfaef,  "usb",      SDR_XSDR, M2_LM7_1_DEVICE_ID     },
/* USDR */          { 0xdea9, 0xfaee,  "usb",      SDR_USDR, M2_LM6_1_DEVICE_ID     },
/* LimeSDR-mini */  { 0x601f, 0x0403,  "usbft601", SDR_LIME, U3_LIMESDR_DEVICE_ID   },
/* LimeSDR-mini */  { 0x601e, 0x0403,  "usbft601", SDR_LIME, U3_LIMESDR_DEVICE_ID   },
/* XSDR new vidpid*/{ 0x1011, 0x3727,  "usb",      SDR_XSDR, M2_LM7_1_DEVICE_ID     },
/* USDR new vidpid*/{ 0x1001, 0x3727,  "usb",      SDR_USDR, M2_LM6_1_DEVICE_ID     },
/* SYNC new vidpid*/{ 0xE001, 0x3727,  "usb",      SDR_NONE, PE_SYNC_DEVICE_ID      },
};

#define KNOWN_USB_DEVICES (sizeof(s_known_devices)/sizeof(s_known_devices[0]))

static inline int libusb_find_dev_index_ex( const char* deviceBusName, uint16_t pid, uint16_t vid,
                                  const usb_pidvid_map_t* map, int map_size ) {
    for (int i = 0; i < map_size; ++i) {
        if ( (!deviceBusName || !strncmp(map[i].deviceBusName, deviceBusName, DEVICE_BUS_NAME_LEN)) &&
                map[i].pid == pid && map[i].vid == vid) {
            return i;
        }
    }
    return -ENOTSUP;
}

static inline int libusb_find_dev_index( unsigned vidpid ) {
    const uint16_t pid = 0x00000000FFFFFFFF & vidpid;
    const uint16_t vid = 0x00000000FFFFFFFF & (vidpid >> 16);
    return libusb_find_dev_index_ex( NULL, pid, vid, s_known_devices, KNOWN_USB_DEVICES);
}

static inline device_id_t libusb_get_deviceid(int idx) {
    assert( idx >= 0 && idx < KNOWN_USB_DEVICES );
    return s_known_devices[idx].deviceid;
}

static inline sdr_type_t libusb_get_dev_sdrtype(int idx) {
    assert( idx >= 0 && idx < KNOWN_USB_DEVICES );
    return s_known_devices[idx].sdrType;
}

#endif //_LIBUSB_VIDPID_MAP_H
