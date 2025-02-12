# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ DESTINATION libexec/usdr_dmonitor/ FILES_MATCHING PATTERN "*.py" PATTERN "*.svg" PATTERN "*.yaml")
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/usdr_registers DESTINATION libexec/usdr_dmonitor/)
