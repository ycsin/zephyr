/*
 * Copyright (c) 2021 Facebook, Inc. and its affiliates
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/debug/coredump.h>

#define ARCH_HDR_VER 1

#ifdef CONFIG_64BIT
#define RV_REG_T uint64_t
#else /* 32BIT */
#define RV_REG_T uint32_t
#endif /* CONFIG_64BIT */

#ifdef CONFIG_RISCV_ISA_RV32E
#define RV_E(reg) RV_REG_T reg
#define RV_I(reg) /* unavailable */
#else
#define RV_E(reg) RV_REG_T reg
#define RV_I(reg) RV_REG_T reg
#endif /* CONFIG_RISCV_ISA_RV32E */

enum riscv_cpu_code {
	RISCV_CPU_RV64,
	RISCV_CPU_RV32,
	RISCV_CPU_RV32E,
};

uintptr_t z_riscv_get_sp_before_exc(const struct arch_esf *esf);

struct riscv_arch_block {
	uint8_t cpu;
	union {
		uint8_t flags;
		struct {
			uint8_t extra_exception_info: 1;
			uint8_t unused: 7;
		};
	};

	struct {
		RV_E(ra);
		RV_E(tp);
		RV_E(t0);
		RV_E(t1);
		RV_E(t2);
		RV_E(a0);
		RV_E(a1);
		RV_E(a2);
		RV_E(a3);
		RV_E(a4);
		RV_E(a5);
		RV_I(a6);
		RV_I(a7);
		RV_I(t3);
		RV_I(t4);
		RV_I(t5);
		RV_I(t6);
		RV_E(pc);
		RV_E(sp);
#ifdef CONFIG_EXTRA_EXCEPTION_INFO
		RV_E(s0);
		RV_E(s1);
		RV_I(s2);
		RV_I(s3);
		RV_I(s4);
		RV_I(s5);
		RV_I(s6);
		RV_I(s7);
		RV_I(s8);
		RV_I(s9);
		RV_I(s10);
		RV_I(s11);
#endif /* CONFIG_EXTRA_EXCEPTION_INFO */
	} r;
} __packed;

/*
 * This might be too large for stack space if defined
 * inside function. So do it here.
 */
static struct riscv_arch_block arch_blk;

void arch_coredump_info_dump(const struct arch_esf *esf)
{
	struct coredump_arch_hdr_t hdr = {
		.id = COREDUMP_ARCH_HDR_ID,
		.hdr_version = ARCH_HDR_VER,
		.num_bytes = sizeof(arch_blk),
	};

	/* Nothing to process */
	if (esf == NULL) {
		return;
	}

	(void)memset(&arch_blk, 0, sizeof(arch_blk));

	sizeof(arch_blk.r);

	if (IS_ENABLED(CONFIG_64BIT)) {
		arch_blk.cpu = RISCV_CPU_RV64;
	} else if (IS_ENABLED(CONFIG_RISCV_ISA_RV32I)) {
		arch_blk.cpu = RISCV_CPU_RV32;
	} else {
		arch_blk.cpu = RISCV_CPU_RV32E;
	}

	arch_blk.extra_exception_info = IS_ENABLED(CONFIG_EXTRA_EXCEPTION_INFO);

	/*
	 * 33 registers expected by GDB. Not all are in ESF but the GDB stub will need
	 * to send all 33 as one packet. The stub will need to send undefined for
	 * registers not presented in coredump.
	 */
	arch_blk.r.ra = esf->ra;
	arch_blk.r.t0 = esf->t0;
	arch_blk.r.t1 = esf->t1;
	arch_blk.r.t2 = esf->t2;
	arch_blk.r.a0 = esf->a0;
	arch_blk.r.a1 = esf->a1;
	arch_blk.r.a2 = esf->a2;
	arch_blk.r.a3 = esf->a3;
	arch_blk.r.a4 = esf->a4;
	arch_blk.r.a5 = esf->a5;
#if !defined(CONFIG_RISCV_ISA_RV32E)
	arch_blk.r.t3 = esf->t3;
	arch_blk.r.t4 = esf->t4;
	arch_blk.r.t5 = esf->t5;
	arch_blk.r.t6 = esf->t6;
	arch_blk.r.a6 = esf->a6;
	arch_blk.r.a7 = esf->a7;
#endif /* !CONFIG_RISCV_ISA_RV32E */
	arch_blk.r.pc = esf->mepc;
	arch_blk.r.sp = z_riscv_get_sp_before_exc(esf);

#ifdef CONFIG_EXTRA_EXCEPTION_INFO
	if (esf->csf != NULL) {
		_callee_saved_t *csf = esf->csf;

		arch_blk.r.s0 = csf->s0;
		arch_blk.r.s1 = csf->s1;
#ifndef CONFIG_RISCV_ISA_RV32E
		arch_blk.r.s2 = csf->s2;
		arch_blk.r.s3 = csf->s3;
		arch_blk.r.s4 = csf->s4;
		arch_blk.r.s5 = csf->s5;
		arch_blk.r.s6 = csf->s6;
		arch_blk.r.s7 = csf->s7;
		arch_blk.r.s8 = csf->s8;
		arch_blk.r.s9 = csf->s9;
		arch_blk.r.s10 = csf->s10;
		arch_blk.r.s11 = csf->s11;
#endif /* CONFIG_RISCV_ISA_RV32E */
	}
#endif /* CONFIG_EXTRA_EXCEPTION_INFO */

	/* Send for output */
	coredump_buffer_output((uint8_t *)&hdr, sizeof(hdr));
	coredump_buffer_output((uint8_t *)&arch_blk, sizeof(arch_blk));
}

uint16_t arch_coredump_tgt_code_get(void)
{
	return COREDUMP_TGT_RISC_V;
}

#if defined(CONFIG_DEBUG_COREDUMP_DUMP_THREAD_PRIV_STACK)
void arch_coredump_priv_stack_dump(struct k_thread *thread)
{
	uintptr_t start_addr, end_addr;

	/* See: zephyr/include/zephyr/arch/riscv/arch.h */
	if (IS_ENABLED(CONFIG_PMP_POWER_OF_TWO_ALIGNMENT)) {
		start_addr = thread->arch.priv_stack_start + Z_RISCV_STACK_GUARD_SIZE;
	} else {
		start_addr = thread->stack_info.start - CONFIG_PRIVILEGED_STACK_SIZE;
	}
	end_addr = Z_STACK_PTR_ALIGN(thread->arch.priv_stack_start + K_KERNEL_STACK_RESERVED +
				     CONFIG_PRIVILEGED_STACK_SIZE);

	coredump_memory_dump(start_addr, end_addr);
}
#endif /* CONFIG_DEBUG_COREDUMP_DUMP_THREAD_PRIV_STACK */
