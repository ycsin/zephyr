/*
 * Copyright (c) 2024 Meta Platforms
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief RISCV specific gdbstub interface header
 */

#ifndef ZEPHYR_INCLUDE_ARCH_RISCV_GDBSTUB_H_
#define ZEPHYR_INCLUDE_ARCH_RISCV_GDBSTUB_H_

#ifndef _ASMLANGUAGE
#include <zephyr/types.h>
#include <zephyr/toolchain.h>

/**
 * @brief RISCV register used in gdbstub
 */

enum GDB_RISCV_REGS {
	GDB_ZERO,
	GDB_RA,
	GDB_T0,
	GDB_T1,
	GDB_T2,
	GDB_T3,
	GDB_T4,
	GDB_T5,
	GDB_T6,
	GDB_A0,
	GDB_A1,
	GDB_A2,
	GDB_A3,
	GDB_A4,
	GDB_A5,
	GDB_A6,
	GDB_A7,
	GDB_MEPC,
	GDB_SP,
	GDB_S0, /* FP */
	GDB_S1,
	GDB_S2,
	GDB_S3,
	GDB_S4,
	GDB_S5,
	GDB_S6,
	GDB_S7,
	GDB_S8,
	GDB_S9,
	GDB_S10,
	GDB_S11,
	GDB_NUM_REGS,
};

struct gdb_ctx {
	unsigned int exception;
	unsigned long registers[GDB_NUM_REGS];
};

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_RISCV_GDBSTUB_H_ */
