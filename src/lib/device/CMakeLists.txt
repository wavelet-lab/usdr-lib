# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

set(USDR_DEVICE_LIB_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/device_bus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/device_vfs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mdev.c
    ${CMAKE_CURRENT_SOURCE_DIR}/device_fe.c
)

add_subdirectory(m2_lm6_1)
add_subdirectory(m2_lm7_1)
add_subdirectory(m2_lsdr)
add_subdirectory(m2_dsdr)


add_subdirectory(ext_exm2pe)
add_subdirectory(ext_pciefe)
add_subdirectory(ext_supersync)
add_subdirectory(ext_simplesync)
add_subdirectory(ext_fe_100_5000)
add_subdirectory(ext_fe_ch4_400_7200)
add_subdirectory(ext_xmass)


add_subdirectory(u3_limesdr)
add_subdirectory(pe_sync)


list(APPEND USDR_LIBRARY_FILES ${USDR_DEVICE_LIB_FILES})
set(USDR_LIBRARY_FILES ${USDR_LIBRARY_FILES} PARENT_SCOPE)

#list(APPEND USDR_INCLUDE_DIRS  ${CMAKE_CURRENT_BINARY_DIR})
#list(APPEND USDR_DEPEND_TARGETS ${USDR_DEPEND_TARGETS})

set(USDR_INCLUDE_DIRS ${USDR_INCLUDE_DIRS} PARENT_SCOPE)
set(USDR_DEPEND_TARGETS ${USDR_DEPEND_TARGETS} PARENT_SCOPE)
set(USDR_LINK_FILES ${USDR_LINK_FILES} PARENT_SCOPE)

