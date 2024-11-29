#!/usr/bin/python3
# -*- coding: utf-8 -*-

# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

import os
import sys
import socket

class ConnDebugPipe:
    def __init__(self, pipe, reg_path = ""):
        self.fd = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.fd.connect(pipe)
        self.chan = 0
        self.reg_path = reg_path

    def get_objects(self, wcard = '*'):
        ss = self.transfer("LS,%s\n" % wcard, 128 * 1024);
        return ss

    def transfer(self, string, maxbuf = 1024):
        self.fd.sendall(bytearray(string, 'utf-8'))
        ret = self.fd.recv(maxbuf)
        return ret.decode('utf-8')

    def tr_get_int(self, string):
        ss = self.transfer(string)
        r, vs = ss.split(',')
        v = int(vs, 16)
        return r, v

    def seti64(self, path, value):
        return self.tr_get_int("SETU64,%s,%016x\n" % (path, value))

    def geti64(self, path):
        return self.tr_get_int("GETU64,%s\n" % (path))

    def setgeti64(self, path, value):
        self.seti64(path, value)
        return self.geti64(path)[1]

    # deprecated
    def set_chan(self, chan):
        self.chan = chan

    def reg_spi(self, reg, rd = False):
        string = ("SETU64,%s,%010x\n" % (self.reg_path, ((1 << self.chan) << 32) | reg))
        if rd:
            self.tr_get_int(string)
            string = ("GETU64,%s\n" % self.reg_path)
        return self.tr_get_int(string)[1]
