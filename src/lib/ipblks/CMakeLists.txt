# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

set(USDR_IPBLKS_LIB_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/si2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/uart.c

    ${CMAKE_CURRENT_SOURCE_DIR}/streams/streams.c
    ${CMAKE_CURRENT_SOURCE_DIR}/streams/streams_api.c

    ${CMAKE_CURRENT_SOURCE_DIR}/streams/stream_sfetrx4_dma32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/streams/stream_sfetrx4_ctrl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/streams/stream_limesdr.c


    ${CMAKE_CURRENT_SOURCE_DIR}/streams/sfe_rx_4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/streams/dma_rx_32.c

    ${CMAKE_CURRENT_SOURCE_DIR}/streams/sfe_tx_4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/streams/dma_tx_32.c

    ${CMAKE_CURRENT_SOURCE_DIR}/espi_flash.c

    ${CMAKE_CURRENT_SOURCE_DIR}/xlnx_bitstream.c

    ${CMAKE_CURRENT_SOURCE_DIR}/xlnx_mmcm.c

    ${CMAKE_CURRENT_SOURCE_DIR}/lms64c_proto.c

    ${CMAKE_CURRENT_SOURCE_DIR}/fgearbox.c
)


list(APPEND USDR_LIBRARY_FILES ${USDR_IPBLKS_LIB_FILES})
set(USDR_LIBRARY_FILES ${USDR_LIBRARY_FILES} PARENT_SCOPE)
