/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "emul_intc.h"
#ifdef CONFIG_INTC2_ALLOC
#include "emul_alloc_intc.h"
#endif

#include <zephyr/intc2.h>
#include <zephyr/irq_offload.h>
#include <zephyr/ztest.h>
#ifdef CONFIG_INTC2_SHELL
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#endif

#define ROOT_NODE  DT_NODELABEL(intc2_root)
#define L2_NODE    DT_NODELABEL(intc2_l2)
#define DEV_A_NODE DT_NODELABEL(test_dev_a)
#define DEV_B_NODE DT_NODELABEL(test_dev_b)
#define DEV_C_NODE DT_NODELABEL(test_dev_c)

#define WIDE_NODE  DT_NODELABEL(intc2_wide)
#define DEV_W_NODE DT_NODELABEL(test_dev_w)

#define ROOT INTC2_NODE_DT_GET(ROOT_NODE)
#define L2   INTC2_NODE_DT_GET(L2_NODE)
#define WIDE INTC2_NODE_DT_GET(WIDE_NODE)

#define DEV_A_PRIO 2

static const uint32_t a_token;

static volatile uint32_t seq;
static volatile uint32_t a_count, b_count, c_count, dyn_count;
static volatile uint32_t b_seq, c_seq;
static volatile const void *a_arg;
static volatile uint32_t spurious_count;

/* Overrides the __weak fatal-error default so tests can observe it */
void z_intc2_spurious(const struct intc2_node *node, int32_t line)
{
	ARG_UNUSED(node);
	ARG_UNUSED(line);

	spurious_count++;
}

static void dev_a_isr(const void *arg)
{
	a_count++;
	a_arg = arg;
	seq++;
}

static void dev_b_isr(const void *arg)
{
	ARG_UNUSED(arg);

	b_count++;
	b_seq = seq++;
}

INTC2_DT_CONNECT(DEV_A_NODE, DEV_A_PRIO, dev_a_isr, &a_token, 0);
INTC2_DT_CONNECT(DEV_B_NODE, DT_IRQ_BY_IDX(DEV_B_NODE, 0, priority), dev_b_isr, NULL, 0);

static volatile uint32_t w_count;

static void dev_w_isr(const void *arg)
{
	ARG_UNUSED(arg);

	w_count++;
}

INTC2_DT_CONNECT(DEV_W_NODE, DT_IRQ_BY_IDX(DEV_W_NODE, 0, priority), dev_w_isr, NULL, 0);

#ifdef CONFIG_INTC2_SHARED
static void dev_c_isr(const void *arg)
{
	ARG_UNUSED(arg);

	c_count++;
	c_seq = seq++;
}

INTC2_DT_CONNECT(DEV_C_NODE, DT_IRQ_BY_IDX(DEV_C_NODE, 0, priority), dev_c_isr, NULL, 0);
#endif /* CONFIG_INTC2_SHARED */

static void dispatch_root(const void *arg)
{
	ARG_UNUSED(arg);

	intc2_dispatch(ROOT);
}

/* Raise a line, then dispatch from interrupt context via irq_offload() */
static void trigger(const struct intc2_node *node, uint32_t line)
{
	emul_intc_raise(node, line);
	irq_offload(dispatch_root, NULL);
}

static void intc2_test_before(void *fixture)
{
	ARG_UNUSED(fixture);

	emul_intc_regs(ROOT)->pending = 0;
	emul_intc_regs(L2)->pending = 0;
	emul_intc_regs(WIDE)->pending = 0;
	seq = 0;
	a_count = 0;
	b_count = 0;
	c_count = 0;
	w_count = 0;
	dyn_count = 0;
	spurious_count = 0;
}

ZTEST_SUITE(intc2_base, NULL, NULL, intc2_test_before, NULL, NULL);

ZTEST(intc2_base, test_spec_resolution)
{
	static const struct intc2_spec spec_a = INTC2_DT_SPEC_GET(DEV_A_NODE);
	static const struct intc2_spec spec_b = INTC2_DT_SPEC_GET(DEV_B_NODE);

	zassert_equal(spec_a.node, ROOT);
	zassert_equal(spec_a.line, 1);
	zassert_equal(spec_b.node, L2);
	zassert_equal(spec_b.line, 2);
	zassert_is_null(ROOT->parent.node, "root must have no parent");
	zassert_equal(L2->parent.node, ROOT);
	zassert_equal(L2->parent.line, 3);
}

ZTEST(intc2_base, test_boot_init)
{
	zassert_true(emul_intc_regs(ROOT)->inited, "root init op not called");
	zassert_true(emul_intc_regs(L2)->inited, "l2 init op not called");

	/* the chained node's upstream edge is enabled during boot */
	zassert_true((emul_intc_regs(ROOT)->enable & BIT(3)) != 0U,
		     "l2 parent line not enabled at boot");

	/* static connect priorities were applied from generated const data */
	zassert_equal(emul_intc_regs(ROOT)->prio[1], DEV_A_PRIO);
#ifdef CONFIG_INTC2_SHARED
	/* two clients on l2 line 2: the last registration's priority wins */
	zassert_equal(emul_intc_regs(L2)->prio[2],
		      DT_IRQ_BY_IDX(DEV_C_NODE, 0, priority));
#else
	zassert_equal(emul_intc_regs(L2)->prio[2],
		      DT_IRQ_BY_IDX(DEV_B_NODE, 0, priority));
#endif
}

ZTEST(intc2_base, test_enable_disable)
{
	struct intc2_spec spec_a = INTC2_DT_SPEC_GET(DEV_A_NODE);

	intc2_enable(spec_a);
	zassert_true(intc2_is_enabled(spec_a));
	zassert_true((emul_intc_regs(ROOT)->enable & BIT(1)) != 0U);

	intc2_disable(spec_a);
	zassert_false(intc2_is_enabled(spec_a));
	zassert_true((emul_intc_regs(ROOT)->enable & BIT(1)) == 0U);
}

ZTEST(intc2_base, test_line_flags)
{
	struct intc2_spec spec = INTC2_DT_SPEC_GET(DEV_A_NODE);
	struct intc2_spec l2spec = {.node = L2, .line = 0};

	zassert_equal(intc2_line_flags(spec), 0);

	emul_intc_regs(ROOT)->line_flags = INTC2_LINE_BANKED | INTC2_LINE_FIXED_ENABLE;
	zassert_equal(intc2_line_flags(spec), INTC2_LINE_BANKED | INTC2_LINE_FIXED_ENABLE);
	emul_intc_regs(ROOT)->line_flags = 0;

	/* controllers without the op report no attributes */
	zassert_equal(intc2_line_flags(l2spec), 0);
}

ZTEST(intc2_base, test_set_priority)
{
	struct intc2_spec spec_a = INTC2_DT_SPEC_GET(DEV_A_NODE);

	zassert_ok(intc2_set_priority(spec_a, 7, 0));
	zassert_equal(emul_intc_regs(ROOT)->prio[1], 7);
	zassert_ok(intc2_set_priority(spec_a, DEV_A_PRIO, 0));
}

ZTEST(intc2_base, test_dispatch_level1)
{
	struct intc2_spec spec_a = INTC2_DT_SPEC_GET(DEV_A_NODE);
	uint32_t eoi = emul_intc_regs(ROOT)->eoi_cnt;

	intc2_enable(spec_a);
	trigger(ROOT, 1);

	zassert_equal(a_count, 1, "ISR not invoked exactly once");
	zassert_equal_ptr((const void *)a_arg, &a_token, "wrong ISR argument");
	zassert_equal(emul_intc_regs(ROOT)->eoi_cnt, eoi + 1, "no EOI");

	intc2_disable(spec_a);
}

ZTEST(intc2_base, test_dispatch_level2)
{
	struct intc2_spec spec_b = INTC2_DT_SPEC_GET(DEV_B_NODE);
	uint32_t root_eoi = emul_intc_regs(ROOT)->eoi_cnt;
	uint32_t l2_eoi = emul_intc_regs(L2)->eoi_cnt;

	intc2_enable(spec_b);
	trigger(L2, 2);

	zassert_equal(b_count, 1, "chained ISR not invoked exactly once");
	zassert_equal(emul_intc_regs(ROOT)->eoi_cnt, root_eoi + 1, "no root EOI");
	zassert_equal(emul_intc_regs(L2)->eoi_cnt, l2_eoi + 1, "no l2 EOI");

	intc2_disable(spec_b);
}

static volatile uint32_t i_count;

static void dev_i_isr(const void *arg)
{
	ARG_UNUSED(arg);

	i_count++;
}

/* Mimics a legacy per-instance driver configuration function */
static void dev_i_config_func(void)
{
	INTC2_DT_CONNECT_INLINE(DT_NODELABEL(test_dev_i), 1, dev_i_isr, NULL, 0);
	intc2_enable((struct intc2_spec)INTC2_DT_SPEC_GET(DT_NODELABEL(test_dev_i)));
}

ZTEST(intc2_base, test_inline_connect)
{
	i_count = 0;
	dev_i_config_func();
	trigger(ROOT, 2);
	zassert_equal(i_count, 1, "inline-connected ISR not invoked exactly once");
	intc2_disable((struct intc2_spec)INTC2_DT_SPEC_GET(DT_NODELABEL(test_dev_i)));
}

ZTEST(intc2_base, test_wide_dispatch)
{
	struct intc2_spec spec_w = INTC2_DT_SPEC_GET(DEV_W_NODE);

	/* sparse unless CONFIG_INTC2_DYNAMIC forces the dense layout */
	if (!IS_ENABLED(CONFIG_INTC2_DYNAMIC)) {
		zassert_not_null(WIDE->lines, "wide node table should be sparse");
		zassert_equal(WIDE->lines[0], 1);
		zassert_equal(WIDE->lines[1], 20);
	} else {
		zassert_is_null(WIDE->lines, "dynamic must force dense tables");
	}

	zassert_equal(spec_w.node, WIDE);
	zassert_equal(spec_w.line, 20);
	zassert_true(emul_intc_regs(WIDE)->inited);
	zassert_equal(emul_intc_regs(WIDE)->prio[20],
		      DT_IRQ_BY_IDX(DEV_W_NODE, 0, priority));

	intc2_enable(spec_w);
	trigger(WIDE, 20);
	zassert_equal(w_count, 1, "sparse-table ISR not invoked exactly once");
	intc2_disable(spec_w);
}

ZTEST(intc2_base, test_wide_spurious_miss)
{
	struct intc2_spec unconnected = {.node = WIDE, .line = 7};

	intc2_enable(unconnected);
	trigger(WIDE, 7);
	zassert_equal(spurious_count, 1, "sparse lookup miss must be spurious");
	intc2_disable(unconnected);
}

ZTEST(intc2_base, test_spurious)
{
	struct intc2_spec unconnected = {.node = ROOT, .line = 6};

	intc2_enable(unconnected);
	trigger(ROOT, 6);

	zassert_equal(spurious_count, 1, "spurious handler not invoked");

	intc2_disable(unconnected);
}

#ifdef CONFIG_INTC2_SHARED
ZTEST_SUITE(intc2_shared, NULL, NULL, intc2_test_before, NULL, NULL);

ZTEST(intc2_shared, test_fanin_both_clients)
{
	struct intc2_spec spec_b = INTC2_DT_SPEC_GET(DEV_B_NODE);
	uint32_t l2_eoi = emul_intc_regs(L2)->eoi_cnt;

	intc2_enable(spec_b);
	trigger(L2, 2);

	zassert_equal(b_count, 1, "first client not invoked");
	zassert_equal(c_count, 1, "second client not invoked");
	zassert_true(b_seq < c_seq, "clients not invoked in registration order");
	zassert_equal(emul_intc_regs(L2)->eoi_cnt, l2_eoi + 1,
		      "shared line must EOI exactly once");

	intc2_disable(spec_b);
}
#endif /* CONFIG_INTC2_SHARED */

#ifdef CONFIG_INTC2_DYNAMIC
static void dyn_isr(const void *arg)
{
	ARG_UNUSED(arg);

	dyn_count++;
}

ZTEST_SUITE(intc2_dynamic, NULL, NULL, intc2_test_before, NULL, NULL);

ZTEST(intc2_dynamic, test_connect_dispatch_disconnect)
{
	struct intc2_spec spec = {.node = ROOT, .line = 5};

	zassert_ok(intc2_connect_dynamic(spec, 4, dyn_isr, NULL, 0));
	zassert_equal(emul_intc_regs(ROOT)->prio[5], 4, "dynamic priority not applied");

	intc2_enable(spec);
	trigger(ROOT, 5);
	zassert_equal(dyn_count, 1, "dynamic ISR not invoked");

	zassert_ok(intc2_disconnect_dynamic(spec, dyn_isr, NULL));
	trigger(ROOT, 5);
	zassert_equal(dyn_count, 1, "ISR invoked after disconnect");
	zassert_equal(spurious_count, 1, "disconnected line must be spurious");

	intc2_disable(spec);
}

ZTEST(intc2_dynamic, test_connect_busy)
{
	/* line 1 of the root is statically connected to dev_a_isr */
	struct intc2_spec spec = {.node = ROOT, .line = 1};

	zassert_equal(intc2_connect_dynamic(spec, 0, dyn_isr, NULL, 0), -EBUSY);
}

ZTEST(intc2_dynamic, test_disconnect_mismatch)
{
	struct intc2_spec spec = {.node = ROOT, .line = 5};

	zassert_ok(intc2_connect_dynamic(spec, 0, dyn_isr, NULL, 0));
	zassert_equal(intc2_disconnect_dynamic(spec, dev_a_isr, NULL), -EINVAL,
		      "mismatched ISR must not disconnect");
	zassert_ok(intc2_disconnect_dynamic(spec, dyn_isr, NULL));
}

ZTEST(intc2_dynamic, test_connect_out_of_range)
{
	struct intc2_spec spec = {.node = ROOT, .line = EMUL_INTC_NLINES};

	zassert_equal(intc2_connect_dynamic(spec, 0, dyn_isr, NULL, 0), -EINVAL);
}
#endif /* CONFIG_INTC2_DYNAMIC */

ZTEST_SUITE(intc2_affinity, NULL, NULL, intc2_test_before, NULL, NULL);

ZTEST(intc2_affinity, test_affinity_roundtrip)
{
#ifndef CONFIG_INTC2_AFFINITY
	ztest_test_skip();
#else
	struct intc2_spec spec = INTC2_DT_SPEC_GET(DEV_A_NODE);
	uint32_t mask = 0xa5a5a5a5;

	zassert_equal(spec.node->flags & INTC2_NODE_AFFINITY_MASK,
		      INTC2_NODE_AFFINITY_MULTI_TARGET);

	/* the controller applies the boot default in its init op */
	zassert_ok(intc2_get_affinity(spec, &mask));
	zassert_equal(mask, CONFIG_INTC2_AFFINITY_DEFAULT_MASK);

	zassert_ok(intc2_set_affinity(spec, BIT(0)));
	zassert_ok(intc2_get_affinity(spec, &mask));
	zassert_equal(mask, BIT(0));

	/* routing does not disturb dispatch */
	intc2_enable(spec);
	trigger(ROOT, spec.line);
	zassert_equal(a_count, 1);
	intc2_disable(spec);
#endif
}

ZTEST(intc2_affinity, test_affinity_bad_mask)
{
#ifndef CONFIG_INTC2_AFFINITY
	ztest_test_skip();
#else
	struct intc2_spec spec = INTC2_DT_SPEC_GET(DEV_A_NODE);

	/* empty masks are rejected in the core */
	zassert_equal(intc2_set_affinity(spec, 0), -EINVAL);

	/* unreachable CPUs are rejected by the controller */
	zassert_equal(intc2_set_affinity(spec, BIT(31)), -EINVAL);
#endif
}

ZTEST(intc2_affinity, test_affinity_fixed_routing)
{
#ifndef CONFIG_INTC2_AFFINITY
	ztest_test_skip();
#else
	struct intc2_spec spec = {.node = L2, .line = 0};
	uint32_t mask;

	zassert_equal(spec.node->flags & INTC2_NODE_AFFINITY_MASK, 0);
	zassert_equal(intc2_set_affinity(spec, BIT(0)), -ENOTSUP);
	zassert_equal(intc2_get_affinity(spec, &mask), -ENOTSUP);
#endif
}

#ifdef CONFIG_INTC2_ALLOC

#define ALLOC_NODE (&emul_alloc_intc_node)

static volatile uint32_t alloc_a_count, alloc_b_count;
static volatile const void *alloc_a_arg;

static void alloc_a_isr(const void *arg)
{
	alloc_a_count++;
	alloc_a_arg = arg;
}

static void alloc_b_isr(const void *arg)
{
	ARG_UNUSED(arg);

	alloc_b_count++;
}

static void intc2_alloc_test_before(void *fixture)
{
	ARG_UNUSED(fixture);

	alloc_a_count = 0;
	alloc_b_count = 0;
	emul_alloc_intc_reset();
}

ZTEST_SUITE(intc2_alloc, NULL, NULL, intc2_alloc_test_before, NULL, NULL);

ZTEST(intc2_alloc, test_alloc_connect_dispatch)
{
	struct intc2_spec spec;
	static const uint32_t token = 0x5a;

	zassert_equal(ALLOC_NODE->flags & INTC2_NODE_ALLOC, INTC2_NODE_ALLOC);

	zassert_ok(intc2_line_alloc(ALLOC_NODE, 1, 0, alloc_a_isr, &token, &spec));
	zassert_equal(spec.node, ALLOC_NODE);

	zassert_equal(intc2_is_enabled(spec), 0);
	intc2_enable(spec);
	zassert_true(intc2_is_enabled(spec) != 0);

	emul_alloc_intc_fire(spec.line);
	zassert_equal(alloc_a_count, 1);
	zassert_equal_ptr((const void *)alloc_a_arg, &token);

	intc2_disable(spec);
	zassert_equal(intc2_is_enabled(spec), 0);

	/* disabled lines don't dispatch */
	emul_alloc_intc_fire(spec.line);
	zassert_equal(alloc_a_count, 1);
}

ZTEST(intc2_alloc, test_alloc_distinct_lines)
{
	struct intc2_spec spec_a, spec_b;

	zassert_ok(intc2_line_alloc(ALLOC_NODE, 1, 0, alloc_a_isr, NULL, &spec_a));
	zassert_ok(intc2_line_alloc(ALLOC_NODE, 1, 0, alloc_b_isr, NULL, &spec_b));
	zassert_not_equal(spec_a.line, spec_b.line);

	intc2_enable(spec_a);
	intc2_enable(spec_b);

	emul_alloc_intc_fire(spec_a.line);
	zassert_equal(alloc_a_count, 1);
	zassert_equal(alloc_b_count, 0);

	emul_alloc_intc_fire(spec_b.line);
	zassert_equal(alloc_a_count, 1);
	zassert_equal(alloc_b_count, 1);

	intc2_disable(spec_a);
	intc2_disable(spec_b);
}

ZTEST(intc2_alloc, test_alloc_exhaustion)
{
	struct intc2_spec spec;
	int allocated = 0;
	int ret;

	do {
		ret = intc2_line_alloc(ALLOC_NODE, 1, 0, alloc_a_isr, NULL, &spec);
		if (ret == 0) {
			allocated++;
		}
	} while ((ret == 0) && (allocated <= EMUL_ALLOC_INTC_NLINES));

	zassert_equal(ret, -ENOMEM, "allocator did not report exhaustion");
	zassert_equal(allocated, EMUL_ALLOC_INTC_NLINES);
}

ZTEST(intc2_alloc, test_alloc_not_supported_on_non_alloc_node)
{
	struct intc2_spec spec;

	/* the L2 node has no INTC2_NODE_ALLOC flag and no alloc op */
	zassert_equal(L2->flags & INTC2_NODE_ALLOC, 0);
	zassert_equal(intc2_line_alloc(L2, 1, 0, alloc_a_isr, NULL, &spec), -ENOTSUP);
}

ZTEST(intc2_alloc, test_msi_alloc)
{
#ifndef CONFIG_INTC2_MSI
	ztest_test_skip();
#else
	struct intc2_spec spec;
	struct intc2_msi msg = {0};

	zassert_ok(intc2_msi_alloc(ALLOC_NODE, 1, 0, alloc_a_isr, NULL, &spec, &msg));
	zassert_not_equal(msg.address, 0);

	intc2_enable(spec);
	emul_alloc_intc_fire(spec.line);
	zassert_equal(alloc_a_count, 1);
	intc2_disable(spec);
#endif
}

ZTEST(intc2_alloc, test_msi_alloc_not_supported_on_plain_alloc_node)
{
#ifdef CONFIG_INTC2_MSI
	ztest_test_skip();
#else
	struct intc2_spec spec;
	struct intc2_msi msg;

	/* CONFIG_INTC2_MSI is disabled: the call must report -ENOSYS */
	zassert_equal(intc2_msi_alloc(ALLOC_NODE, 1, 0, alloc_a_isr, NULL, &spec, &msg), -ENOSYS);
#endif
}

#endif /* CONFIG_INTC2_ALLOC */

#ifdef CONFIG_INTC2_SHELL

static void *intc2_shell_setup(void)
{
	/* let the shell backend initialize before the first command */
	k_usleep(10);

	return NULL;
}

ZTEST_SUITE(intc2_shell, NULL, intc2_shell_setup, NULL, NULL, NULL);

static const char *exec(const char *cmd)
{
	const struct shell *sh = shell_backend_dummy_get_ptr();
	size_t size;

	shell_backend_dummy_clear_output(sh);
	zassert_ok(shell_execute_cmd(sh, cmd), "shell command failed: %s", cmd);

	return shell_backend_dummy_get_output(sh, &size);
}

ZTEST(intc2_shell, test_list)
{
	const char *out = exec("intc2 list");

	zassert_not_null(strstr(out, "intc2-root"), "root node missing from 'intc2 list'");
	zassert_not_null(strstr(out, "intc2-l2"), "l2 node missing from 'intc2 list'");
}

ZTEST(intc2_shell, test_affinity_get_set)
{
	if (!IS_ENABLED(CONFIG_INTC2_AFFINITY)) {
		ztest_test_skip();
	}

	/* mask must stay within CONFIG_MP_MAX_NUM_CPUS (1 on this build) */
	zassert_not_null(strstr(exec("intc2 affinity get intc2-root 1"), "0x1"));
	zassert_not_null(strstr(exec("intc2 affinity set intc2-root 1 0x1"), "0x1"));
	zassert_not_null(strstr(exec("intc2 affinity get intc2-root 1"), "0x1"));

	/* the l2 node's api has no set_affinity/get_affinity ops */
	const struct shell *sh = shell_backend_dummy_get_ptr();

	zassert_not_equal(shell_execute_cmd(sh, "intc2 affinity get intc2-l2 0"), 0);
}

ZTEST(intc2_shell, test_unknown_node)
{
	const struct shell *sh = shell_backend_dummy_get_ptr();

	zassert_not_equal(shell_execute_cmd(sh, "intc2 affinity get no-such-node 0"), 0);
}

#endif /* CONFIG_INTC2_SHELL */
