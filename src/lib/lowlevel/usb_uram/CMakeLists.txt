# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

if(NOT LIBUSB_1_FOUND)
        if(ENABLE_USB)
                message(WARNING "No USB support, install libusb to enable it")
                set(ENABLE_USB OFF)
        endif(ENABLE_USB)
else(NOT LIBUSB_1_FOUND)
        list(APPEND USDR_INCLUDE_FILES ${LIBUSB_1_INCLUDE_DIRS})
        list(APPEND USDR_LINK_FILES ${LIBUSB_1_LIBRARIES})

        set(USDR_USB_URAM_LIB_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/usb_uram_generic.c
            ${CMAKE_CURRENT_SOURCE_DIR}/usb_uram_libusb.c
        )

        list(APPEND USDR_LIBRARY_FILES ${USDR_USB_URAM_LIB_FILES})
        set(USDR_LIBRARY_FILES ${USDR_LIBRARY_FILES} PARENT_SCOPE)
        set(USDR_LINK_FILES ${USDR_LINK_FILES} PARENT_SCOPE)
        set(USDR_INCLUDE_FILES ${USDR_INCLUDE_FILES} PARENT_SCOPE)
endif(NOT LIBUSB_1_FOUND)



