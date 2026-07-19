/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * intc2 root node for the ARM Cortex-M NVIC. The NVIC is hardware
 * vectored, so the node has no get_pending/eoi ops - its generated
 * dispatch table is entered directly by _isr_wrapper through the
 * bridged _sw_isr_table alias (CONFIG_INTC2_LEGACY_BRIDGE).
 */

#define DT_DRV_COMPAT arm_v7m_nvic

#include <zephyr/device.h>
#include <zephyr/intc2.h>
#include <zephyr/irq.h>

#include <cmsis_core.h>

extern void z_arm_irq_priority_set(unsigned int irq, unsigned int prio, uint32_t flags);

static void nvic_intc2_enable(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	NVIC_EnableIRQ((IRQn_Type)line);
}

static void nvic_intc2_disable(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	NVIC_DisableIRQ((IRQn_Type)line);
}

static int nvic_intc2_is_enabled(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	return NVIC_GetEnableIRQ((IRQn_Type)line);
}

static int nvic_intc2_set_priority(const struct intc2_node *node, uint32_t line,
				   uint32_t prio, uint32_t flags)
{
	ARG_UNUSED(node);

	/* the canonical Cortex-M priority mapping (ZLI offsets etc.) */
	z_arm_irq_priority_set(line, prio, flags);

	return 0;
}

static DEVICE_API(intc2, nvic_intc2_api) = {
	.enable = nvic_intc2_enable,
	.disable = nvic_intc2_disable,
	.is_enabled = nvic_intc2_is_enabled,
	.set_priority = nvic_intc2_set_priority,
};

INTC2_NODE_DT_DEFINE(DT_DRV_INST(0), &nvic_intc2_api, NULL, CONFIG_NUM_IRQS,
		     INTC2_NODE_ROOT_BRIDGE);
