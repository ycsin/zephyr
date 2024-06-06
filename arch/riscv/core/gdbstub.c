/*
 * Copyright (c) 2024 Meta Platforms
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/riscv/gdbstub.h>
#include <zephyr/debug/gdbstub.h>

#include <kernel_internal.h>
#include <inttypes.h>

static struct gdb_ctx ctx;

/**
 * Currently we just handle vectors 11 and 3 but lets keep it generic
 * to be able to notify other exceptions in the future
 */
static unsigned int get_exception(unsigned int vector)
{
	unsigned int exception;

	switch (vector) {
	case RISCV_IRQ_MEXT:
		exception = GDB_EXCEPTION_BREAKPOINT;
		break;
	case RISCV_IRQ_MSOFT:
		exception = GDB_EXCEPTION_BREAKPOINT;
		break;
	default:
		exception = GDB_EXCEPTION_MEMORY_FAULT;
		break;
	}

	return exception;
}

/*
 * Debug exception handler.
 */
static void z_gdb_interrupt(unsigned int vector, struct arch_esf *esf, _callee_saved_t *csf)
{
	ctx.exception = get_exception(vector);

	ctx.registers[GDB_RA] = esf->ra;
	ctx.registers[GDB_T0] = esf->t0;
	ctx.registers[GDB_T1] = esf->t1;
	ctx.registers[GDB_T2] = esf->t2;
	ctx.registers[GDB_T3] = esf->t3;
	ctx.registers[GDB_T4] = esf->t4;
	ctx.registers[GDB_T5] = esf->t5;
	ctx.registers[GDB_T6] = esf->t6;
	ctx.registers[GDB_A0] = esf->a0;
	ctx.registers[GDB_A1] = esf->a1;
	ctx.registers[GDB_A2] = esf->a2;
	ctx.registers[GDB_A3] = esf->a3;
	ctx.registers[GDB_A4] = esf->a4;
	ctx.registers[GDB_A5] = esf->a5;
	ctx.registers[GDB_A6] = esf->a6;
	ctx.registers[GDB_A7] = esf->a7;
	ctx.registers[GDB_MEPC] = esf->mepc;
	ctx.registers[GDB_SP] = csf->sp;
	ctx.registers[GDB_S0] = csf->s0;
	ctx.registers[GDB_S1] = csf->s1;
	ctx.registers[GDB_S2] = csf->s2;
	ctx.registers[GDB_S3] = csf->s3;
	ctx.registers[GDB_S4] = csf->s4;
	ctx.registers[GDB_S5] = csf->s5;
	ctx.registers[GDB_S6] = csf->s6;
	ctx.registers[GDB_S7] = csf->s7;
	ctx.registers[GDB_S8] = csf->s8;
	ctx.registers[GDB_S9] = csf->s9;
	ctx.registers[GDB_S10] = csf->s10;
	ctx.registers[GDB_S11] = csf->s11;

	z_gdb_main_loop(&ctx);

	esf->ra = ctx.registers[GDB_RA];
	esf->t0 = ctx.registers[GDB_T0];
	esf->t1 = ctx.registers[GDB_T1];
	esf->t2 = ctx.registers[GDB_T2];
	esf->t3 = ctx.registers[GDB_T3];
	esf->t4 = ctx.registers[GDB_T4];
	esf->t5 = ctx.registers[GDB_T5];
	esf->t6 = ctx.registers[GDB_T6];
	esf->a0 = ctx.registers[GDB_A0];
	esf->a1 = ctx.registers[GDB_A1];
	esf->a2 = ctx.registers[GDB_A2];
	esf->a3 = ctx.registers[GDB_A3];
	esf->a4 = ctx.registers[GDB_A4];
	esf->a5 = ctx.registers[GDB_A5];
	esf->a6 = ctx.registers[GDB_A6];
	esf->a7 = ctx.registers[GDB_A7];
	esf->mepc = ctx.registers[GDB_MEPC];
	csf->sp = ctx.registers[GDB_SP];
	csf->s0 = ctx.registers[GDB_S0];
	csf->s1 = ctx.registers[GDB_S1];
	csf->s2 = ctx.registers[GDB_S2];
	csf->s3 = ctx.registers[GDB_S3];
	csf->s4 = ctx.registers[GDB_S4];
	csf->s5 = ctx.registers[GDB_S5];
	csf->s6 = ctx.registers[GDB_S6];
	csf->s7 = ctx.registers[GDB_S7];
	csf->s8 = ctx.registers[GDB_S8];
	csf->s9 = ctx.registers[GDB_S9];
	csf->s10 = ctx.registers[GDB_S10];
	csf->s11 = ctx.registers[GDB_S11];
}

void arch_gdb_enter(void)
{
	__asm__ volatile ("ebreak");
}

void arch_gdb_continue(void)
{
	uint32_t *pc;

	/* Adjust PC if we entered via Ctrl-C or break on init */
	if ((void *)ctx.registers[GDB_MEPC] == arch_gdb_enter) {
		pc = (uint32_t *)ctx.registers[GDB_MEPC];
		/* Check for compressed EBREAK */
		if (*pc == 0x9002) {
			ctx.registers[GDB_MEPC] += 2;
		} else {
			ctx.registers[GDB_MEPC] += 4;
		}
	}
}

void arch_gdb_step(void)
{
	uint64_t val;

	csr_write(tselect, 0);
	val = csr_read(tselect);
	if (val != 0) {
		/* trigger 0 select failed */
		return;
	}
	/* type = single step, M and U modes */
	val = (3ULL << 60) | (1ULL << 6) | (1ULL << 9);
	csr_write(tdata1, val);
}

void z_gdb_debug_isr(struct arch_esf *esf, _callee_saved_t *csf)
{
	z_gdb_interrupt(RISCV_IRQ_MEXT, esf, csf);
}

void z_gdb_break_isr(struct arch_esf *esf, _callee_saved_t *csf)
{
	z_gdb_interrupt(RISCV_IRQ_MSOFT, esf, csf);
}

void arch_gdb_init(void)
{
	/* enable debug triggers in M mode */
	csr_write(tcontrol, (1ULL << 3));
}

size_t arch_gdb_reg_readall(struct gdb_ctx *ctx_ptr, uint8_t *buf, size_t buflen)
{
	size_t ret;

	if (buflen < (sizeof(ctx_ptr->registers) * 2)) {
		ret = 0;
	} else {
		ret = bin2hex((const uint8_t *)&(ctx_ptr->registers),
			      sizeof(ctx_ptr->registers), buf, buflen);
	}

	return ret;
}

size_t arch_gdb_reg_writeall(struct gdb_ctx *ctx_ptr, uint8_t *hex, size_t hexlen)
{
	size_t ret;

	if (hexlen != (sizeof(ctx_ptr->registers) * 2)) {
		ret = 0;
	} else {
		ret = hex2bin(hex, hexlen,
			      (uint8_t *)&(ctx_ptr->registers),
			      sizeof(ctx_ptr->registers));
	}

	return ret;
}

size_t arch_gdb_reg_readone(struct gdb_ctx *ctx_ptr, uint8_t *buf, size_t buflen,
			    uint32_t regno)
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
		ret = bin2hex((const uint8_t *)&(ctx_ptr->registers[regno]),
			      sizeof(ctx_ptr->registers[regno]),
			      buf, buflen);
	}

	return ret;
}

size_t arch_gdb_reg_writeone(struct gdb_ctx *ctx_ptr, uint8_t *hex, size_t hexlen,
			     uint32_t regno)
{
	size_t ret;

	if (regno >= GDB_NUM_REGS) {
		ret = 0;
	} else if (hexlen != (sizeof(unsigned int) * 2)) {
		/* Make sure the input hex string matches register size */
		ret = 0;
	} else {
		ret = hex2bin(hex, hexlen,
			      (uint8_t *)&(ctx_ptr->registers[regno]),
			      sizeof(ctx_ptr->registers[regno]));
	}

	return ret;
}
