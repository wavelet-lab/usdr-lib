// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef PCIE_URAM_MAIN
#define PCIE_URAM_MAIN

#include <usdr_port.h>
#include <usdr_logging.h>
#include <usdr_lowlevel.h>


// Static registration function
const struct lowlevel_plugin* pcie_uram_register();

#endif
