/*
 * Copyright (c) 2024 Meta Platforms
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/debug/symtab.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>
#include <kernel_internal.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(os, CONFIG_KERNEL_LOG_LEVEL);

uintptr_t z_riscv_get_sp_before_exc(const struct arch_esf *esf);

#define MAX_STACK_FRAMES                                                                           \
	MAX(CONFIG_EXCEPTION_STACK_TRACE_MAX_FRAMES, CONFIG_ARCH_STACKWALK_MAX_FRAMES)

struct stackframe {
	uintptr_t fp;
	uintptr_t ra;
};

static bool in_curr_thread_stack_bound(uintptr_t addr, const struct arch_esf *esf)
{
#ifdef CONFIG_THREAD_STACK_INFO
	uintptr_t start, end;

	start = _current->stack_info.start;
	end = Z_STACK_PTR_ALIGN(_current->stack_info.start + _current->stack_info.size);

	return (addr >= start) && (addr < end);
#else /* !CONFIG_THREAD_STACK_INFO */
	ARG_UNUSED(addr);
	ARG_UNUSED(esf);
	return true;
#endif /* CONFIG_THREAD_STACK_INFO */
}

static bool in_stack_bound(uintptr_t addr, const struct arch_esf *esf)
{
	uintptr_t start, end;

	if (_current == NULL || arch_is_in_isr()) {
		/* We were servicing an interrupt */
		uint8_t cpu_id = IS_ENABLED(CONFIG_SMP) ? arch_curr_cpu()->id : 0U;

		start = (uintptr_t)K_KERNEL_STACK_BUFFER(z_interrupt_stacks[cpu_id]);
		end = start + CONFIG_ISR_STACK_SIZE;
#ifdef CONFIG_USERSPACE
	} else if (((esf->mstatus & MSTATUS_MPP) == PRV_U) &&
		   ((_current->base.user_options & K_USER) != 0)) {
		/* See: zephyr/include/zephyr/arch/riscv/arch.h */
		if (IS_ENABLED(CONFIG_PMP_POWER_OF_TWO_ALIGNMENT)) {
			start = _current->arch.priv_stack_start - CONFIG_PRIVILEGED_STACK_SIZE;
			end = _current->arch.priv_stack_start;
		} else {
			start = _current->stack_info.start - CONFIG_PRIVILEGED_STACK_SIZE;
			end = _current->stack_info.start;
		}
#endif /* CONFIG_USERSPACE */
	} else {
		return in_curr_thread_stack_bound(addr, esf);
	}

	return (addr >= start) && (addr < end);
}

static inline bool in_text_region(uintptr_t addr)
{
	extern uintptr_t __text_region_start, __text_region_end;

	return (addr >= (uintptr_t)&__text_region_start) && (addr < (uintptr_t)&__text_region_end);
}

#ifdef CONFIG_FRAME_POINTER
static void walk_stackframe(bool (*vrfy_bound)(uintptr_t, const struct arch_esf *),
			    const _callee_saved_t *csf, const struct arch_esf *esf,
			    bool (*fn)(void *, unsigned long), void *arg)
{
	ARG_UNUSED(csf);
	/* make sure last_fp is smaller than fp for the first run */
	uintptr_t fp = esf->s0, last_fp = fp - 1;
	uintptr_t ra = esf->mepc;
	struct stackframe *frame;

	for (int i = 0;
	     (i < MAX_STACK_FRAMES) && (fp != 0U) && vrfy_bound(fp, esf) && (fp > last_fp);) {
		if (in_text_region(ra)) {
			if (!fn(arg, ra)) {
				break;
			}
			/*
			 * Increment the iterator only if `ra` is within the text region to get the
			 * most out of it
			 */
			i++;
		}
		frame = (struct stackframe *)fp - 1;
		ra = frame->ra;
		last_fp = fp;
		fp = frame->fp;
	}
}
#else  /* !CONFIG_FRAME_POINTER */
static void walk_stackframe(bool (*vrfy_bound)(uintptr_t, const struct arch_esf *),
			    const _callee_saved_t *csf, const struct arch_esf *esf,
			    bool (*fn)(void *, unsigned long), void *arg)
{
	ARG_UNUSED(csf);
	uintptr_t sp = z_riscv_get_sp_before_exc(esf);
	uintptr_t ra = esf->mepc;
	uintptr_t *ksp = (uintptr_t *)sp, last_ksp = (uintptr_t)ksp - 1;

	for (int i = 0; (i < MAX_STACK_FRAMES) && ((uintptr_t)ksp != 0U) &&
			vrfy_bound((uintptr_t)ksp, esf) && ((uintptr_t)ksp > last_ksp);
	     ksp++) {
		if (in_text_region(ra)) {
			if (!fn(arg, ra)) {
				break;
			}
			/*
			 * Increment the iterator only if `ra` is within the text region to get the
			 * most out of it
			 */
			i++;
		}
		ra = *ksp;
		last_ksp = (uintptr_t)ksp;
	}
}
#endif /* CONFIG_FRAME_POINTER */

void arch_stack_walk(stack_trace_callback_fn callback_fn, void *cookie,
		     const struct k_thread *thread, const struct arch_esf *esf)
{
	walk_stackframe(in_curr_thread_stack_bound, &thread->callee_saved, esf, callback_fn,
			cookie);
}

#if __riscv_xlen == 32
#define PR_REG "%08" PRIxPTR
#elif __riscv_xlen == 64
#define PR_REG "%016" PRIxPTR
#endif

#ifdef CONFIG_EXCEPTION_STACK_TRACE_SYMTAB
#define LOG_STACK_TRACE(idx, ra, name, offset)                                                     \
	LOG_ERR("     %2d: ra: " PR_REG " [%s+0x%x]", idx, ra, name, offset)
#else
#define LOG_STACK_TRACE(idx, ra, name, offset) LOG_ERR("     %2d: ra: " PR_REG, idx, ra)
#endif /* CONFIG_EXCEPTION_STACK_TRACE_SYMTAB */

static bool print_trace_address(void *arg, unsigned long ra)
{
	int *i = arg;
#ifdef CONFIG_EXCEPTION_STACK_TRACE_SYMTAB
	uint32_t offset = 0;
	const char *name = symtab_find_symbol_name(ra, &offset);
#endif

	LOG_STACK_TRACE((*i)++, ra, name, offset);

	return true;
}

void z_riscv_unwind_stack(const struct arch_esf *esf)
{
	int i = 0;

	LOG_ERR("call trace:");
	walk_stackframe(in_stack_bound, NULL, esf, print_trace_address, &i);
	LOG_ERR("");
}
