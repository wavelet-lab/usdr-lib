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
import glob
import errno
import re

from conn_pipe import ConnDebugPipe

from PyQt5.QtWidgets import *
from PyQt5.QtGui import QPalette, QColor, QIcon
from PyQt5.QtCore import Qt, QTimer

import pyqtgraph as pg


class Sensors(QWidget):
    def __init__(self, params, pipe):
        super(Sensors, self).__init__()
        self.max_points = 7200

        self.pens = [
            pg.mkPen(color=(255, 0,   0)),
            pg.mkPen(color=(255, 255, 0)),
            pg.mkPen(color=(0,   255, 0)),
            pg.mkPen(color=(255, 0,   255)),
            pg.mkPen(color=(0,   255, 0)),
            pg.mkPen(color=(0,   255, 255)),
            pg.mkPen(color=(255, 255, 255))
        ]

        self.layout = QGridLayout()
        self.pipe = pipe
        self.timer = QTimer()
        self.timer.timeout.connect(self.on_timer)
        self.sensors = params
        #self.timer.setInterval(1000)
        self.timer.start(1000)

        self.plot_graph = pg.PlotWidget()
        self.plot_graph.setLabel("bottom", "Time (sec)")
        self.plot_graph.addLegend()

        self.lv = QVBoxLayout()
        self.lv.addWidget(self.plot_graph)
        self.setLayout(self.lv)

        self.time = [ ]
        self.vals = [ ]
        self.line = [ ]
        for i in range(len(params)):
            self.vals.append([])
            self.line.append(self.plot_graph.plot(
                self.time,
                self.vals[i],
                name=params[i],
                pen=self.pens[i % len(self.pens)]
            ))


    def on_timer(self):
        if (len(self.time) > self.max_points):
            self.time = self.time[1:]
        elif (len(self.time) == 0):
            self.time.append(0)
        else:
            self.time.append(self.time[-1] + 1)

        for idx, i in enumerate(self.sensors):
            res, v = self.pipe.geti64(i)
            print("sensor[%s] = %s, %s" % (i, res, v))

            if (len(self.vals[idx]) > self.max_points):
                self.vals[idx] = self.vals[idx][1:]

            self.vals[idx].append(int(v) / 256.0)
            self.line[idx].setData(self.time, self.vals[idx])

    def update(self):
        pass




class GeneralProperties(QScrollArea):
    def __init__(self, params, pipe):
        super(GeneralProperties, self).__init__()
        self.layout = QGridLayout()
        self.pipe = pipe

        sizePolicyE = QSizePolicy(QSizePolicy.Preferred, QSizePolicy.Preferred)
        sizePolicyS = QSizePolicy(QSizePolicy.MinimumExpanding, QSizePolicy.Preferred)

        for i, prop in enumerate(params):
            hidx = 0

            self.layout.addWidget(QLabel(prop), i, hidx, 1, 1)
            hidx += 1

            gv = QLineEdit()
            gv.setMinimumWidth(250)
            gv.setSizePolicy(sizePolicyE)
            self.layout.addWidget(gv, i, hidx, 1, 1)
            hidx += 1

            bt_get = QPushButton("GET")
            self.layout.addWidget(bt_get, i, hidx, 1, 1)
            hidx += 1

            bt_set = QPushButton("SET")
            self.layout.addWidget(bt_set, i, hidx, 1, 1)
            hidx += 1

            status = QLabel()
            status.setSizePolicy(sizePolicyS)
            self.layout.addWidget(status, i, hidx, 1, 1)
            hidx += 1

            bt_get.clicked.connect(lambda index, s = str(prop), e = gv, st = status: self.on_get_param(s, e, st))
            bt_set.clicked.connect(lambda index, s = str(prop), e = gv, st = status: self.on_set_param(s, e, st))

        pgw = QWidget()
        pgw.setLayout(self.layout)

        self.setSizePolicy(QSizePolicy.MinimumExpanding, QSizePolicy.Minimum)
        self.setFrameShadow(QFrame.Plain)
        self.setFrameShape(QFrame.NoFrame)
        self.setWidget(pgw)
        self.setWidgetResizable(True)


    def on_get_param(self, param, edit, stat):
        res, v = self.pipe.geti64(param)
        print("GET(%s): R = %s, V = %s" % (param, res, v))
        if res == "OK":
            edit.setText("%u" % v)
            stat.setText("OK")
        else:
            stat.setText("FAIL: Error %d (%s)" % (v, errno.errorcode[-v]))


    def on_set_param(self, param, edit, stat):
        try:
            value = int(edit.text())
        except ValueError as e:
            try:
                value = int(float(edit.text()))
            except ValueError as e:
                stat.setText("FAIL: Error %s" % str(e))
                return

        res, v = self.pipe.seti64(param, value)
        print("SET(%s, %d): R = %s, V = %s" % (param, value, res, v))
        if res == "OK":
            stat.setText("OK")
        else:
            stat.setText("FAIL: Error %d (%s)" % (v, errno.errorcode[-v]))


    def update(self):
        pass


class RegActor:
    def __init__(self, pipe, parser, path):
        self.pipe = pipe
        self.addr_width = parser.addr_width
        self.data_width = parser.data_width
        self.addr_mask = ((1 << self.addr_width) - 1)
        self.data_mask = ((1 << self.data_width) - 1)
        self.wr_mask = 0 if parser.wr_mask is None else parser.wr_mask
        self.rd_mask = 0 if parser.rd_mask is None else parser.rd_mask
        self.path = path

    def make_reg(self, addr, value):
        val = (addr & self.addr_mask) << self.data_width
        val |= value & self.data_mask
        return val

    def __setitem__(self, addr, value):
        pipe.seti64(self.path, self.make_reg(addr, value) | self.wr_mask, )
        print("WR%s[%04x]<=%04x" % (self.path, addr, value))


    def __getitem__(self, addr):
        r = pipe.setgeti64(self.path, (self.make_reg(addr, -1) & ~self.wr_mask) | self.rd_mask) & self.data_mask
        print("RD%s[%04x]=>%04x" % (self.path, addr, r))
        return r


class RegistersWidget:
    def __init__(self, pipe, regparser, path):
        self.widget = pyqt5_widget.QtBuilderTop(regparser, RegActor(pipe, regparser, path))



class ParserCollection:
    def __init__(self, paths):
        self.parsers = []
        path = paths.split(':')
        for p in path:
            print("Looking for YAML in %s" % p)
            for i in glob.glob(p + "/*.yaml"):
                print(i)
                with open(i, 'r') as f:
                    docs = yaml.safe_load_all(f)
                    pdata = reg_parser.ParserTop(next(docs))
                    self.parsers.append(pdata)

        print("Parsed %d register representers!" % len(self.parsers))


    def find_hw_parser(self, pf):
        for p in self.parsers:
            pattern = '^\s*' + str(p.path).replace('*', '([a-zA-Z0-9]+)') + '\s*$'
            res = re.match(pattern, pf)
            if res is not None:
                return p

        return None


class MainWindow(QMainWindow):
    def __init__(self, pipe, yparse):
        super(MainWindow, self).__init__()
        self.setWindowTitle("uSDR control")

        self.pages = []
        self.l = QListWidget()
        self.p = QWidget()
        self.qs = QSplitter(self)
        self.qs.addWidget(self.l)
        self.qs.addWidget(self.p)

        self.setCentralWidget(self.qs)

        self.icons = {
            "i2c": QIcon("img/i2c_logo.svg"),
            "spi": QIcon("img/spi_logo.svg"),
            "virtual": QIcon("img/virt_logo.svg"),
            "ctrl": QIcon("img/ctrl_logo.svg"),
            "info": QIcon("img/info_logo.svg"),
            "stat": QIcon("img/stat_logo.svg"),
        }

        if type(pipe) is FakePipe:
            for y in yparse.parsers:
                self.pages.append(pyqt5_widget.QtBuilderTop(y, RegActor(pipe, y, "")))
                QListWidgetItem(y.path, self.l)
        else:
            objs_list = pipe.get_objects("/debug/hw/*/reg")
            objs = objs_list.split(',')
            count = int(objs[1])
            for z in range(count):
                p = yparse.find_hw_parser(objs[z + 2])
                if p is not None:
                    self.pages.append(pyqt5_widget.QtBuilderTop(p, RegActor(pipe, p, objs[z + 2])))
                    names = objs[z + 2].split('/')
                    QListWidgetItem(self.icons[p.bus_type], "%s[%s]" % (names[3], names[4]), self.l)

            # General properties
            objs_list = pipe.get_objects("/dm/sdr/0/*")
            objs = objs_list.split(',')
            count = int(objs[1])
            if count > 0:
                self.gp = GeneralProperties(objs[2:], pipe)
                self.pages.append(self.gp)
                QListWidgetItem(self.icons["info"], "GENERAL", self.l)

            # Sensors
            objs_list = pipe.get_objects("/dm/sensor/*")
            objs = objs_list.split(',')
            count = int(objs[1])
            if count > 0:
                self.gp = Sensors(objs[2:], pipe)
                self.pages.append(self.gp)
                QListWidgetItem(self.icons["stat"], "Sensors", self.l)

        self.l.currentItemChanged.connect(lambda item, prev: self.qs.replaceWidget(1, self.pages[self.l.row(item)]) and self.pages[self.l.row(item)].update())



class FakePipe:
    def seti64(self, path, value):
        return -1

    def geti64(self, path):
        return -1

    def setgeti64(self, path, value):
        return -1

    def get_objects(self, wcard = '*'):
        return []

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Debug UI options')
    parser.add_argument('--ypath', dest='ypath', type=str, default="./yamls/")
    parser.add_argument('--pipe', dest='pipe', type=str, default="usdr_debug_pipe",
                        help='sum the integers (default: find the max)')
    parser.add_argument('--fake', dest='fake', action='store_true', help='use fake pipe')
    args = parser.parse_args()
    app = QApplication(sys.argv)

    yparse = ParserCollection(args.ypath)
    pipe = FakePipe() if args.fake else ConnDebugPipe(args.pipe)

    window = MainWindow(pipe, yparse)
    window.show()

    sys.exit(app.exec_())
