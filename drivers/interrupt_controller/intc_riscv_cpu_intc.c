/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * intc2 root node for the RISC-V CPU interrupt controller (mie/sie
 * CSRs). The architecture's assembly dispatch reads the bridged
 * _sw_isr_table alias directly, so the node has no get_pending/eoi
 * ops (CONFIG_INTC2_LEGACY_BRIDGE).
 */

#define DT_DRV_COMPAT riscv_cpu_intc

#include <zephyr/device.h>
#include <zephyr/intc2.h>
#include <zephyr/irq.h>

static void riscv_cpu_intc2_enable(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	arch_irq_enable(line);
}

static void riscv_cpu_intc2_disable(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	arch_irq_disable(line);
}

static int riscv_cpu_intc2_is_enabled(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	return arch_irq_is_enabled(line);
}

static uint32_t riscv_cpu_intc2_line_flags(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);
	ARG_UNUSED(line);

	/* the mie/sie CSR is banked per hart */
	return INTC2_LINE_BANKED;
}

static DEVICE_API(intc2, riscv_cpu_intc2_api) = {
	.enable = riscv_cpu_intc2_enable,
	.disable = riscv_cpu_intc2_disable,
	.is_enabled = riscv_cpu_intc2_is_enabled,
	.line_flags = riscv_cpu_intc2_line_flags,
};

INTC2_NODE_DT_DEFINE(DT_DRV_INST(0), &riscv_cpu_intc2_api, NULL, CONFIG_NUM_IRQS,
		     INTC2_NODE_ROOT_BRIDGE);
