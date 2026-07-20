/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_INTC2_X86_IRQ_ALLOC_H_
#define ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_INTC2_X86_IRQ_ALLOC_H_

#include <zephyr/intc2.h>

/*
 * The x86 dynamic IRQ/vector pool as an intc2 INTC2_NODE_ALLOC node
 * (CONFIG_INTC2_X86_IRQ_ALLOC). Has no devicetree node: the virtual
 * IRQ space arch_irq_allocate() draws from has no hardware
 * representation to allocate from either.
 */
extern const struct intc2_node intc2_x86_irq_alloc_node;

#endif /* ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_INTC2_X86_IRQ_ALLOC_H_ */
