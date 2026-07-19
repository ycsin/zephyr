/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "emul_intc.h"

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

static void emul_intc_enable(const struct intc2_node *node, uint32_t line)
{
	emul_intc_regs(node)->enable |= BIT(line);
}

static void emul_intc_disable(const struct intc2_node *node, uint32_t line)
{
	emul_intc_regs(node)->enable &= ~BIT(line);
}

static int emul_intc_is_enabled(const struct intc2_node *node, uint32_t line)
{
	return (emul_intc_regs(node)->enable & BIT(line)) != 0U;
}

static int emul_intc_set_priority(const struct intc2_node *node, uint32_t line,
				  uint32_t prio, uint32_t flags)
{
	ARG_UNUSED(flags);

	if (line >= node->nlines) {
		return -EINVAL;
	}

	emul_intc_regs(node)->prio[line] = (uint8_t)prio;

	return 0;
}

static int32_t emul_intc_get_pending(const struct intc2_node *node)
{
	struct emul_intc_regs *regs = emul_intc_regs(node);
	uint32_t active = regs->pending & regs->enable;

	if (active == 0U) {
		return -1;
	}

	/* claim the highest-numbered active line */
	{
		uint32_t line = (uint32_t)(find_msb_set(active) - 1);

		regs->pending &= ~BIT(line);
		return (int32_t)line;
	}
}

static void emul_intc_eoi(const struct intc2_node *node, uint32_t line)
{
	ARG_UNUSED(line);

	emul_intc_regs(node)->eoi_cnt++;
}

#ifdef CONFIG_INTC2_AFFINITY
static int emul_intc_set_affinity(const struct intc2_node *node, uint32_t line, uint32_t cpumask)
{
	if (line >= node->nlines) {
		return -EINVAL;
	}

	if ((cpumask & ~(uint32_t)BIT_MASK(CONFIG_MP_MAX_NUM_CPUS)) != 0U) {
		return -EINVAL;
	}

	emul_intc_regs(node)->cpumask[line] = cpumask;

	return 0;
}

static int emul_intc_get_affinity(const struct intc2_node *node, uint32_t line, uint32_t *cpumask)
{
	if (line >= node->nlines) {
		return -EINVAL;
	}

	*cpumask = emul_intc_regs(node)->cpumask[line];

	return 0;
}
#endif /* CONFIG_INTC2_AFFINITY */

static void emul_intc_init(const struct intc2_node *node)
{
	struct emul_intc_regs *regs = emul_intc_regs(node);

#ifdef CONFIG_INTC2_AFFINITY
	for (uint32_t line = 0; line < node->nlines; line++) {
		regs->cpumask[line] = CONFIG_INTC2_AFFINITY_DEFAULT_MASK;
	}
#endif

	regs->inited = true;
}

static DEVICE_API(intc2, emul_intc_api) = {
	.enable = emul_intc_enable,
	.disable = emul_intc_disable,
	.is_enabled = emul_intc_is_enabled,
	.set_priority = emul_intc_set_priority,
#ifdef CONFIG_INTC2_AFFINITY
	.set_affinity = emul_intc_set_affinity,
	.get_affinity = emul_intc_get_affinity,
#endif
	.get_pending = emul_intc_get_pending,
	.eoi = emul_intc_eoi,
	.init = emul_intc_init,
};

/* Fixed-routing variant so tests cover the -ENOTSUP affinity path */
static DEVICE_API(intc2, emul_intc_fixed_api) = {
	.enable = emul_intc_enable,
	.disable = emul_intc_disable,
	.is_enabled = emul_intc_is_enabled,
	.set_priority = emul_intc_set_priority,
	.get_pending = emul_intc_get_pending,
	.eoi = emul_intc_eoi,
	.init = emul_intc_init,
};

void emul_intc_raise(const struct intc2_node *node, uint32_t line)
{
	struct emul_intc_regs *regs = emul_intc_regs(node);

	regs->pending |= BIT(line);

	/* aggregated output asserts the parent line when unmasked */
	if (((regs->pending & regs->enable) != 0U) && (node->parent.node != NULL)) {
		emul_intc_raise(node->parent.node, node->parent.line);
	}
}

/* Routable nodes carry the multi-target capability class */
#define EMUL_INTC_NODE_FLAGS                                                                       \
	(IS_ENABLED(CONFIG_INTC2_AFFINITY) ? INTC2_NODE_AFFINITY_MULTI_TARGET : 0)

#define EMUL_INTC_DEFINE_N(node_id, nlines, api_, flags_)                                          \
	static struct emul_intc_regs _CONCAT(emul_regs_, DT_DEP_ORD(node_id));                     \
	INTC2_NODE_DT_DEFINE(node_id, api_, &_CONCAT(emul_regs_, DT_DEP_ORD(node_id)),             \
			     nlines, flags_);

#define EMUL_INTC_DEFINE(node_id)                                                                  \
	EMUL_INTC_DEFINE_N(node_id, EMUL_INTC_NLINES, &emul_intc_api, EMUL_INTC_NODE_FLAGS)
#define EMUL_INTC_FIXED_DEFINE(node_id)                                                            \
	EMUL_INTC_DEFINE_N(node_id, EMUL_INTC_NLINES, &emul_intc_fixed_api, 0)
#define EMUL_INTC_WIDE_DEFINE(node_id)                                                             \
	EMUL_INTC_DEFINE_N(node_id, EMUL_INTC_WIDE_NLINES, &emul_intc_api, EMUL_INTC_NODE_FLAGS)

DT_FOREACH_STATUS_OKAY(vnd_intc2_emul, EMUL_INTC_DEFINE)
DT_FOREACH_STATUS_OKAY(vnd_intc2_emul_l2, EMUL_INTC_FIXED_DEFINE)
DT_FOREACH_STATUS_OKAY(vnd_intc2_emul_wide, EMUL_INTC_WIDE_DEFINE)
