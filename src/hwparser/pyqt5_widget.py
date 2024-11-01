#!/usr/bin/python3

# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

import sys
from PyQt5.QtWidgets import *
from PyQt5.QtGui import QRegExpValidator, QPalette, QColor
from PyQt5.QtCore import Qt, QObject, pyqtSignal, QRegExp
import math

class LongSpinBox(QAbstractSpinBox):
    valueChanged = pyqtSignal(int)
    clicked = pyqtSignal(Qt.MouseButton)

    def __init__(self, parent = None):
        super(LongSpinBox, self).__init__(parent)
        self.int_value = 0
        self.minimum = 0
        self.setMaximum(255)
        self.lineEdit().setText("0")
        self.lineEdit().textChanged.connect(lambda t: self.setValue(int(t)))
        self.lineEdit().setValidator(QRegExpValidator(QRegExp("[0-9]*"), self.lineEdit()))

    def setMaximum(self, max_value):
        self.maximum = max_value
        nums = math.ceil(math.log10(self.maximum))
        self.setMinimumWidth(40 + 20 * nums)

    def text(self):
        return str(self.int_value)
    
    def value(self):
        return self.int_value
    
    def setValue(self, val):
        if val < self.minimum:
            val = self.minimum
        elif val > self.maximum:
            val = self.maximum
        
        if self.int_value != val:
            self.int_value = val
            self.lineEdit().setText(self.text())
            self.valueChanged.emit(val)

    def stepBy(self, steps):
        self.setValue(self.int_value + steps)

    def stepUp(self):
        print("UP")
        self.setValue(self.int_value + 1)
    
    def stepDown(self):
        print("DOWN")
        self.setValue(self.int_value - 1)

    def stepEnabled(self):
        flags = 0
        if self.int_value != self.minimum:
            flags |= QAbstractSpinBox.StepDownEnabled
        if self.int_value != self.maximum:
            flags |= QAbstractSpinBox.StepUpEnabled
        return flags


class QtBuilderPage(QWidget):
    def __init__(self, conf, page, top):
        super(QtBuilderPage, self).__init__()
        self.top = top
        self.layout = QGridLayout()
        self.raw_lcds = {}
        self.reg_updates = {}
        self.reg_cnt = {}
        self.reg_big = {}
        self.ignore = False
        
        for i, reg in enumerate(page.regs):
            hidx = 0
            if reg.ucnt == 1:
                lstr = "0x%02x" % (reg.addr)
            elif reg.big:
                lstr = "0x%02x:0x%02x" % (reg.addr_l, reg.addr)
            else:
                lstr = "0x%02x:0x%02x" % (reg.addr, reg.addr_l)

            label = QLabel(lstr)
            label.setToolTip(reg.name)
            self.layout.addWidget(label, i, hidx, 1, 1)
            hidx += 1

            label = QLabel(reg.name)
            label.setToolTip(reg.name)
            self.layout.addWidget(label, i, hidx, 1, 1)
            hidx += 1
            
            lw = QWidget()
            lv = QHBoxLayout()
            self.raw_lcds[reg.addr_l] = []

            for l in range(reg.ucnt):
                raw = QLCDNumber()
                raw.setToolTip("Addr: 0x%02x" % (reg.addr_l + l))
                raw.setMinimumHeight(40)
                raw.setHexMode()
                raw.setNumDigits(top.data_width / 4)
                self.raw_lcds[reg.addr_l].append(raw)
                lv.addWidget(raw)
            lw.setLayout(lv)
            self.layout.addWidget(lw, i, hidx, 1, 1)
            hidx += 1
            
            fuis = []
            # Parse bit fields
            for j, field in enumerate(reg.fields):
                fname = QLabel(field.name)
                fname.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
                fname.setToolTip("Bits[%d:%d]" % (field.bits_h, field.bits_l))
                self.layout.addWidget(fname, i + 0, hidx + 2 * j + 0)
                
                if len(field.opts) > 1:
                    part = QComboBox()
                    for (k, v) in field.opts.items():
                        part.addItem("%d -- %s" % (v, k))
                        
                    part.currentIndexChanged.connect(lambda index, a = int(reg.addr_l), u = reg.ucnt: self.update_control(index, a, u))
                elif field.vmax == 1:
                    part = QCheckBox()
                    part.clicked.connect(lambda checked, a = int(reg.addr_l), u = reg.ucnt: self.update_control(checked, a, u))
                else:
                    part = QSpinBox() if reg.ucnt == 1 else LongSpinBox()
                    part.setMaximum(field.vmax)
                    part.valueChanged.connect(lambda value, a = int(reg.addr_l), u = reg.ucnt: self.update_control(value, a, u))
                part.setToolTip(field.desc)
                self.layout.addWidget(part,  i + 0, hidx + 2 * j + 1)
                fuis.append((part, field))
                
            self.reg_updates[reg.addr_l] = fuis
            self.reg_cnt[reg.addr_l] = reg.ucnt
            self.reg_big[reg.addr_l] = reg.big
        self.setLayout(self.layout)


    def update_control(self, value, addr, ucnt):
        if self.ignore:
            return
        
        big = self.reg_big[addr]
        newVal = 0
        for i in range(ucnt):
            print(addr, ucnt, i, newVal)
            if big:
                newVal = newVal | (self.raw_lcds[addr][ucnt - i - 1].intValue() << (i * self.top.data_width))
            else:
                newVal = newVal | (self.raw_lcds[addr][i].intValue() << (i * self.top.data_width))

        for ui, f in self.reg_updates[addr]:
            newVal &= ~f.mask
            if isinstance(ui, QCheckBox):
                v = 1 << f.bits_l if ui.isChecked() else 0
            elif isinstance(ui, QComboBox):
                v = ui.currentIndex() << f.bits_l
            else:
                v = ui.value() << f.bits_l

            newVal |= v
        
        print("Update register %04x x %d => %d" % (addr, ucnt, newVal))
        for i in range(ucnt):
            if big:
                v = (newVal >> ((ucnt - i - 1) * self.top.data_width)) & ((1 << self.top.data_width) - 1)
            else:
                v = (newVal >> (i * self.top.data_width)) & ((1 << self.top.data_width) - 1)

            self.raw_lcds[addr][i].display(v)
            self.top.actor[addr + i] = v
        
    def update_filed(self, value, ui, field):
        v = (value & field.mask) >> field.bits_l
        if isinstance(ui, QCheckBox):
            ui.setChecked(v)
        elif isinstance(ui, QComboBox):
            ui.setCurrentIndex(v)
        else:
            ui.setValue(v)
        
    def page_changed(self):
        self.ignore = True
        for addr in self.reg_updates:
            fields = self.reg_updates[addr]
            ucnt = self.reg_cnt[addr]
            big = self.reg_big[addr]
            value = 0

            for i in range(ucnt):
                rv = self.top.actor[addr + i]
                self.raw_lcds[addr][i].display(rv)
                if big:
                    value = value | (rv << ((ucnt - i - 1) * self.top.data_width))
                else:
                    value = value | (rv << (i * self.top.data_width))

            for ui, field in fields:
                self.update_filed(value, ui, field)
        self.ignore = False
        

class QtBuilderTop:
    def __init__(self, conf, actor):
        self.tab = QTabWidget()
        self.tab.currentChanged.connect(self.page_changed)
        self.b_pgs = []
        self.actor = actor
        self.data_width = conf.data_width
        self.addr_width = conf.addr_width

        for page in conf.pages:
            pgw = QtBuilderPage(conf, page, self)
            self.b_pgs.append(pgw)

            #w = QWidget()
            #w.setSizePolicy(QSizePolicy.MinimumExpanding, QSizePolicy.MinimumExpanding)
            #w.setLayout(pgw)
            q = QScrollArea(self.tab)
            # q.setWidgetResizable(True)
            q.setSizePolicy(QSizePolicy.MinimumExpanding, QSizePolicy.Minimum)
            q.setFrameShadow(QFrame.Plain)
            q.setFrameShape(QFrame.NoFrame)
            q.setWidget(pgw)
            

            self.tab.addTab(q, page.name)

    def page_changed(self, page):
        print("Page changed to %d" % page)
        self.b_pgs[page].page_changed()

