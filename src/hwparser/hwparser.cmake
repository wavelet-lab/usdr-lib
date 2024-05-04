# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

set(HWPARSER_PATH_BIN ${CMAKE_CURRENT_LIST_DIR})

find_package(PythonInterp 3 REQUIRED)

macro(GENERATE_YAML_H yaml_source h_dest)
    add_custom_command(
        OUTPUT ${h_dest}
        COMMAND ${PYTHON_EXECUTABLE} ${HWPARSER_PATH_BIN}/gen_h.py  --yaml ${yaml_source} --ch ${h_dest} > ${h_dest}
        DEPENDS ${HWPARSER_PATH_BIN}/reg_parser.py ${HWPARSER_PATH_BIN}/gen_h.py ${yaml_source}
        VERBATIM
    )
    get_filename_component(barename ${yaml_source} NAME_WE)
    add_custom_target(generate_${barename} DEPENDS ${h_dest})

    message(STATUS "YAML=>C generate_${barename}")
endmacro()

