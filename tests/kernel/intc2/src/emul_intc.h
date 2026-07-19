/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TESTS_KERNEL_INTC2_SRC_EMUL_INTC_H_
#define TESTS_KERNEL_INTC2_SRC_EMUL_INTC_H_

#include <zephyr/intc2.h>

#define EMUL_INTC_NLINES      8
#define EMUL_INTC_WIDE_NLINES 32

/* Emulated controller "registers", one instance per DT controller node */
struct emul_intc_regs {
	uint32_t enable;
	uint32_t pending;
	uint8_t prio[EMUL_INTC_WIDE_NLINES];
	bool inited;
	uint32_t eoi_cnt;
};

/*
 * Raise line @a line of @a node. Mimics hardware: when the line is
 * enabled, the aggregated output propagates pending up the parent edge.
 */
void emul_intc_raise(const struct intc2_node *node, uint32_t line);

static inline struct emul_intc_regs *emul_intc_regs(const struct intc2_node *node)
{
	return (struct emul_intc_regs *)node->config;
}

#endif /* TESTS_KERNEL_INTC2_SRC_EMUL_INTC_H_ */
