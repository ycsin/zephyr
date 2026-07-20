/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Real-backing test for the intc2 x86 IRQ/vector allocator node
 * (design delta D1): intc2_line_alloc() must wrap
 * arch_irq_allocate()/irq_connect_dynamic() correctly -- distinct
 * lines per call, exhaustion once CONFIG_MAX_IRQ_LINES virtual IRQs
 * are used up, and enable/disable/is_enabled reaching the real x86
 * IRQ state. The vector-dispatch mechanism itself (a dynamically
 * connected IRQ actually firing on the allocated vector) is not
 * re-proven here: it is the same arch_irq_connect_dynamic() path
 * every interrupt on intel64 already goes through continuously.
 */

#include <zephyr/drivers/interrupt_controller/intc2_x86_irq_alloc.h>
#include <zephyr/intc2.h>
#include <zephyr/irq.h>
#include <zephyr/ztest.h>

#define ALLOC_NODE (&intc2_x86_irq_alloc_node)

static void dummy_isr(const void *arg)
{
	ARG_UNUSED(arg);
}

ZTEST(intc_x86_irq_alloc, test_node_shape)
{
	zassert_equal(ALLOC_NODE->flags & INTC2_NODE_ALLOC, INTC2_NODE_ALLOC);
	zassert_is_null(ALLOC_NODE->table, "allocator node must have no dispatch table");
}

ZTEST(intc_x86_irq_alloc, test_alloc_distinct_lines)
{
	struct intc2_spec spec_a, spec_b;

	zassert_ok(intc2_line_alloc(ALLOC_NODE, 1, 0, dummy_isr, NULL, &spec_a));
	zassert_ok(intc2_line_alloc(ALLOC_NODE, 1, 0, dummy_isr, NULL, &spec_b));

	zassert_equal(spec_a.node, ALLOC_NODE);
	zassert_equal(spec_b.node, ALLOC_NODE);
	zassert_not_equal(spec_a.line, spec_b.line);
}

ZTEST(intc_x86_irq_alloc, test_enable_disable)
{
	struct intc2_spec spec;

	/*
	 * x86 has no arch_irq_is_enabled(): the node tracks enable
	 * state itself, so intc2_is_enabled() is the only way to query
	 * it on this backing.
	 */
	zassert_ok(intc2_line_alloc(ALLOC_NODE, 1, 0, dummy_isr, NULL, &spec));

	zassert_equal(intc2_is_enabled(spec), 0);

	intc2_enable(spec);
	zassert_true(intc2_is_enabled(spec) != 0);

	intc2_disable(spec);
	zassert_equal(intc2_is_enabled(spec), 0);
}

/*
 * Named to sort last: ztest runs a suite's tests in name order, and
 * arch_irq_allocate()'s pool is a real, permanent, un-freeable
 * resource (matching hardware) -- draining it here would starve
 * every other test in this suite if it ran first.
 */
ZTEST(intc_x86_irq_alloc, test_zz_alloc_exhaustion)
{
	struct intc2_spec spec;
	int ret;
	int allocated = 0;

	/* drain the virtualized IRQ pool; some are already used by the
	 * other tests above plus whatever the kernel itself reserved.
	 */
	do {
		ret = intc2_line_alloc(ALLOC_NODE, 1, 0, dummy_isr, NULL, &spec);
		if (ret == 0) {
			allocated++;
		}
	} while ((ret == 0) && (allocated <= CONFIG_MAX_IRQ_LINES));

	zassert_equal(ret, -ENOMEM, "allocator did not report exhaustion");
}

ZTEST_SUITE(intc_x86_irq_alloc, NULL, NULL, NULL, NULL, NULL);
