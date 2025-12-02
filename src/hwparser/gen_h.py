#!/usr/bin/python3
# -*- coding: utf-8 -*-

# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

import os
import argparse
import reg_parser
from functools import reduce


class GenH:
    TAB = "    "

    def __init__(self, parser: reg_parser.ParserTop, filename: str) -> None:
        self.o_name = str(os.path.splitext(os.path.basename(filename))[0])
        self.l_name = self.o_name.lower()
        self.h_name = self.o_name.upper()

        self.parser = parser
        self.addr_width = parser.addr_width
        self.data_width = parser.data_width

        self.enums = parser.enums
        self.page_prefix = parser.page_prefix
        self.reg_prefix = "%s_" % parser.reg_prefix.upper() if len(parser.reg_prefix) > 0 else ''

        self.field_prefix_ar = [a.lower() for a in parser.field_prefix_ar]

        # Flat all pages
        self.regs = {}
        for p in self.parser.pages:
            pname = p.name.upper()
            for r in p.regs:
                name = "%s_%s" % (pname, r.name) if self.page_prefix else r.name

                if name in self.regs.keys():
                    raise (Exception("Rigester `%s` is already in flat map! Rename it" % name))

                # TODO: parse ucnt
                if r.ucnt == 1:
                    self.regs[name] = r.addr
                elif r.big:
                    for k in range(r.ucnt):
                        self.regs[name + "_BY%d" % k] = r.addr_l + k
                else:
                    for k in range(r.ucnt):
                        self.regs[name + "_BY%d" % (r.ucnt - k - 1)] = r.addr_l + k

    def regName(self, reg: reg_parser.ParserRegs) -> str:
        if self.page_prefix:
            return "%s_%s" % (reg.page.name.upper(), reg.name)

        return reg.name

    def fieldName(self, field: reg_parser.ParserFields) -> str:
        pfx = []
        for i in self.field_prefix_ar:
            if i == "page":
                pfx.append(field.reg.page.name.upper())
            elif i == "regname":
                pfx.append(field.reg.name.upper())
            elif i == "regaddr":
                pfx.append("%02x" % field.reg.addr_l)
            else:
                raise (Exception("Unknown prefix type '%s'" % i))

        if len(pfx) > 0:
            pfx.append(field.name)
            return reduce(lambda x, y: "%s_%s" % (x, y), pfx)

        return field.name

    def normalize(self, name: str) -> str:
        return (name.replace('-', '_')
                .replace('<=', 'LE')
                .replace('>=', 'GE')
                .replace('>', 'GT')
                .replace('<', 'LT')
                .replace('=', 'EQ')
                .replace('+', 'PL')
                .replace("'", 'MARK')
                .replace('.', '_')
                .replace(',', '_')
                .replace(' ', '_')
                .replace('(', '')
                .replace('|', 'OR')
                .replace(')', '')
                .replace('/', 'DIV'))

    def ser_ch_fenum(self, reg: reg_parser.ParserRegs, name: str) -> str:
        vt = False
        str = "enum %s_fields_t {\n" % name.lower()
        for f in reg.fields:
            if f.name.lower() == "value":
                vt = True
            str += "%s%s_OFF = 0x%x,\n" % (GenH.TAB, self.fieldName(f), f.bits_l)
            str += "%s%s_MSK = 0x%x,\n" % (GenH.TAB, self.fieldName(f), f.mask)
        str += "};"
        if vt and len(reg.fields) == 1:
            return ""

        return str

    def generate_setter_expression(self, f, custom_name) -> str:
        res = ""
        fname = self.fieldName(f)
        bwidth = len(f.bits_list)
        if bwidth > 0:
            res += "(("
            for i in range(bwidth):
                idx = bwidth - i - 1
                res += "((((%s) >> %d) & 0x1) << %d)" % (custom_name, idx, f.bits_list[i])
                if idx != 0:
                    res += " | "
            res += ") & %s_MSK)" % (fname)
        else:
            res += "(((%s) << %s_OFF) & %s_MSK)" % (custom_name, fname, fname)

        return res

    def generate_getter_expression(self, f, custom_name) -> str:
        res = ""
        fname = self.fieldName(f)
        bwidth = len(f.bits_list)
        if bwidth > 0:
            res = "("
            for i in range(bwidth):
                idx = bwidth - i - 1
                res += "(((((%s) & %s_MSK) >> %d) & 0x1) << %d)" % (custom_name, fname, f.bits_list[idx], i)
                if idx != 0:
                    res += " | "
            res += ")"
        else:
            res += "(((%s) & %s_MSK) >> %s_OFF)" % (custom_name, fname, fname)

        return res

    def ser_cf_fmacro(self, reg: reg_parser.ParserRegs) -> str:
        str1 = ""
        str2 = ""
        for f in reg.fields:
            fname = self.fieldName(f)

            str1 += "#define GET_%s_%s(x) " % (self.h_name, fname)
            str1 += self.generate_getter_expression(f, "x")
            str1 += "\n"

            str2 += "#define SET_%s_%s(p, f) (p) = ((p) & ~%s_MSK) | " % (self.h_name, fname, fname)
            str2 += self.generate_setter_expression(f, "f")
            str2 += "\n"

        return str1 + str2

    def ser_cf_options(self, reg: reg_parser.ParserRegs) -> str:
        str = ""
        for f in reg.fields:
            if len(f.opts) > 0:
                fname = self.fieldName(f)
                str += "enum %s_options {\n" % fname.lower()
                for i, o in enumerate(f.opts):
                    if o is not None:
                        str += "%s%s_%s = %d,\n" % (self.TAB, fname, self.normalize(o.upper()), i)
                str += "};\n"
        return str

    def ser_ch_enum(self, name: str, en_dict: dict, prefix: str = "") -> str:
        str = "enum %s_t {\n" % name
        for i, v in en_dict.items():
            str += "%s%s%s = 0x%x,\n" % (GenH.TAB, prefix, i.replace('-', '_')
                                         .replace('.', '_')
                                         .replace(',', '_')
                                         .replace(' ', '_')
                                         .replace('(', '')
                                         .replace('|', 'OR')
                                         .replace(')', '')
                                         .replace('/', 'DIV'), v)
        str += "};"
        return str

    def write_ch(self, filename):
        # All registers
        all_regs = self.ser_ch_enum(self.l_name + "_regs", self.regs, self.reg_prefix)
        print(all_regs)

        # Make register define
        if (self.parser.wr_mask is not None) and (self.parser.rd_mask is not None):
            raise (Exception("You should specify rd_mask OR wr_mask, but not both!"))

        def_macro = "MAKE_%s_REG_WR" % self.h_name
        def_wr_msk = "0x%x | " % self.parser.wr_mask if self.parser.wr_mask is not None else ""
        def_wr = "#define %s(a, v) (%s((a) << %d) | ((v) & 0x%x))" % (def_macro, def_wr_msk, self.data_width, (1 << self.data_width) - 1)
        print(def_wr)

        def_macro_rd = "MAKE_%s_REG_RD" % self.h_name
        def_rd_msk = "0x%x | " % self.parser.rd_mask if self.parser.rd_mask is not None else ""
        def_rd = "#define %s(a) (%s((a) << %d))" % (def_macro_rd, def_rd_msk, self.data_width)
        print(def_rd)

        # Predefined universal enums
        for e in self.enums:
            em = e.replace('-', '_')
            enum = self.ser_ch_enum(self.l_name + "_" + em, self.enums[e], em.upper() + "_")
            print(enum)

        for p in self.parser.pages:
            pname = p.name.upper()
            for r in p.regs:
                name = "%s_%s" % (pname, r.name) if self.page_prefix else r.name
                desc = "// Register R%d [0x%0x] -- %s" % (r.addr_l, r.addr_l, name)
                print(desc)

                print(self.ser_cf_options(r))

                ef = self.ser_ch_fenum(r, name)
                if len(ef) < 1:
                    # Skip single word single value registers
                    if r.ucnt == 1:
                        continue

                print(ef)

                if self.parser.field_macros:
                    print(self.ser_cf_fmacro(r))

                if r.ucnt == 1:
                    defc = "#define MAKE_%s_%s(%s)" % (
                    self.h_name, name, reduce(lambda x, y: "%s, %s" % (x, y), [x.name.lower() for x in r.fields]))
                    # defc += " ((%s << %d) |" % (name, self.data_width)
                    defc += " %s(%s," % (def_macro, name)
                    defc += reduce(lambda x, y: "%s | %s" % (x, y),
                                   [" \\\n%s%s" % (self.TAB, self.generate_setter_expression(x, x.name.lower())) for x in r.fields])
                    defc += ")"
                    print(defc)
                else:
                    # comma sep bit-list fields are not supported here
                    for f in r.fields:
                        if f.bits_list and len(f.bits_list) > 0:
                            raise ValueError('Bulk reg operations are not supported for the comma-separated bitlist fields!')

                    value_msk = reduce(lambda x, y: x | y, [x.mask for x in r.fields])
                    value_off = reduce(lambda x, y: min(x, y), [x.bits_l for x in r.fields])
                    if len(r.fields) > 1:
                        defc = "#define MAKE_%s_%s_LONG(%s) (" % (
                        self.h_name, name, reduce(lambda x, y: "%s, %s" % (x, y), [x.name.lower() for x in r.fields]))
                        defc += reduce(lambda x, y: "%s | %s" % (x, y),
                                       [" \\\n%s%s" % (self.TAB, self.generate_setter_expression(x, x.name.lower())) for x in r.fields])
                        defc += ")"
                        print(defc)

                    for u in range(r.ucnt):
                        uidx = r.ucnt - u - 1 if r.big else u

                        by_off = self.data_width * uidx - value_off
                        by_msk = (value_msk >> self.data_width * uidx) & ((1 << self.data_width) - 1)

                        defc = "#define MAKE_%s_%s_BY%d(value)" % (self.h_name, name, u)
                        defc += " %s(%s_BY%d," % (def_macro, name, u)
                        if by_off > 0:
                            # defc += " ((%s_BY%d << %d) | (((value) >> %d) & 0x%x))" % (name, u, self.data_width, by_off, by_msk)
                            defc += " (((value) >> %d) & 0x%x))" % (by_off, by_msk)
                        else:
                            # defc += " ((%s_BY%d << %d) | (((value) << %d) & 0x%x))" % (name, u, self.data_width, -by_off, by_msk)
                            defc += " (((value) << %d) & 0x%x))" % (-by_off, by_msk)
                        print(defc)

                        # if 'c-cache' in self.parser.raw_yaml:
        #     cc = self.parser.raw_yaml['c-cache']
        #     if 'regs' in cc:
        #         print("\n\n/* Cached operations */")
        #         self.parse_c_cache(cc['regs'])

    def parse_c_cache(self, cregs):
        fr = []
        for i in cregs:
            p, r = i.split('.')

            page = self.parser[p]
            regi = page[r]
            fr.append(regi)

        sn = "%s_c" % self.l_name
        tp = "uint%d_t" % self.parser.data_width
        dstr = "#if 0\n"
        dstr += "struct %s {\n" % sn
        dstr += reduce(lambda x, y: "%s\n%s" % (x, y), ["%s%s %s;" % (self.TAB, tp, self.regName(x).lower()) for x in fr])
        dstr += "\n};\n"
        dstr += "#endif\n"
        dstr += "typedef struct %s %s_t;" % (sn, sn)
        print(dstr)

        for r in fr:
            regn = self.regName(r).lower()
            for f in r.fields:
                fname = f.name.lower()
                FLDN = self.fieldName(f)
                fldn = FLDN.lower()
                fn = "static inline void %s_%s_set(%s_t* p, %s %s) { " % (sn, fldn, sn, tp, fname)
                fn += "p->%s = (p->%s & ~%s_MSK) | ((%s << %s_OFF) & %s_MSK); }" % (regn, regn, FLDN, fname, FLDN, FLDN)
                print(fn)

    def write_vh(self, filename):
        pass


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Debug UI options')
    parser.add_argument('--yaml', dest='yaml', type=str,
                        help='path to YAML register map file')
    parser.add_argument('--ch', dest='c_header', type=str,
                        help='generate C header file')
    parser.add_argument('--vh', dest='v_header', type=str,
                        help='generate Verilog header file')
    args = parser.parse_args()

    genh = GenH(reg_parser.ParserTop.fromFile(args.yaml), args.yaml)

    genh.write_ch(args.c_header)
