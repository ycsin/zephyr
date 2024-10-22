/*
 * Copyright (c) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>
#include <kernel_internal.h>
#include <kernel_tls.h>
#include <zephyr/app_memory/app_memdomain.h>
#include <zephyr/sys/util.h>

size_t arch_tls_stack_setup(struct k_thread *new_thread, char *stack_ptr)
{
	/*
	 * TLS area for RISC-V is simple without any extra
	 * data.
	 */

	/*
	 * Since we are populating things backwards,
	 * setup the TLS data/bss area first.
	 */
	stack_ptr -= z_tls_data_size();
	z_tls_copy(stack_ptr);

	/*
	 * Set thread TLS pointer which is used in
	 * context switch to point to TLS area.
	 */
	new_thread->tls = POINTER_TO_UINT(stack_ptr);

	/* make sure that `z_tls_current` already has the correct value when a thread starts */

	// temporarily set tp to new_thread->tls, and set the current thread

	// unsigned int k = arch_irq_lock();
	// save current tp, to be restored later
	const uintptr_t curr_tp = POINTER_TO_UINT(__builtin_thread_pointer());

	__asm__("mv tp, %0" : : "r" (new_thread->tls));
	extern Z_THREAD_LOCAL k_tid_t z_tls_current;

	z_tls_current = new_thread;

	// restore tp
	__asm__("mv tp, %0" : : "r" (curr_tp));
	// arch_irq_unlock(k);

	return z_tls_data_size();
}
