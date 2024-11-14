// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef UART_H
#define UART_H

#include <usdr_port.h>
#include <usdr_lowlevel.h>
#include <stdbool.h>

enum {
    TX_BUF = 4096,
    RX_BUF = 4096,
};

// 9600 bod
struct uart_core {
    lldev_t lldev;
    subdev_t subdev;
    unsigned base;


    //char txbuf[TX_BUF];
    //char rxbuf[RX_BUF];
};
typedef struct uart_core uart_core_t;

int uart_core_init(lldev_t lldev, subdev_t subdev, unsigned base, uart_core_t* puac);

int uart_core_rx_get(uart_core_t* puac, unsigned max_buf, char* data);

int uart_core_rx_collect(uart_core_t* puac, unsigned max_buf, char* data, unsigned timeout_ms);

#endif
