/*
 * Copyright (c) 2024 Meta Platforms
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ARCH_RISCV_GDBSTUB_H_
#define ZEPHYR_INCLUDE_ARCH_RISCV_GDBSTUB_H_

#include <zephyr/arch/riscv/exception.h>

#ifndef _ASMLANGUAGE

enum RISCV_GDB_REG {
	GDB_RISCV_ZERO,
	GDB_RISCV_RA,
	GDB_RISCV_T0,
	GDB_RISCV_T1,
	GDB_RISCV_T2,
	GDB_RISCV_T3,
	GDB_RISCV_T4,
	GDB_RISCV_T5,
	GDB_RISCV_T6,
	GDB_RISCV_A0,
	GDB_RISCV_A1,
	GDB_RISCV_A2,
	GDB_RISCV_A3,
	GDB_RISCV_A4,
	GDB_RISCV_A5,
	GDB_RISCV_A6,
	GDB_RISCV_A7,
	GDB_RISCV_MEPC,
	GDB_RISCV_SP,
	GDB_RISCV_S0, /* FP */
	GDB_RISCV_S1,
	GDB_RISCV_S2,
	GDB_RISCV_S3,
	GDB_RISCV_S4,
	GDB_RISCV_S5,
	GDB_RISCV_S6,
	GDB_RISCV_S7,
	GDB_RISCV_S8,
	GDB_RISCV_S9,
	GDB_RISCV_S10,
	GDB_RISCV_S11,
	GDB_NUM_REGS
};

struct gdb_ctx {
	unsigned int exception;
	unsigned int registers[GDB_NUM_REGS];
};

void z_gdb_entry(struct arch_esf *esf, unsigned int exc_cause);

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_RISCV_GDBSTUB_H_ */
