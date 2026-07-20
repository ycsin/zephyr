/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * End-to-end dispatch test for the RISC-V APLIC (direct delivery
 * mode) as an intc2 controller node: the console UART's TX-ready
 * interrupt is statically connected by the ns16550 driver and must
 * reach the callback through the intc2 graph (CPU-root chain slot ->
 * APLIC node claim dispatch).
 */

#include <zephyr/drivers/uart.h>
#include <zephyr/ztest.h>

#ifdef CONFIG_INTC2
#include <zephyr/intc2.h>
#endif

#define CONSOLE_NODE DT_CHOSEN(zephyr_console)

static const struct device *const console_dev = DEVICE_DT_GET(CONSOLE_NODE);
static volatile int tx_ready_count;

static void uart_cb(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	uart_irq_update(dev);

	if (uart_irq_tx_ready(dev)) {
		tx_ready_count++;
		uart_irq_tx_disable(dev);
	}
}

ZTEST(intc_aplic_intc2, test_uart_irq_dispatch_via_aplic)
{
	tx_ready_count = 0;

	uart_irq_callback_set(console_dev, uart_cb);
	uart_irq_tx_enable(console_dev);

	/* TX holding register is empty almost immediately */
	for (int i = 0; (i < 100) && (tx_ready_count == 0); i++) {
		k_busy_wait(1000);
	}

	uart_irq_tx_disable(console_dev);
	uart_irq_callback_set(console_dev, NULL);

	zassert_true(tx_ready_count > 0,
		     "console TX interrupt did not dispatch through the APLIC");
}

#ifdef CONFIG_INTC2
ZTEST(intc_aplic_intc2, test_console_spec_resolves_to_aplic_node)
{
	struct intc2_spec spec = INTC2_DT_SPEC_GET(CONSOLE_NODE);

	zassert_equal(spec.node, INTC2_NODE_DT_GET(DT_NODELABEL(aplic)),
		      "console interrupt spec does not point at the APLIC node");
	zassert_equal(spec.line, DT_IRQ_BY_IDX(CONSOLE_NODE, 0, irq));

	/* The ns16550 driver enabled its line at init via the legacy
	 * runtime API; the intc2 op must see the same hardware state.
	 */
	zassert_true(intc2_is_enabled(spec) > 0,
		     "console line not enabled on the APLIC node");
}

ZTEST(intc_aplic_intc2, test_set_priority)
{
	struct intc2_spec spec = INTC2_DT_SPEC_GET(CONSOLE_NODE);

	zassert_ok(intc2_set_priority(spec, 2, 0));
	zassert_ok(intc2_set_priority(spec, DT_IRQ_BY_IDX(CONSOLE_NODE, 0, priority), 0));
}

ZTEST(intc_aplic_intc2, test_invalid_line_rejected)
{
	const struct intc2_node *aplic = INTC2_NODE_DT_GET(DT_NODELABEL(aplic));
	struct intc2_spec spec = {.node = aplic, .line = aplic->nlines};

	zassert_equal(intc2_set_priority(spec, 1, 0), -EINVAL);
}
#endif /* CONFIG_INTC2 */

ZTEST_SUITE(intc_aplic_intc2, NULL, NULL, NULL, NULL, NULL);
