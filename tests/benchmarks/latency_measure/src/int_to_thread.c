/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 * Copyright (c) 2017, 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief Measure time from ISR back to interrupted thread
 *
 * This file covers three interrupt to threads scenarios:
 *  1. ISR returning to the interrupted kernel thread
 *  2. ISR returning to a different (kernel) thread
 *  3. ISR returning to a different (user) thread
 *
 * In all three scenarios, the source of the ISR is a software generated
 * interrupt originating from a kernel thread. Ideally, these tests would
 * also cover the scenarios where the interrupted thread is a user thread.
 * However, some implementations of the irq_offload() routine lock interrupts,
 * which is not allowed in userspace.
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/irq_multilevel.h>
#include "utils.h"
#include "timing_sc.h"

#include <zephyr/irq_offload.h>

// #define _INT_OFFLOAD
// #define _INT_MSIP
#define _INT_PLIC

const uint32_t local_irq = 11;
const uint32_t irqn = IRQ_TO_L2(local_irq) | RISCV_IRQ_MSOFT;

static K_SEM_DEFINE(isr_sem, 0, 1);
#define MSIP_BASE 0x2000000UL
#define MSIP(hartid) ((volatile uint32_t *)MSIP_BASE)[hartid]

#define DT_DRV_COMPAT	sifive_plic_1_0_0
#define PLIC_BASE DT_INST_REG_ADDR(0)
#define IRQ_REG(n) (n >> 5)

#define REG_PRIORITY(irq)     (PLIC_BASE + 0x0 + (irq << 2))
#define REG_PENDING(irq)      (PLIC_BASE + 0x1000 + (IRQ_REG(irq) << 2))
#define REG_ENABLE(hart, irq) (PLIC_BASE + 0x2000 + (hart << 7) + IRQ_REG(irq))
#define REG_CLAIM(hart)       (PLIC_BASE + 0x200004 + (hart << 12))

/**
 * @brief Test ISR used to measure time to return to thread
 *
 * The interrupt handler gets the first timestamp used in the test.
 * It then copies the timetsamp into a message queue and returns.
 */
static void test_isr(const void *arg)
{
	struct k_sem *sem = (struct k_sem *)arg;

	if (arg != NULL) {
		k_sem_give(sem);
	}

	timestamp.sample = timing_timestamp_get();
	printk("signaled\n");
#ifdef _INT_MSIP
	/* Clear MSIP */
	MSIP(0) = 0;
#endif
}

__maybe_unused
static void trig_msip(const void *arg)
{
	irq_connect_dynamic(RISCV_IRQ_MSOFT, 0, test_isr, arg, 0);
	/* Trigger MSIP */
	MSIP(0) = 1;
}

__maybe_unused
static void trig_plic(const void *arg)
{
	uint32_t pend;

	pend = sys_read32(REG_PENDING(local_irq));
	printk("initial pend %X\n", pend);
	pend |= BIT(local_irq);
	printk("write pend %X to %p\n", pend, (void *)REG_PENDING(local_irq));
	sys_write32(pend, REG_PENDING(local_irq));
}

/**
 * @brief Measure time to return from interrupt
 *
 * This function is used to measure the time it takes to return from an
 * interrupt.
 */
static void int_to_interrupted_thread(uint32_t num_iterations, uint64_t *sum)
{
	timing_t  start;
	timing_t  finish;

	*sum = 0ull;

#ifdef _INT_PLIC
	irq_connect_dynamic(irqn, 1, test_isr, NULL, 0);
#endif

	for (uint32_t i = 0; i < num_iterations; i++) {
		// printk("%u trig\n", i);
#ifdef _INT_MSIP
		trig_msip(NULL);
#elif defined(_INT_PLIC)
		/* set PLIC pending */
		trig_plic(NULL);
#else
		irq_offload(test_isr, NULL);
#endif

		finish = timing_timestamp_get();
		start = timestamp.sample;

		*sum += timing_cycles_get(&start, &finish);
	}
}

static void start_thread_entry(void *p1, void *p2, void *p3)
{
	uint32_t      num_iterations = (uint32_t)(uintptr_t)p1;
	struct k_sem *sem = p2;

	ARG_UNUSED(p3);

	uint64_t  sum = 0ull;
	timing_t  start;
	timing_t  finish;

	/* Ensure that <isr_sem> is unavailable */

	(void) k_sem_take(sem, K_NO_WAIT);
	k_thread_start(&alt_thread);

	for (uint32_t i = 0; i < num_iterations; i++) {

		/* 1. Wait on an unavailable semaphore */

		k_sem_take(sem, K_FOREVER);

		/* 3. Obtain the start and finish timestamps */

		finish = timing_timestamp_get();
		start = timestamp.sample;

		sum += timing_cycles_get(&start, &finish);
	}

	timestamp.cycles = sum;
}

static void alt_thread_entry(void *p1, void *p2, void *p3)
{
	uint32_t      num_iterations = (uint32_t)(uintptr_t)p1;
	struct k_sem *sem = p2;

	ARG_UNUSED(p3);

#ifdef _INT_PLIC
	irq_connect_dynamic(irqn, 1, test_isr, sem, 0);
#endif

	for (uint32_t i = 0; i < num_iterations; i++) {

		/* 2. Trigger the test_isr() to execute */

#ifdef _INT_MSIP
		trig_msip(sem);
#elif defined(_INT_PLIC)
		trig_plic(sem);
#else
		irq_offload(test_isr, sem);
#endif

		/*
		 * ISR expected to have awakened higher priority start_thread
		 * thereby preempting alt_thread.
		 */
	}

	k_thread_join(&start_thread, K_FOREVER);
}

static void int_to_another_thread(uint32_t num_iterations, uint64_t *sum,
				  uint32_t options)
{
	int  priority;
	*sum = 0ull;

	priority = k_thread_priority_get(k_current_get());

	k_thread_create(&start_thread, start_stack,
			K_THREAD_STACK_SIZEOF(start_stack),
			start_thread_entry,
			(void *)(uintptr_t)num_iterations, &isr_sem, NULL,
			priority - 2, options, K_FOREVER);

	k_thread_create(&alt_thread, alt_stack,
			K_THREAD_STACK_SIZEOF(alt_stack),
			alt_thread_entry,
			(void *)(uintptr_t)num_iterations, &isr_sem, NULL,
			priority - 1, 0, K_FOREVER);

#if CONFIG_USERSPACE
	if (options != 0) {
		k_thread_access_grant(&start_thread, &isr_sem, &alt_thread);
	}
#endif

	k_thread_start(&start_thread);

	k_thread_join(&alt_thread, K_FOREVER);

	*sum = timestamp.cycles;
}

static void isr_blah(const void *arg)
{
	printk("blah\n");
}

/**
 *
 * @brief The test main function
 *
 * @return 0 on success
 */
int int_to_thread(uint32_t num_iterations)
{
	uint64_t sum;
	char description[120];

#ifndef _INT_DEFAULT
	irq_enable(RISCV_IRQ_MSOFT);
#endif

#ifdef _INT_PLIC
	irq_connect_dynamic(irqn, 1, isr_blah, NULL, 0);
	irq_enable(irqn);
#endif

	timing_start();
	TICK_SYNCH();

	int_to_interrupted_thread(num_iterations, &sum);

	sum -= timestamp_overhead_adjustment(0, 0);

	snprintf(description, sizeof(description),
		 "%-40s - Return from ISR to interrupted thread",
		 "isr.resume.interrupted.thread.kernel");
	PRINT_STATS_AVG(description, (uint32_t)sum, num_iterations, false, "");

	/* ************** */

	int_to_another_thread(num_iterations, &sum, 0);

	sum -= timestamp_overhead_adjustment(0, 0);

	snprintf(description, sizeof(description),
		 "%-40s - Return from ISR to another thread",
		 "isr.resume.different.thread.kernel");
	PRINT_STATS_AVG(description, (uint32_t)sum, num_iterations, false, "");

	/* ************** */

#if CONFIG_USERSPACE
	int_to_another_thread(num_iterations, &sum, K_USER);

	sum -= timestamp_overhead_adjustment(0, K_USER);

	snprintf(description, sizeof(description),
		 "%-40s - Return from ISR to another thread",
		 "isr.resume.different.thread.user");
	PRINT_STATS_AVG(description, (uint32_t)sum, num_iterations, false, "");
#endif

	timing_stop();
	return 0;
}
