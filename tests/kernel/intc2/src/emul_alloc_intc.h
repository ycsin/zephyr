/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TESTS_KERNEL_INTC2_SRC_EMUL_ALLOC_INTC_H_
#define TESTS_KERNEL_INTC2_SRC_EMUL_ALLOC_INTC_H_

#include <zephyr/intc2.h>

#define EMUL_ALLOC_INTC_NLINES 4

/*
 * A bare, devicetree-less allocator node (design deltas D1/D2):
 * exercises the intc2_line_alloc()/intc2_msi_alloc() contract against
 * a synthetic bitmap-backed line pool, mirroring how a real vector
 * allocator (a CPU's dynamic vector pool, an MSI doorbell) has no
 * devicetree representation to allocate from either.
 */
extern const struct intc2_node emul_alloc_intc_node;

/*
 * Simulate the allocated line firing: invokes the connected ISR
 * exactly like a real hardware vector would, if the line is enabled.
 */
void emul_alloc_intc_fire(uint32_t line);

/* Free every allocated line and clear enable state, for test isolation */
void emul_alloc_intc_reset(void);

#endif /* TESTS_KERNEL_INTC2_SRC_EMUL_ALLOC_INTC_H_ */
