/*
 * Copyright (c) 2024 Meta Platforms.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>

#include <kernel_internal.h>

#include <zephyr/arch/riscv/exception.h>
#include <zephyr/debug/gdbstub.h>
#include <zephyr/kernel.h>

extern struct gdb_ctx debug_ctx;

void arch_gdb_continue(void)
{
	/* ? */
}

void arch_gdb_step(void)
{
	/* ? */
}

size_t arch_gdb_reg_readall(struct gdb_ctx *ctx, uint8_t *buf, size_t buflen)
{
	size_t ret;

	if (buflen < (sizeof(ctx->registers) * 2)) {
		ret = 0;
	} else {
		ret = bin2hex((const uint8_t *)&(ctx->registers), sizeof(ctx->registers), buf,
			      buflen);
	}

	return ret;
}

size_t arch_gdb_reg_writeall(struct gdb_ctx *ctx, uint8_t *hex, size_t hexlen)
{
	size_t ret;

	if (hexlen != (sizeof(ctx->registers) * 2)) {
		ret = 0;
	} else {
		ret = hex2bin(hex, hexlen, (uint8_t *)&(ctx->registers), sizeof(ctx->registers));
	}

	return ret;
}

size_t arch_gdb_reg_readone(struct gdb_ctx *ctx, uint8_t *buf, size_t buflen, uint32_t regno)
{
	size_t ret;

	if (buflen < (sizeof(unsigned int) * 2)) {
		/* Make sure there is enough space to write hex string */
		ret = 0;
	} else if (regno >= GDB_NUM_REGS) {
		/* Return hex string "xx" to tell GDB that this register
		 * is not available. So GDB will continue probing other
		 * registers instead of stopping in the middle of
		 * "info registers all".
		 */
		if (buflen >= 2) {
			memcpy(buf, "xx", 2);
			ret = 2;
		} else {
			ret = 0;
		}
	} else {
		ret = bin2hex((const uint8_t *)&(ctx->registers[regno]),
			      sizeof(ctx->registers[regno]), buf, buflen);
	}

	return ret;
}

size_t arch_gdb_reg_writeone(struct gdb_ctx *ctx, uint8_t *hex, size_t hexlen, uint32_t regno)
{
	size_t ret;

	if (regno >= GDB_NUM_REGS) {
		ret = 0;
	} else if (hexlen != (sizeof(unsigned int) * 2)) {
		/* Make sure the input hex string matches register size */
		ret = 0;
	} else {
		ret = hex2bin(hex, hexlen, (uint8_t *)&(ctx->registers[regno]),
			      sizeof(ctx->registers[regno]));
	}

	return ret;
}

int arch_gdb_add_breakpoint(struct gdb_ctx *ctx, uint8_t type, uintptr_t addr, uint32_t kind)
{
	int ret;

	switch (type) {
	case 1:
		/* Hardware breakpoint */
		ret = -1;
		break;
	case 0:
		/* Software breakpoint */
		ret = -2;
		break;
	default:
		/* Breakpoint type not supported */
		ret = -2;
		break;
	}

	return ret;
}

int arch_gdb_remove_breakpoint(struct gdb_ctx *ctx, uint8_t type, uintptr_t addr, uint32_t kind)
{
	int ret;

	switch (type) {
	case 1:
		/* Hardware breakpoint */
		ret = -1;
		break;
	case 0:
		/* Software breakpoint */
		ret = -2;
		break;
	default:
		/* Breakpoint type not supported */
		ret = -2;
		break;
	}

	return ret;
}

void arch_gdb_init(void)
{
	/* ? */
}
