# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

set(BOARD_EXM2PE_LIB_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/ext_pciefe.c
)

set(HW_FILES pciefe pciefe_cmd)
foreach(I ${HW_FILES})
    message(STATUS "Generating header for ${I}")
    GENERATE_YAML_H(${CMAKE_CURRENT_SOURCE_DIR}/${I}.yaml ${CMAKE_CURRENT_BINARY_DIR}/def_${I}.h)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/${I}.yaml DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/usdr/schema/)

    list(APPEND USDR_DEPEND_TARGETS generate_${I})
endforeach()

list(APPEND USDR_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR})
list(APPEND USDR_LIBRARY_FILES ${BOARD_EXM2PE_LIB_FILES})

set(USDR_LIBRARY_FILES ${USDR_LIBRARY_FILES} PARENT_SCOPE)
set(USDR_DEPEND_TARGETS ${USDR_DEPEND_TARGETS} PARENT_SCOPE)
set(USDR_INCLUDE_DIRS ${USDR_INCLUDE_DIRS} PARENT_SCOPE)
