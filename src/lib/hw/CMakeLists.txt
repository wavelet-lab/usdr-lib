# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

set(HW_FILES common si549 si5332 ads42lbx9 tps6594 tmp108 tmp114 lms6002d lms7002m lms8001 lp8758 lmk05318 lmk5c33216 tps6381x dac80501
             lmk04832 tca6424a adf4002b lp875484 afe79xx lmx1204 lmx1214 lmk1d1208i ad5662 lmx1205 lp87524j lmx2820)
# YAML registers generators
set(HW_FILES_YAML si5332 lmk05318 lmk5c33216 lms6002d lms7002m lms8001 tps6381x dac80501
                  lmk04832 xra1405 tca6424a adf4002b lp875484 lmx1204 lmx1214 lmk1d1208i ad5662 lmx1205 lp87524j lmx2820)

foreach(I ${HW_FILES})
    aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/${I} HW_DIR_FILES)
    list(APPEND USDR_HW_LIB_FILES ${HW_DIR_FILES})
endforeach()

foreach(I ${HW_FILES_YAML})
    message(STATUS "Generating header for ${I}")
    GENERATE_YAML_H(${CMAKE_CURRENT_SOURCE_DIR}/${I}/${I}.yaml ${CMAKE_CURRENT_BINARY_DIR}/def_${I}.h)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/${I}/${I}.yaml DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/usdr/schema/)

    list(APPEND USDR_DEPEND_TARGETS generate_${I})
endforeach()

#Extra yaml files
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/si549/si549.yaml DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/usdr/schema/)

list(APPEND USDR_LIBRARY_FILES ${USDR_HW_LIB_FILES})
list(APPEND USDR_INCLUDE_FILES ${CMAKE_CURRENT_BINARY_DIR})

set(USDR_LIBRARY_FILES ${USDR_LIBRARY_FILES} PARENT_SCOPE)
set(USDR_DEPEND_TARGETS ${USDR_DEPEND_TARGETS} PARENT_SCOPE)
set(USDR_INCLUDE_FILES ${USDR_INCLUDE_FILES} PARENT_SCOPE)
