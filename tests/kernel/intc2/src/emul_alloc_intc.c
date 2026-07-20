/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "emul_alloc_intc.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

static struct intc2_entry emul_alloc_table[EMUL_ALLOC_INTC_NLINES];
static uint32_t emul_alloc_used;
static uint32_t emul_alloc_enabled;

static void emul_alloc_enable(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	emul_alloc_enabled |= BIT(line);
}

static void emul_alloc_disable(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	emul_alloc_enabled &= ~BIT(line);
}

static int emul_alloc_is_enabled(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(node);

	return (emul_alloc_enabled & BIT(line)) != 0U;
}

static int emul_alloc_alloc(const struct intc2_node *node, uint32_t prio, uint32_t flags,
			    void (*isr)(const void *arg), const void *arg,
			    struct intc2_spec *spec)
{
	uint32_t free_mask = (~emul_alloc_used) & BIT_MASK(EMUL_ALLOC_INTC_NLINES);
	uint32_t line;

	ARG_UNUSED(prio);
	ARG_UNUSED(flags);

	if (free_mask == 0U) {
		return -ENOMEM;
	}

	line = (uint32_t)(find_lsb_set(free_mask) - 1);
	emul_alloc_used |= BIT(line);
	emul_alloc_table[line].isr = isr;
	emul_alloc_table[line].arg = arg;

	spec->node = node;
	spec->line = line;

	return 0;
}

#ifdef CONFIG_INTC2_MSI
static int emul_alloc_msi_alloc(const struct intc2_node *node, uint32_t prio, uint32_t flags,
				void (*isr)(const void *arg), const void *arg,
				struct intc2_spec *spec, struct intc2_msi *msg)
{
	int ret = emul_alloc_alloc(node, prio, flags, isr, arg, spec);

	if (ret != 0) {
		return ret;
	}

	/*
	 * Synthetic message target in the x86 MSI address/data shape
	 * (pcie_msi_map()/pcie_msi_mdr()); there is no real doorbell
	 * behind this emulated node.
	 */
	msg->address = 0xfee00000ULL | ((uint64_t)spec->line << 12);
	msg->data = 0x4000U | spec->line;

	return 0;
}
#endif /* CONFIG_INTC2_MSI */

static DEVICE_API(intc2, emul_alloc_api) = {
	.enable = emul_alloc_enable,
	.disable = emul_alloc_disable,
	.is_enabled = emul_alloc_is_enabled,
	.alloc = emul_alloc_alloc,
#ifdef CONFIG_INTC2_MSI
	.msi_alloc = emul_alloc_msi_alloc,
#endif
};

const struct intc2_node emul_alloc_intc_node = {
	.api = &emul_alloc_api,
	.config = NULL,
	.table = NULL,
	.lines = NULL,
	.nlines = EMUL_ALLOC_INTC_NLINES,
	.flags = INTC2_NODE_ALLOC,
	.parent = {NULL, 0},
};

void emul_alloc_intc_fire(uint32_t line)
{
	struct intc2_entry *entry = &emul_alloc_table[line];

	if (((emul_alloc_enabled & BIT(line)) == 0U) || (entry->isr == NULL)) {
		return;
	}

	entry->isr(entry->arg);
}

void emul_alloc_intc_reset(void)
{
	emul_alloc_used = 0;
	emul_alloc_enabled = 0;
	memset(emul_alloc_table, 0, sizeof(emul_alloc_table));
}
