#!/usr/bin/python3
# -*- coding: utf-8 -*-

# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

import os
import sys
import socket
import argparse
import reg_parser
import pyqt5_widget
import conn_pipe
import yaml
from conn_pipe import ConnDebugPipe

from PyQt5.QtWidgets import *
from PyQt5.QtGui import QPalette, QColor
from PyQt5.QtCore import Qt


# Class usage example
class MainWindow(QMainWindow):
    def __init__(self, top):
        super(MainWindow, self).__init__()
        self.setWindowTitle("uSDR control")
        self.setCentralWidget(top.tab)


class FakePipe:
    def reg_spi(self, reg, rd = False):
        print("FPIPE %08x RD:%d" % (reg, rd))
        return -1


class SPIActor:
    def __init__(self, pipe, top):
        self.pipe = pipe
        self.addr_width = top.addr_width #16
        self.data_width = top.data_width #8
        self.addr_mask = ((1 << self.addr_width) - 1)
        self.data_mask = ((1 << self.data_width) - 1)
        self.wr_mask = top.wr_mask #0x800000

    def make_reg(self, addr, value):
        val = (addr & self.addr_mask) << self.data_width
        val |= value & self.data_mask
        return val

    def __setitem__(self, addr, value):
        pipe.reg_spi(self.make_reg(addr, value) | self.wr_mask)

    def __getitem__(self, addr):
        r = pipe.reg_spi(self.make_reg(addr, -1) & ~self.wr_mask, True) & self.data_mask
        print("RD[%02x]=>%04x" % (addr, r))
        return r


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Debug UI options')
    parser.add_argument('--pipe', dest='pipe', type=str, default="usdr_debug_pipe",
                        help='sum the integers (default: find the max)')
    parser.add_argument('--fake', dest='fake', action='store_true', help='use fake pipe')
    parser.add_argument('--hwyaml', dest='hwyaml', type=str, help='use this hw register definitions')
    args = parser.parse_args()
    app = QApplication(sys.argv)

    pdata = reg_parser.ParserTop.fromFile(args.hwyaml)
    pipe = FakePipe() if args.fake else ConnDebugPipe(args.pipe, pdata.path)
    topWidget = pyqt5_widget.QtBuilderTop(pdata, SPIActor(pipe, pdata))

    window = MainWindow(topWidget)
    window.show()

    sys.exit(app.exec_())
