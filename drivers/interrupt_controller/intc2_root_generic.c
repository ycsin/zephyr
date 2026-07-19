/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Generic intc2 CPU-root node for architectures whose interrupt
 * dispatch reads the (bridged) _sw_isr_table symbol directly and whose
 * line operations are the architecture's arch_irq_*() functions. The
 * node has no get_pending/eoi ops (CONFIG_INTC2_LEGACY_BRIDGE).
 */

#include <zephyr/device.h>
#include <zephyr/intc2.h>
#include <zephyr/irq.h>

static void intc2_root_enable(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	arch_irq_enable(line);
}

static void intc2_root_disable(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	arch_irq_disable(line);
}

static int intc2_root_is_enabled(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	return arch_irq_is_enabled(line);
}

static DEVICE_API(intc2, intc2_root_generic_api) = {
	.enable = intc2_root_enable,
	.disable = intc2_root_disable,
	.is_enabled = intc2_root_is_enabled,
};

#define INTC2_ROOT_GENERIC_DEFINE(node_id)                                                         \
	INTC2_NODE_DT_DEFINE(node_id, &intc2_root_generic_api, NULL, CONFIG_NUM_IRQS,              \
			     INTC2_NODE_ROOT_BRIDGE)

#if defined(CONFIG_INTC2_ROOT_GIC)
INTC2_ROOT_GENERIC_DEFINE(DT_INST(0, arm_gic_v1));
#elif defined(CONFIG_INTC2_ROOT_IRQMP)
INTC2_ROOT_GENERIC_DEFINE(DT_INST(0, gaisler_irqmp));
#elif defined(CONFIG_INTC2_ROOT_MIPS_CPU_INTC)
INTC2_ROOT_GENERIC_DEFINE(DT_INST(0, mti_cpu_intc));
#elif defined(CONFIG_INTC2_ROOT_OR1K_PIC)
INTC2_ROOT_GENERIC_DEFINE(DT_INST(0, opencores_or1k_pic_level));
#endif
