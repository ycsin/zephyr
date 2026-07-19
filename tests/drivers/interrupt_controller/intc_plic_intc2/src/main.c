/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * End-to-end dispatch test for interrupts behind the PLIC: the console
 * UART's TX-ready interrupt is statically connected by the ns16550
 * driver with a multilevel-encoded IRQ number and must reach the
 * callback both on the legacy path and through the intc2 graph
 * (CPU-root chain slot -> PLIC node claim/complete dispatch).
 */

#include <zephyr/drivers/interrupt_controller/riscv_plic.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq_multilevel.h>
#include <zephyr/ztest.h>

#ifdef CONFIG_INTC2
#include <zephyr/intc2.h>
#endif

#define CONSOLE_NODE DT_CHOSEN(zephyr_console)

static const struct device *const console_dev = DEVICE_DT_GET(CONSOLE_NODE);
static volatile int tx_ready_count;
static volatile unsigned int claimed_line;

static void uart_cb(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	uart_irq_update(dev);

	if (uart_irq_tx_ready(dev)) {
		tx_ready_count++;
		claimed_line = riscv_plic_get_irq();
		uart_irq_tx_disable(dev);
	}
}

ZTEST(intc_plic_intc2, test_uart_irq_dispatch_via_plic)
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
		     "console TX interrupt did not dispatch through the PLIC");
	zassert_equal(claimed_line, DT_IRQ_BY_IDX(CONSOLE_NODE, 0, irq),
		      "claimed PLIC line %u, devicetree says %u", claimed_line,
		      DT_IRQ_BY_IDX(CONSOLE_NODE, 0, irq));
}

/* A free PLIC line, addressed the way legacy consumers do: with a
 * multilevel-encoded IRQ number built from an arithmetic expression.
 */
#define TEST_FREE_LINE 30
#define TEST_ENC_IRQ   (IRQ_TO_L2(TEST_FREE_LINE) | DT_IRQN(DT_NODELABEL(plic)))

static void enc_handler(const void *arg)
{
	ARG_UNUSED(arg);
}

ZTEST(intc_plic_intc2, test_encoded_legacy_connect)
{
	IRQ_CONNECT(TEST_ENC_IRQ, 1, enc_handler, NULL, 0);

	zassert_false(irq_is_enabled(TEST_ENC_IRQ));
	irq_enable(TEST_ENC_IRQ);
	zassert_true(irq_is_enabled(TEST_ENC_IRQ));
	irq_disable(TEST_ENC_IRQ);
	zassert_false(irq_is_enabled(TEST_ENC_IRQ));

#ifdef CONFIG_INTC2
	/* The bridge re-homes the encoded connect into the PLIC node's
	 * own dispatch table at the local line.
	 */
	const struct intc2_node *plic = INTC2_NODE_DT_GET(DT_NODELABEL(plic));
	Z_INTC2_TABLE_CONST struct intc2_entry *entry =
		z_intc2_lookup(plic, TEST_FREE_LINE);

	zassert_not_null(entry, "no table slot for the re-homed connect");
	zassert_equal_ptr(entry->isr, (void *)enc_handler,
			  "re-homed slot does not hold the connected ISR");

	/* Same hardware state through the intc2 line ops */
	struct intc2_spec spec = {.node = plic, .line = TEST_FREE_LINE};

	zassert_true(intc2_is_enabled(spec) == 0);
	intc2_enable(spec);
	zassert_true(irq_is_enabled(TEST_ENC_IRQ));
	intc2_disable(spec);
	zassert_true(intc2_is_enabled(spec) == 0);
#endif /* CONFIG_INTC2 */
}

#ifdef CONFIG_INTC2
ZTEST(intc_plic_intc2, test_console_spec_resolves_to_plic_node)
{
	struct intc2_spec spec = INTC2_DT_SPEC_GET(CONSOLE_NODE);

	zassert_equal(spec.node, INTC2_NODE_DT_GET(DT_NODELABEL(plic)),
		      "console interrupt spec does not point at the PLIC node");
	zassert_equal(spec.line, DT_IRQ_BY_IDX(CONSOLE_NODE, 0, irq));

	/* The ns16550 driver enabled its line at init via the legacy
	 * runtime API; the intc2 op must see the same hardware state.
	 */
	zassert_true(intc2_is_enabled(spec) > 0,
		     "console line not enabled on the PLIC node");
}
#endif /* CONFIG_INTC2 */

ZTEST_SUITE(intc_plic_intc2, NULL, NULL, NULL, NULL, NULL);
