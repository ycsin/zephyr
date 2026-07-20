/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * intc2 allocator node (design delta D1) wrapping x86's native dynamic
 * IRQ allocator: arch_irq_allocate() mints a fresh virtual IRQ number
 * and irq_connect_dynamic() allocates the hardware vector and installs
 * the ISR directly into the arch's own vector-dispatch table. The
 * node has no dispatch table of its own -- x86 vectors to the ISR
 * without any intc2-side lookup, exactly as it does today; it exists
 * so that consumers written against intc2_line_alloc() get a uniform
 * (node, line) spec and the standard enable/disable/is_enabled ops
 * instead of calling the arch allocator directly.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/interrupt_controller/intc2_x86_irq_alloc.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

/*
 * x86 does not implement arch_irq_is_enabled(), so enable state is
 * tracked here; this node is the sole owner of every line it hands
 * out, so the shadow can never drift from the real IDT/vector state.
 */
static uint8_t x86_irq_alloc_enabled[CONFIG_MAX_IRQ_LINES];

static void x86_irq_alloc_enable(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	x86_irq_alloc_enabled[line] = 1U;
	irq_enable(line);
}

static void x86_irq_alloc_disable(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	irq_disable(line);
	x86_irq_alloc_enabled[line] = 0U;
}

static int x86_irq_alloc_is_enabled(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	return x86_irq_alloc_enabled[line];
}

static int x86_irq_alloc_alloc(const struct intc2_node *node, uint32_t prio, uint32_t flags,
			       void (*isr)(const void *arg), const void *arg,
			       struct intc2_spec *spec)
{
	unsigned int irq = arch_irq_allocate();

	if (irq == UINT_MAX) {
		return -ENOMEM;
	}

	(void)irq_connect_dynamic(irq, prio, isr, arg, flags);

	spec->node = node;
	spec->line = irq;

	return 0;
}

static DEVICE_API(intc2, x86_irq_alloc_api) = {
	.enable = x86_irq_alloc_enable,
	.disable = x86_irq_alloc_disable,
	.is_enabled = x86_irq_alloc_is_enabled,
	.alloc = x86_irq_alloc_alloc,
};

const struct intc2_node intc2_x86_irq_alloc_node = {
	.api = &x86_irq_alloc_api,
	.config = NULL,
	.table = NULL,
	.lines = NULL,
	.nlines = CONFIG_MAX_IRQ_LINES,
	.flags = INTC2_NODE_ALLOC,
	.parent = {NULL, 0},
};
