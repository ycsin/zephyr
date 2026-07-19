/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Verifies the intc2 legacy-backend shim: with CONFIG_INTC2 disabled,
 * the INTC2_* consumer API compiles down to the legacy interrupt
 * machinery (IRQ_CONNECT/irq_enable) and dispatches through the real
 * architecture interrupt path (NVIC on this platform).
 */

#include <zephyr/intc2.h>
#include <zephyr/ztest.h>

#include <cmsis_core.h>

#define SHIM_DEV DT_NODELABEL(shim_dev)

static const uint32_t shim_token;

static volatile uint32_t shim_count;
static volatile const void *shim_arg;

static void shim_isr(const void *arg)
{
	shim_count++;
	shim_arg = arg;
}

INTC2_DT_CONNECT(SHIM_DEV, DT_IRQ(SHIM_DEV, priority), shim_isr, &shim_token, 0);

ZTEST(intc2_shim, test_spec_resolution)
{
	struct intc2_spec spec = INTC2_DT_SPEC_GET(SHIM_DEV);

	zassert_equal(spec.irqn, DT_IRQN(SHIM_DEV));
}

ZTEST(intc2_shim, test_enable_dispatch_disable)
{
	struct intc2_spec spec = INTC2_DT_SPEC_GET(SHIM_DEV);

	intc2_enable(spec);
	zassert_true(intc2_is_enabled(spec));

	NVIC_SetPendingIRQ(DT_IRQN(SHIM_DEV));
	k_busy_wait(100);

	zassert_equal(shim_count, 1, "ISR not invoked exactly once");
	zassert_equal_ptr((const void *)shim_arg, &shim_token, "wrong ISR argument");

	intc2_disable(spec);
	zassert_false(intc2_is_enabled(spec));
}

#define SHIM_DEV_I DT_NODELABEL(shim_dev_i)

static volatile uint32_t shim_i_count;

static void shim_i_isr(const void *arg)
{
	ARG_UNUSED(arg);

	shim_i_count++;
}

/* Mimics a legacy per-instance driver configuration function */
static void shim_i_config_func(void)
{
	INTC2_DT_CONNECT_INLINE(SHIM_DEV_I, DT_IRQ(SHIM_DEV_I, priority), shim_i_isr, NULL, 0);
	intc2_enable((struct intc2_spec)INTC2_DT_SPEC_GET(SHIM_DEV_I));
}

ZTEST(intc2_shim, test_inline_connect)
{
	shim_i_config_func();

	NVIC_SetPendingIRQ(DT_IRQN(SHIM_DEV_I));
	k_busy_wait(100);

	zassert_equal(shim_i_count, 1, "inline-connected ISR not invoked exactly once");
	intc2_disable((struct intc2_spec)INTC2_DT_SPEC_GET(SHIM_DEV_I));
}

ZTEST_SUITE(intc2_shim, NULL, NULL, NULL, NULL, NULL);
