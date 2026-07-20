/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * End-to-end dispatch test for SPIs behind the GICv3: a free SPI is
 * connected with the legacy IRQ_CONNECT() and raised by software via
 * GICD_ISPENDR, and must reach the handler both on the legacy path
 * and through the bridged intc2 root table. On SMP targets the
 * runtime affinity ops route the line to each CPU in turn.
 */

#include <zephyr/drivers/interrupt_controller/gic.h>
#include <zephyr/dt-bindings/interrupt-controller/arm-gic.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#ifdef CONFIG_INTC2
#include <zephyr/intc2.h>
#endif

/* INTID of a free SPI on the qemu virt machine (SPI 10) */
#define TEST_SPI_INTID 42U

/* A banked PPI line for the -ENOTSUP affinity checks (virtual timer) */
#define TEST_PPI_INTID 27U

static volatile int spi_count;
static volatile int handled_cpu;

static void spi_handler(const void *arg)
{
	ARG_UNUSED(arg);

	spi_count++;
	handled_cpu = (int)arch_curr_cpu()->id;
}

/* Software-raise the test SPI and wait for it to dispatch */
static void pulse_spi(void)
{
	spi_count = 0;

	sys_write32(BIT(TEST_SPI_INTID % 32),
		    GICD_ISPENDRn + (TEST_SPI_INTID / 32) * 4);

	for (int i = 0; (i < 100) && (spi_count == 0); i++) {
		k_busy_wait(1000);
	}
}

ZTEST(intc_gicv3_intc2, test_spi_dispatch)
{
	zassert_false(irq_is_enabled(TEST_SPI_INTID));
	irq_enable(TEST_SPI_INTID);
	zassert_true(irq_is_enabled(TEST_SPI_INTID));

	pulse_spi();

	zassert_true(spi_count > 0, "SPI did not dispatch");

#ifdef CONFIG_INTC2
	/* The bridge holds the connected ISR in the root node's table */
	const struct intc2_node *gic = INTC2_NODE_DT_GET(DT_INST(0, arm_gic_v3));
	Z_INTC2_TABLE_CONST struct intc2_entry *entry =
		z_intc2_lookup(gic, TEST_SPI_INTID);

	zassert_not_null(entry, "no table slot for the bridged connect");
	zassert_equal_ptr(entry->isr, (void *)spi_handler,
			  "bridged slot does not hold the connected ISR");

	/* Same hardware state through the intc2 line ops */
	struct intc2_spec spec = {.node = gic, .line = TEST_SPI_INTID};

	zassert_true(intc2_is_enabled(spec) != 0);
	intc2_disable(spec);
	zassert_false(irq_is_enabled(TEST_SPI_INTID));
	intc2_enable(spec);
#endif /* CONFIG_INTC2 */

	irq_disable(TEST_SPI_INTID);
}

#ifdef CONFIG_INTC2
ZTEST(intc_gicv3_intc2, test_spi_affinity_routing)
{
#if !defined(CONFIG_INTC2_AFFINITY) || (CONFIG_MP_MAX_NUM_CPUS < 2)
	ztest_test_skip();
#else
	const struct intc2_node *gic = INTC2_NODE_DT_GET(DT_INST(0, arm_gic_v3));
	struct intc2_spec spec = {.node = gic, .line = TEST_SPI_INTID};
	struct intc2_spec ppi = {.node = gic, .line = TEST_PPI_INTID};
	uint32_t mask;

	/* the GIC routes an SPI to exactly one PE */
	zassert_equal(gic->flags & INTC2_NODE_AFFINITY_MASK,
		      INTC2_NODE_AFFINITY_SINGLE_TARGET,
		      "GICv3 node does not advertise single-target routing");

	/* banked lines never route */
	zassert_equal(intc2_set_affinity(ppi, BIT(0)), -ENOTSUP);
	zassert_equal(intc2_get_affinity(ppi, &mask), -ENOTSUP);

	/* lines boot routed to the first CPU of the default mask */
	zassert_ok(intc2_get_affinity(spec, &mask));
	zassert_equal(mask, BIT(LOG2(CONFIG_INTC2_AFFINITY_DEFAULT_MASK &
				     (0 - CONFIG_INTC2_AFFINITY_DEFAULT_MASK))));

	irq_enable(TEST_SPI_INTID);

	/* route the live line to each CPU in turn and check the
	 * handler actually runs there
	 */
	for (int cpu = 0; cpu < 2; cpu++) {
		handled_cpu = -1;
		zassert_ok(intc2_set_affinity(spec, BIT(cpu)));

		pulse_spi();

		zassert_true(spi_count > 0,
			     "SPI did not dispatch while routed to CPU %d", cpu);
		zassert_equal(handled_cpu, cpu,
			      "line routed to CPU %d but handled on CPU %d", cpu, handled_cpu);
		zassert_ok(intc2_get_affinity(spec, &mask));
		zassert_equal(mask, BIT(cpu));
	}

	irq_disable(TEST_SPI_INTID);

	/* restore the boot default */
	zassert_ok(intc2_set_affinity(spec, CONFIG_INTC2_AFFINITY_DEFAULT_MASK));
#endif
}
#endif /* CONFIG_INTC2 */

static void *intc_gicv3_intc2_setup(void)
{
	/* one connect record for the line, shared by all tests */
	IRQ_CONNECT(TEST_SPI_INTID, IRQ_DEFAULT_PRIORITY, spi_handler, NULL, 0);

	return NULL;
}

ZTEST_SUITE(intc_gicv3_intc2, NULL, intc_gicv3_intc2_setup, NULL, NULL, NULL);
