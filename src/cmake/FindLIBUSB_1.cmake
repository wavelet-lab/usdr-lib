# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

find_library(LIBUSB_1_LIBRARIES
   NAMES libusb libusb-1.0 usb-1.0
   PATHS "/usr/lib" "/usr/local/lib/")

find_path(LIBUSB_1_INCLUDE_DIRS
   NAMES libusb.h libusb-1.0.h
   PATHS "/usr/local/include/" "$ENV{LIBUSB_PATH}/include/libusb-1.0"
   PATH_SUFFIXES "include" "libusb" "libusb-1.0")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBUSB_1 DEFAULT_MSG
                                  LIBUSB_1_LIBRARIES
                                  LIBUSB_1_INCLUDE_DIRS)
