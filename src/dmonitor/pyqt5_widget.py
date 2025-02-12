#!/usr/bin/python3

# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

import sys
from PyQt5.QtWidgets import *
from PyQt5.QtGui import QRegExpValidator, QPalette, QColor, QIcon
from PyQt5.QtCore import Qt, QObject, pyqtSignal, QRegExp
import math
import configparser
import re
import time


ABS_PATH = os.path.dirname(os.path.abspath(__file__))


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
        self.conf = conf

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
                raw.setNumDigits(top.data_width // 4)
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
                    for n, v in enumerate(field.opts):
                        part.addItem("%d -- %s" % (n, v))

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
                v = f.unpack_bits(ui.currentIndex(), f.bits_list) << f.bits_l
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
            ui.setCurrentIndex(field.pack_bits(v, field.bits_list))
        else:
            ui.setValue(v)

    def page_changed(self, dump = False)->dict:

        self.ignore = True
        addr_size_chars = math.ceil(self.conf.addr_width / 4)
        val_size_chars  = math.ceil(self.conf.data_width / 4)
        dump_dict = {}

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

                if(dump):
                    dump_dict['0x%0*x' % (addr_size_chars, addr + i)] = '0x%0*x' % (val_size_chars, rv)

            for ui, field in fields:
                self.update_filed(value, ui, field)
        self.ignore = False
        return dump_dict


    def page_dump(self)->str:
        return self.page_changed(dump = True)


class QtBuilderTop(QWidget):
    def __init__(self, conf, actor):
        super(QtBuilderTop, self).__init__()
        self.tab = QTabWidget()
        self.tab.currentChanged.connect(self.page_changed)
        self.b_pgs = []
        self.actor = actor
        self.data_width = conf.data_width
        self.addr_width = conf.addr_width
        self.current = 0
        self.conf = conf

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

        saveAction = QAction(QIcon(f"{ABS_PATH}/img/save.png"), 'Save', self)
        saveAction.setShortcut('Ctrl+S')
        saveAction.triggered.connect(self.save_button_clicked)

        loadAction = QAction(QIcon(f"{ABS_PATH}/img/load.png"), 'Load', self)
        loadAction.setShortcut('Ctrl+L')
        loadAction.triggered.connect(self.load_button_clicked)

        toolbar = QToolBar()
        toolbar.addAction(saveAction)
        toolbar.addAction(loadAction)

        self.lv = QVBoxLayout()
        self.lv.addWidget(toolbar)
        self.lv.addWidget(self.tab)
        self.setLayout(self.lv)


    def update(self):
        self.b_pgs[self.current].page_changed()

    def page_changed(self, page):
        print("Page changed to %d" % page)
        self.b_pgs[page].page_changed()
        self.current = page


    def get_ini_section_name(self)->str:
        ini_section_name = self.conf.ini_section;
        names = self.actor.path.split('/')

        if(not ini_section_name):
            ini_section_name = '%s_registers_%s' % (names[3], names[4])
        else:
            ini_section_name = '%s_%s' % (ini_section_name, names[4])

        return ini_section_name

    def load_button_clicked(self)->None:
        print("LOAD [%s] regs from ini file" % self.actor.path)

        inifile_name, _unused = QFileDialog.getOpenFileName(self, caption = "Load Registers Set for " + self.actor.path, directory="./", filter="Ini Files (*.ini)")
        if not inifile_name:
            return

        ini_section_name = self.get_ini_section_name()

        found = False
        regcnt = 0
        f = open(inifile_name, 'r')

        while(True):
            line = f.readline()
            if(not line):
                break

            line = line.rstrip('\n').strip()

            if(found):

                if re.match('^\[(.*)\]$', line) is not None:
                    break

                parts = line.split('=')
                if(len(parts) != 2):
                    continue

                try:
                    addr = int(parts[0], 16)
                    val  = int(parts[1], 16)
                except ValueError:
                    continue

                self.actor[addr] = val
                time.sleep(0.01)
                regcnt += 1
                #print('loaded 0x%04x=0x%04x' % (addr, val))
            else:
                found = ('[%s]' % ini_section_name) == line
                if(found):
                    print('LOAD: found section [%s] in file "%s"' % (ini_section_name, inifile_name))

        if(not found):
            print('LOAD: section [%s] not found in file "%s", registers were not loaded!' % (ini_section_name, inifile_name))
        else:
            print('LOAD: %d registers were loaded from "%s".[%s]' % (regcnt, inifile_name, ini_section_name))
            self.update()

        f.close()

    def save_button_clicked(self)->None:
        print("SAVE [%s] regs to ini file" % self.actor.path)

        inifile_name, _unused = QFileDialog.getSaveFileName(self, caption = "Save Registers Set for " + self.actor.path, filter="Ini Files (*.ini)", directory="./")
        if not inifile_name:
            return

        ini_section_name = self.get_ini_section_name()

        dump_dict = {}
        for page in self.b_pgs:
            dump_dict.update(page.page_dump())

        config = configparser.ConfigParser()
        config.read(inifile_name)

        if not config.has_section(ini_section_name):
            config.add_section(ini_section_name)

        for addr, val in dump_dict.items():
            config[ini_section_name][addr] = val

        with open(inifile_name, 'w') as configfile:
            config.write(configfile)
