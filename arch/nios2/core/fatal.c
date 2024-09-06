/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/kernel_structs.h>
#include <inttypes.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(os, CONFIG_KERNEL_LOG_LEVEL);

FUNC_NORETURN void z_nios2_fatal_error(unsigned int reason,
				       const struct arch_esf *esf)
{
#if CONFIG_EXCEPTION_DEBUG
	if (esf != NULL) {
		/* Subtract 4 from EA since we added 4 earlier so that the
		 * faulting instruction isn't retried.
		 *
		 * TODO: Only caller-saved registers get saved upon exception
		 * entry.  We may want to introduce a config option to save and
		 * dump all registers, at the expense of some stack space.
		 */
		LOG_ERR("Faulting instruction: 0x%08x", esf->instr - 4);
		LOG_ERR("  r1: 0x%08x  r2: 0x%08x  r3: 0x%08x  r4: 0x%08x",
			esf->r1, esf->r2, esf->r3, esf->r4);
		LOG_ERR("  r5: 0x%08x  r6: 0x%08x  r7: 0x%08x  r8: 0x%08x",
			esf->r5, esf->r6, esf->r7, esf->r8);
		LOG_ERR("  r9: 0x%08x r10: 0x%08x r11: 0x%08x r12: 0x%08x",
			esf->r9, esf->r10, esf->r11, esf->r12);
		LOG_ERR(" r13: 0x%08x r14: 0x%08x r15: 0x%08x  ra: 0x%08x",
			esf->r13, esf->r14, esf->r15, esf->ra);
		LOG_ERR("estatus: %08x", esf->estatus);
	}
#endif /* CONFIG_EXCEPTION_DEBUG */

	z_fatal_error(reason, esf);
	CODE_UNREACHABLE;
}

#if defined(CONFIG_EXTRA_EXCEPTION_INFO) && defined(CONFIG_LOG) &&                                 \
	defined(ALT_CPU_HAS_EXTRA_EXCEPTION_INFO)

static const char *const cause_string[26] = {
	[0] = "reset",
	[1] = "processor-only reset request",
	[2] = "interrupt",
	[3] = "trap",
	[4] = "unimplemented instruction",
	[5] = "illegal instruction",
	[6] = "misaligned data address",
	[7] = "misaligned destination address",
	[8] = "division error",
	[9] = "supervisor-only instruction address",
	[10] = "supervisor-only instruction",
	[11] = "supervisor-only data address",
	[12] = "TLB miss",
	[13] = "TLB permission violation (execute)",
	[14] = "TLB permission violation (read)",
	[15] = "TLB permission violation (write)",
	[16] = "MPU region violation (instruction)",
	[17] = "MPU region violation (data)",
	[18] = "ECC TLB error",
	[19] = "ECC fetch error (instruction)",
	[20] = "ECC register file error",
	[21] = "ECC data error",
	[22] = "ECC data cache writeback error",
	[23] = "bus instruction fetch error",
	[24] = "bus data region violation",
	[25] = "unknown",
};
static const char *cause_str(uint32_t cause_code)
{
	return cause_string[MIN(cause_code, ARRAY_SIZE(cause_string) - 1)];
}
#endif

FUNC_NORETURN void _Fault(const struct arch_esf *esf)
{
#if defined(CONFIG_LOG)
	/* Unfortunately, completely unavailable on Nios II/e cores */
#ifdef ALT_CPU_HAS_EXTRA_EXCEPTION_INFO
	uint32_t exc_reg, badaddr_reg, eccftl;
	enum nios2_exception_cause cause;

	exc_reg = z_nios2_creg_read(NIOS2_CR_EXCEPTION);

	/* Bit 31 indicates potentially fatal ECC error */
	eccftl = (exc_reg & NIOS2_EXCEPTION_REG_ECCFTL_MASK) != 0U;

	/* Bits 2-6 contain the cause code */
	cause = (exc_reg & NIOS2_EXCEPTION_REG_CAUSE_MASK)
		 >> NIOS2_EXCEPTION_REG_CAUSE_OFST;

	LOG_ERR("Exception cause: %d ECCFTL: 0x%x", cause, eccftl);
#if CONFIG_EXTRA_EXCEPTION_INFO
	LOG_ERR("reason: %s", cause_str(cause));
#endif
	if (BIT(cause) & NIOS2_BADADDR_CAUSE_MASK) {
		badaddr_reg = z_nios2_creg_read(NIOS2_CR_BADADDR);
		LOG_ERR("Badaddr: 0x%x", badaddr_reg);
	}
#endif /* ALT_CPU_HAS_EXTRA_EXCEPTION_INFO */
#endif /* CONFIG_LOG */

	z_nios2_fatal_error(K_ERR_CPU_EXCEPTION, esf);
}

#ifdef ALT_CPU_HAS_DEBUG_STUB
FUNC_NORETURN void arch_system_halt(unsigned int reason)
{
	ARG_UNUSED(reason);

	z_nios2_break();
	CODE_UNREACHABLE;
}
#endif
