/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * intc2 nodes for the x86 IOAPIC and LOAPIC (CONFIG_INTC2_X86_APIC).
 *
 * x86 is self-dispatching: the CPU vectors through the IDT to a
 * runtime-allocated vector, so these nodes carry no intc2 software
 * dispatch table (node->table == NULL). Their connect op installs the
 * ISR through the arch's dynamic vector machinery
 * (arch_irq_connect_dynamic), and enable/disable route through
 * arch_irq_*() which the system-APIC glue steers to the IOAPIC or
 * LOAPIC. See CONFIG_INTC2_ARCH_VECTORED for how INTC2_DT_CONNECT()
 * lowers to a runtime intc2_connect_dynamic() on this arch.
 */

#include <zephyr/device.h>
#include <zephyr/intc2.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>

/*
 * x86 implements no arch_irq_is_enabled(); track the mask state here.
 * These nodes own every line handed out through them, so the shadow
 * cannot drift from the real IOAPIC/LOAPIC mask state.
 */
static uint8_t x86_apic_enabled[CONFIG_MAX_IRQ_LINES];

static void x86_apic_intc2_enable(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	if (line < CONFIG_MAX_IRQ_LINES) {
		x86_apic_enabled[line] = 1U;
	}
	arch_irq_enable(line);
}

static void x86_apic_intc2_disable(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	arch_irq_disable(line);
	if (line < CONFIG_MAX_IRQ_LINES) {
		x86_apic_enabled[line] = 0U;
	}
}

static int x86_apic_intc2_is_enabled(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	return (line < CONFIG_MAX_IRQ_LINES) ? x86_apic_enabled[line] : 0;
}

#ifdef CONFIG_INTC2_DYNAMIC
static int x86_apic_intc2_connect(const struct intc2_node *node, uint32_t line, uint32_t prio,
				  void (*isr)(const void *arg), const void *arg, uint32_t flags)
{
	ARG_UNUSED(node);

	/*
	 * Allocate a vector, install @a isr into the arch's vector
	 * dispatch table, and program the IOAPIC/LOAPIC to route @a line
	 * (the virtualized IRQ / input pin) to it.
	 */
	(void)arch_irq_connect_dynamic(line, prio, isr, arg, flags);

	return 0;
}
#endif /* CONFIG_INTC2_DYNAMIC */

static DEVICE_API(intc2, x86_apic_intc2_api) = {
	.enable = x86_apic_intc2_enable,
	.disable = x86_apic_intc2_disable,
	.is_enabled = x86_apic_intc2_is_enabled,
#ifdef CONFIG_INTC2_DYNAMIC
	.connect = x86_apic_intc2_connect,
#endif
};

#define IOAPIC_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(intel_ioapic)
#define LOAPIC_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(intel_loapic)

#if DT_NODE_EXISTS(IOAPIC_NODE)
INTC2_NODE_DT_DEFINE(IOAPIC_NODE, &x86_apic_intc2_api, NULL, CONFIG_MAX_IRQ_LINES, 0);
#endif

#if DT_NODE_EXISTS(LOAPIC_NODE)
INTC2_NODE_DT_DEFINE(LOAPIC_NODE, &x86_apic_intc2_api, NULL, CONFIG_MAX_IRQ_LINES, 0);
#endif
