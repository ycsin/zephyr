#!/usr/bin/env python3
#
# Copyright (c) 2021 Facebook, Inc. and its affiliates
#
# SPDX-License-Identifier: Apache-2.0

import binascii
import logging
import struct

from enum import Enum, IntEnum
from gdbstubs.gdbstub import GdbStub


logger = logging.getLogger("gdbstub")

class RegNum():
    ZERO = 0
    RA = 1
    SP = 2
    GP = 3
    TP = 4
    T0 = 5
    T1 = 6
    T2 = 7
    FP = 8
    S1 = 9
    A0 = 10
    A1 = 11
    A2 = 12
    A3 = 13
    A4 = 14
    A5 = 15
    A6 = 16
    A7 = 17
    S2 = 18
    S3 = 19
    S4 = 20
    S5 = 21
    S6 = 22
    S7 = 23
    S8 = 24
    S9 = 25
    S10 = 26
    S11 = 27
    T3 = 28
    T4 = 29
    T5 = 30
    T6 = 31
    PC = 32


class RiscvCpu(Enum):
    RISCV_CPU_RV64 = 0
    RISCV_CPU_RV32 = 1
    RISCV_CPU_RV32E = 2


class RiscvFlag(IntEnum):
    EXTRA_EXCEPTION_INFO = 1

class GdbStub_RISC_V(GdbStub):
    GDB_SIGNAL_DEFAULT = 7

    GDB_G_PKT_NUM_REGS = 33

    def __init__(self, logfile, elffile):
        super().__init__(logfile=logfile, elffile=elffile)
        self.registers = None
        self.gdb_signal = self.GDB_SIGNAL_DEFAULT
        self.extra_exception_info = False

        self.parse_arch_data_block()

    def parse_arch_data_block(self):
        arch_data_blk = self.logfile.get_arch_data()['data']

        print(len(arch_data_blk[2:]))

        # Parse CPU type
        self.cpu = RiscvCpu(bytearray(arch_data_blk)[0])
        logger.debug(self.cpu)

        # Parse flags
        self.flags = bytearray(arch_data_blk)[1]
        logger.debug(self.flags)
        self.extra_exception_info = (self.flags & int(RiscvFlag.EXTRA_EXCEPTION_INFO)) != 0

        if self.cpu == RiscvCpu.RISCV_CPU_RV64:
            if self.extra_exception_info:
                self.ARCH_DATA_BLK_STRUCT = "<QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ"
            else:
                self.ARCH_DATA_BLK_STRUCT = "<QQQQQQQQQQQQQQQQQQQ"
            self.reg_fmt = "<Q"
            self.reg_sz = 16
        else:
            self.reg_fmt = "<I"
            self.reg_sz = 8
            if self.cpu == RiscvCpu.RISCV_CPU_RV32:
                if self.extra_exception_info:
                    self.ARCH_DATA_BLK_STRUCT = "<IIIIIIIIIIIIIIIIIIIIIIIIIIIIIII"
                else:
                    self.ARCH_DATA_BLK_STRUCT = "<IIIIIIIIIIIIIIIIIII"
            else: # RiscvCpu.RISCV_CPU_RV32E
                if self.extra_exception_info:
                    self.ARCH_DATA_BLK_STRUCT = "<IIIIIIIIIIIIIII"
                else:
                    self.ARCH_DATA_BLK_STRUCT = "<IIIIIIIIIIIII"

        tu = struct.unpack(self.ARCH_DATA_BLK_STRUCT, arch_data_blk[2:])

        self.registers = dict()

        idx = 0
        self.registers[RegNum.RA] = tu[idx]
        idx += 1
        self.registers[RegNum.TP] = tu[idx]
        idx += 1
        self.registers[RegNum.T0] = tu[idx]
        idx += 1
        self.registers[RegNum.T1] = tu[idx]
        idx += 1
        self.registers[RegNum.T2] = tu[idx]
        idx += 1
        self.registers[RegNum.A0] = tu[idx]
        idx += 1
        self.registers[RegNum.A1] = tu[idx]
        idx += 1
        self.registers[RegNum.A2] = tu[idx]
        idx += 1
        self.registers[RegNum.A3] = tu[idx]
        idx += 1
        self.registers[RegNum.A4] = tu[idx]
        idx += 1
        self.registers[RegNum.A5] = tu[idx]
        idx += 1
        if self.cpu != RiscvCpu.RISCV_CPU_RV32E:
            self.registers[RegNum.A6] = tu[idx]
            idx += 1
            self.registers[RegNum.A7] = tu[idx]
            idx += 1
            self.registers[RegNum.T3] = tu[idx]
            idx += 1
            self.registers[RegNum.T4] = tu[idx]
            idx += 1
            self.registers[RegNum.T5] = tu[idx]
            idx += 1
            self.registers[RegNum.T6] = tu[idx]
            idx += 1
        self.registers[RegNum.PC] = tu[idx]
        idx += 1
        self.registers[RegNum.SP] = tu[idx]
        idx += 1
        if self.extra_exception_info:
            self.registers[RegNum.FP] = tu[idx]
            idx += 1
            self.registers[RegNum.S1] = tu[idx]
            idx += 1
            if self.cpu != RiscvCpu.RISCV_CPU_RV32E:
                self.registers[RegNum.S2] = tu[idx]
                idx += 1
                self.registers[RegNum.S3] = tu[idx]
                idx += 1
                self.registers[RegNum.S4] = tu[idx]
                idx += 1
                self.registers[RegNum.S5] = tu[idx]
                idx += 1
                self.registers[RegNum.S6] = tu[idx]
                idx += 1
                self.registers[RegNum.S7] = tu[idx]
                idx += 1
                self.registers[RegNum.S8] = tu[idx]
                idx += 1
                self.registers[RegNum.S9] = tu[idx]
                idx += 1
                self.registers[RegNum.S10] = tu[idx]
                idx += 1
                self.registers[RegNum.S11] = tu[idx]

    def handle_register_group_read_packet(self):
        idx = 0
        pkt = b''

        while idx < self.GDB_G_PKT_NUM_REGS:
            if idx in self.registers:
                bval = struct.pack(self.reg_fmt, self.registers[idx])
                pkt += binascii.hexlify(bval)
            else:
                # Register not in coredump -> unknown value
                # Send in "xxxxxxxx"
                pkt += b'x' * self.reg_sz

            idx += 1

        self.put_gdb_packet(pkt)

    def handle_register_single_read_packet(self, pkt):
        # Mark registers as "<unavailable>". 'p' packets are not sent for the registers
        # currently handled in this file so we can safely reply "xxxxxxxx" here.
        self.put_gdb_packet(b'x' * self.reg_sz)
