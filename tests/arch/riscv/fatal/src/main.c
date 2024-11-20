/*
 * Copyright (c) 2024 Meta Platforms
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/riscv/syscall.h>
#include <zephyr/arch/riscv/reg.h>
#include <zephyr/fatal_types.h>

/**
 * Load up a bunch of known values into registers
 * and expect them to show up in the core dump.
 * Value is register ABI name kinda spelled out,
 * followed by zeros to pad to 32 bits,
 * followed by FF00, followed by hex number of the register,
 * follwed by the "hex-coded-decimal" number of the register.
 */
int main(void)
{
	reg_write(ra, 0xDADA0000FF000101);

#ifdef WRITE_SP
	/* Skip SP because it can mess stuff up. */
	reg_write(sp, 0x0000000000000000);
#endif /* WRITE_SP */

#ifndef CONFIG_RISCV_GP
	reg_write(gp, 0xE1E10000FF000303);
#endif /* CONFIG_RISCV_GP */

#ifndef CONFIG_THREAD_LOCAL_STORAGE
	reg_write(tp, 0xE2E20000FF000404);
#endif /* CONFIG_THREAD_LOCAL_STORAGE */

	reg_write(t0, 0xD0FF0000FF000505);
	reg_write(t1, 0xD1FF0000FF000606);
	reg_write(t2, 0xD2FF0000FF000707);

#ifndef CONFIG_FRAME_POINTER
	reg_write(s0, 0xC0FF0000FF000808);
#endif /* CONFIG_FRAME_POINTER */

	reg_write(s1, 0xC1FF0000FF000909);

	reg_write(a0, 0xA0FF0000FF000A10);
	reg_write(a1, 0xA1FF0000FF000B11);
	reg_write(a2, 0xA2FF0000FF000C12);
	reg_write(a3, 0xA3FF0000FF000D13);
	reg_write(a4, 0xA4FF0000FF000E14);
	reg_write(a5, 0xA5FF0000FF000F15);

#ifndef CONFIG_RISCV_ISA_RV32E
	reg_write(a6, 0xA6FF0000FF001016);
	reg_write(a7, 0xA7FF0000FF001117);

	reg_write(s2, 0xC2FF0000FF001218);
	reg_write(s3, 0xC3FF0000FF001319);
	reg_write(s4, 0xC4FF0000FF001420);
	reg_write(s5, 0xC5FF0000FF001521);
	reg_write(s6, 0xC6FF0000FF001622);
	reg_write(s7, 0xC7FF0000FF001723);
	reg_write(s8, 0xC8FF0000FF001824);
	reg_write(s9, 0xC9FF0000FF001925);
	reg_write(s10, 0xC10FF000FF001A26);
	reg_write(s11, 0xC11FF000FF001B27);

	reg_write(t3, 0xD3FF0000FF001C28);
	reg_write(t4, 0xD4FF0000FF001D29);
	reg_write(t5, 0xD5FF0000FF001E30);
	reg_write(t6, 0xD6FF0000FF001F31);
#endif /* CONFIG_RISCV_ISA_RV32E */

#ifdef CONFIG_TEST_RISCV_FATAL_PANIC
	reg_write(a0, K_ERR_KERNEL_PANIC);
	reg_write(t0, RV_ECALL_RUNTIME_EXCEPT);
	__asm__("ecall");
#else /* CONFIG_TEST_RISCV_FATAL_ILLEGAL_INSTRUCTION */
	__asm__(
		/**
		 * 0 is an illegal instruction,
		 * put 2 copies to make it 4 bytes wide just in case.
		 */
		".insn 2, 0\n\t"
		".insn 2, 0\n\t"
		"");
#endif /* CONFIG_TEST_RISCV_FATAL_PANIC */

	return 0;
}
