# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

from __future__ import annotations

import ast
from functools import reduce
import yaml


# Basic YAML file processor and structure builder

class ParserFields:
    def unpack_bits(self, val, bits_list) -> int:

        if (bits_list is None) or (len(bits_list) == 0):
            return val

        #print("unpack_bits() input val:", "{:0{}b}".format(val, len(bits_list)), ", bits_list:", bits_list)

        res = 0
        offs = min(bits_list)

        for idx, b in enumerate(bits_list, start = (1 - len(bits_list))):
            if val & (1 << -idx):
                res |= (1 << (b - offs))

        #print("unpack_bits() output res:", "{:0{}b}".format(res, max(bits_list) - min(bits_list) + 1))
        return res

    def pack_bits(self, val, bits_list) -> int:

        if (bits_list is None) or (len(bits_list) == 0):
            return val

        #print("pack_bits() input val:", "{:0{}b}".format(val, max(bits_list) - min(bits_list) + 1), ", bits_list:", bits_list)

        res = 0
        offs = min(bits_list)

        for idx, b in enumerate(bits_list, start = (1 - len(bits_list))):
            if val & (1 << (b - offs)):
                res |= (1 << -idx)

        #print("pack_bits() output res:", "{:0{}b}".format(res, len(bits_list)))
        return res

    def __init__(self, yaml, top: ParserTop, page: ParserPages, reg: ParserRegs):
        self.reg = reg

        self.name = yaml['name']
        self.desc = yaml['desc'] if 'desc' in yaml else yaml['name']
        self.bits_raw = str(yaml['bits'])

        self.bits_list = []
        bits = self.bits_raw.split(',')
        if len(bits) > 1:
            # comma-sepatated list of bits
            self.bits_list = [ int(b) for b in bits ]
            self.bits_h = max(self.bits_list)
            self.bits_l = min(self.bits_list)

            self.mask = 0;
            for b in self.bits_list:
                self.mask |= (1 << b)

            self.vmax = reduce(lambda x, y: x | y, [ 1 << i for i in range(len(self.bits_list))])

            #print(self.name, ".vmax =", "{0:b}".format(self.vmax))
            #print(self.name, ".mask =", "{0:b}".format(self.mask))

        else:
            bits = self.bits_raw.split(':')
            if len(bits) == 1:
                bits = self.bits_raw.split('-')

            if len(bits) == 2:
                self.bits_h, self.bits_l = [ int(b) for b in bits ]
            else:
                self.bits_h = self.bits_l = int(bits[0])
            self.mask = reduce(lambda x, y: x | y, [ 1 << i for i in range(self.bits_l, self.bits_h + 1)])
            self.vmax = self.mask >> self.bits_l

        self.opts = []
        if "opts" in yaml:
            self.opts = [ None ] * (self.vmax + 1)
            for o in yaml['opts']:
                #print("%x => %s" % (o, yaml['opts'][o]))
                self.opts[o] = yaml['opts'][o]

        bit_max = top.data_width * reg.ucnt - 1
        if self.bits_h > bit_max:
            raise Exception('Maximum bit %d in description is more that %d bit in "%s::%s" register' % (self.bits_h, bit_max, reg.name, self.name))
        
        if top.verbose:
            print('%08x %-32s BITS [%d:%d] MSK %08x' % (reg.addr, ("%s::%s" % (reg.name, self.name)), self.bits_h, self.bits_l, self.mask))


class ParserRegs:
    def __init__(self, yaml, top: ParserTop, page: ParserPages):
        self.page = page

        self.name = yaml['name']
        self.fields = []
        self.big = False

        # 0x11:0x10  MSB 0x11  LSB 0x10 -- Little endian
        # 0x10:0x11  MSB 0x10  LSB 0x11 -- Big endian 
        addrs = str(yaml['addr']).split(':') # if yaml['addr'] is str else [ yaml['addr'] ]
        self.addr = addrs[0] if isinstance(addrs[0], int) else ast.literal_eval(addrs[0])
        if len(addrs) == 2:
            self.addr_l = addrs[1] if isinstance(addrs[1], int) else ast.literal_eval(addrs[1])
            # print(addrs, self.addr_l, self.addr)

            if self.addr < self.addr_l:
                self.addr, self.addr_l = self.addr_l, self.addr
                self.big = True

            if self.addr > self.addr_l + 8:
                raise Exception("Combined register is 8+ of wide, please check")
        else:
            self.addr_l = self.addr
        
        self.ucnt = self.addr - self.addr_l + 1
        
        if 'fields' not in yaml or yaml['fields'] is None:
            fake = { "name": "VALUE", "desc": self.name, "bits": "%d:0" % (top.data_width * self.ucnt - 1)}
            self.fields.append(ParserFields(fake, top, page, self))
        else:
            for field in yaml['fields']:
                self.fields.append(ParserFields(field, top, page, self))

        # Put fields in descending order
        self.fields.sort(key = lambda x: x.bits_l, reverse = True)
        
    def __getitem__(self, name: str) -> ParserFields:
        for n in self.fields:
            if n.name == name:
                return n

        raise(Exception("Field '%s' not found!"))


class ParserPages:
    def __init__(self, yaml, top: ParserTop):
        self.top = top

        self.name = yaml['name']
        self.regs = []
        
        for reg in yaml['regs']:
            self.regs.append(ParserRegs(reg, top, self))

    def __getitem__(self, name: str) -> ParserRegs:
        for n in self.regs:
            if n.name == name:
                return n

        raise(Exception("Register '%s' not found!"))
    

class ParserTop:
    def __init__(self, yaml, verbose = False):
        self.raw_yaml = yaml
        self.name = yaml['name']
        self.desc = yaml['desc'] if 'desc' in yaml else yaml['name']
        self.reg_prefix = yaml['reg_prefix'] if 'reg_prefix' in yaml else ''
        self.page_prefix = bool(yaml['page_prefix']) if 'page_prefix' in yaml else False
        self.field_prefix_ar = yaml['field_prefix'] if 'field_prefix' in yaml else []
        self.field_macros = yaml['field_macros'] if 'field_macros' in yaml else False
        self.addr_width = int(yaml['addr_width'])
        self.data_width = int(yaml['data_width'])
        self.pages = []
        self.path = None
        self.wr_mask = None
        self.rd_mask = None
        self.verbose = verbose
        self.enums = {}
        self.bus_type = "virtual"
        if 'bus' in yaml:
            if 'type' in yaml['bus']:
                self.bus_type = str(yaml['bus']['type']).lower()
            if 'usdr_path' in yaml['bus']:
                self.path = yaml['bus']['usdr_path']
            if 'wr_mask' in yaml['bus']:
                self.wr_mask = yaml['bus']['wr_mask']
            if 'rd_mask' in yaml['bus']:
                self.rd_mask = yaml['bus']['rd_mask']

        for page in yaml['pages']:
            self.pages.append(ParserPages(page, self))

        # Parse enums
        for xin in yaml:
            if xin.startswith('x-'):
                self.enums[xin[2:]] = ({ yaml[xin][x]: x for x in yaml[xin] })


    @staticmethod
    def fromFile(filename):
        with open(filename, 'r') as file:
            docs = yaml.safe_load_all(file)
            return ParserTop(next(docs))


    def __getitem__(self, name: str) -> ParserPages:
        for n in self.pages:
            if n.name == name:
                return n

        raise(Exception("Page '%s' not found!"))
