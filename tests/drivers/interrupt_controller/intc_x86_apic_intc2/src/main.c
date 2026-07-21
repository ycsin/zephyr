/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * End-to-end dispatch test for the x86 IOAPIC as an intc2 node: the
 * console UART's TX-ready interrupt is connected by the ns16550 driver
 * and must reach the callback both on the legacy path and through the
 * intc2 graph. On x86 the graph "connect" lowers to a runtime
 * arch_irq_connect_dynamic() via the IOAPIC node's connect op
 * (CONFIG_INTC2_ARCH_VECTORED), and the CPU vectors through the IDT --
 * there is no intc2 software dispatch table.
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

ZTEST(intc_x86_apic_intc2, test_uart_irq_dispatch_via_ioapic)
{
	tx_ready_count = 0;

	uart_irq_callback_set(console_dev, uart_cb);
	uart_irq_tx_enable(console_dev);

	for (int i = 0; (i < 100) && (tx_ready_count == 0); i++) {
		k_busy_wait(1000);
	}

	uart_irq_tx_disable(console_dev);
	uart_irq_callback_set(console_dev, NULL);

	zassert_true(tx_ready_count > 0,
		     "console TX interrupt did not dispatch through the IOAPIC");
}

#ifdef CONFIG_INTC2
ZTEST(intc_x86_apic_intc2, test_console_spec_resolves_to_ioapic_node)
{
	struct intc2_spec spec = INTC2_DT_SPEC_GET(CONSOLE_NODE);

	zassert_equal(spec.node,
		      INTC2_NODE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(intel_ioapic)),
		      "console spec does not point at the IOAPIC intc2 node");

	/* the ns16550 driver enabled its line; the node op must agree */
	zassert_true(intc2_is_enabled(spec) > 0,
		     "console line not enabled on the IOAPIC node");

	/* enable/disable round-trip through the node */
	intc2_disable(spec);
	zassert_equal(intc2_is_enabled(spec), 0);
	intc2_enable(spec);
	zassert_true(intc2_is_enabled(spec) > 0);
}
#endif /* CONFIG_INTC2 */

ZTEST_SUITE(intc_x86_apic_intc2, NULL, NULL, NULL, NULL, NULL);
