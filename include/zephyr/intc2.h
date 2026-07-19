/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief DAG-based interrupt core (intc2)
 *
 * intc2 models the interrupt fabric as a directed acyclic graph of const
 * interrupt controller nodes derived from the devicetree. An interrupt is
 * addressed by a compile-time (node, line) pair instead of an encoded IRQ
 * number, removing the multilevel encoding depth/bit-budget limits and the
 * Kconfig-carved software ISR table offsets.
 *
 * Dispatch tables are per-node and are laid out at build time by
 * scripts/build/gen_intc2_tables.py from connect records collected in a
 * first link pass; the entries themselves are emitted in the translation
 * unit that calls INTC2_DT_CONNECT(), so no addresses ever pass through
 * the generator.
 */

#ifndef ZEPHYR_INCLUDE_INTC2_H_
#define ZEPHYR_INCLUDE_INTC2_H_

#if !defined(_ASMLANGUAGE)

#include <stddef.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/toolchain.h>

#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(CONFIG_INTC2) && !defined(__DOXYGEN__)
/*
 * Legacy-backend shim: when the graph backend is not enabled, the
 * INTC2_* consumer-facing constructs compile down to the legacy
 * interrupt machinery, so code converted to the intc2 API keeps
 * building and working unchanged. Only the leaf-consumer surface is
 * shimmed; controller-side constructs (nodes, dispatch, the driver
 * API) have no legacy equivalent and remain graph-backend-only.
 */

#include <zephyr/init.h>
#include <zephyr/irq.h>

struct intc2_spec {
	uint32_t irqn;
};

#define INTC2_DT_SPEC_GET_BY_IDX(node_id, idx)   {.irqn = DT_IRQN_BY_IDX(node_id, idx)}
#define INTC2_DT_SPEC_GET_BY_NAME(node_id, name) {.irqn = DT_IRQN_BY_NAME(node_id, name)}
#define INTC2_DT_SPEC_GET(node_id)               INTC2_DT_SPEC_GET_BY_IDX(node_id, 0)

/*
 * Function-position connect: expands to the legacy IRQ_CONNECT()
 * verbatim, preserving the exact semantics of the call site it
 * replaces during migration.
 */
#define INTC2_DT_CONNECT_INLINE_BY_IDX(node_id, idx, prio, isr, arg, flags)                        \
	IRQ_CONNECT(DT_IRQN_BY_IDX(node_id, idx), prio, isr, arg, flags)
#define INTC2_DT_CONNECT_INLINE_BY_NAME(node_id, name, prio, isr, arg, flags)                     \
	IRQ_CONNECT(DT_IRQN_BY_NAME(node_id, name), prio, isr, arg, flags)
#define INTC2_DT_CONNECT_INLINE(node_id, prio, isr, arg, flags)                                    \
	INTC2_DT_CONNECT_INLINE_BY_IDX(node_id, 0, prio, isr, arg, flags)

/** @cond INTERNAL_HIDDEN */
#define Z_INTC2_SHIM_CONNECT(irqn_, prio_, isr_, arg_, flags_, counter_)                           \
	static int _CONCAT(__intc2_shim_init_, counter_)(void)                                     \
	{                                                                                          \
		IRQ_CONNECT(irqn_, prio_, isr_, arg_, flags_);                                     \
		return 0;                                                                          \
	}                                                                                          \
	SYS_INIT(_CONCAT(__intc2_shim_init_, counter_), PRE_KERNEL_1, 0)

/* extra layer so that __COUNTER__ expands exactly once */
#define Z_INTC2_SHIM_CONNECT_C(irqn_, prio_, isr_, arg_, flags_, counter_)                         \
	Z_INTC2_SHIM_CONNECT(irqn_, prio_, isr_, arg_, flags_, counter_)
/** INTERNAL_HIDDEN @endcond */

#define INTC2_DT_CONNECT_BY_IDX(node_id, idx, prio, isr, arg, flags)                               \
	Z_INTC2_SHIM_CONNECT_C(DT_IRQN_BY_IDX(node_id, idx), prio, isr, arg, flags, __COUNTER__)

#define INTC2_DT_CONNECT(node_id, prio, isr, arg, flags)                                           \
	INTC2_DT_CONNECT_BY_IDX(node_id, 0, prio, isr, arg, flags)

static inline void intc2_enable(struct intc2_spec spec)
{
	irq_enable(spec.irqn);
}

static inline void intc2_disable(struct intc2_spec spec)
{
	irq_disable(spec.irqn);
}

static inline int intc2_is_enabled(struct intc2_spec spec)
{
	return irq_is_enabled(spec.irqn);
}

static inline int intc2_set_priority(struct intc2_spec spec, uint32_t prio, uint32_t flags)
{
	ARG_UNUSED(spec);
	ARG_UNUSED(prio);
	ARG_UNUSED(flags);

	/* legacy priorities are programmed at connect time */
	return -ENOSYS;
}

#ifdef CONFIG_DYNAMIC_INTERRUPTS
static inline int intc2_connect_dynamic(struct intc2_spec spec, uint32_t prio,
					void (*isr)(const void *arg), const void *arg,
					uint32_t flags)
{
	(void)irq_connect_dynamic(spec.irqn, prio, isr, arg, flags);
	return 0;
}
#endif /* CONFIG_DYNAMIC_INTERRUPTS */

#else /* CONFIG_INTC2: the graph backend */

/**
 * @defgroup intc2_apis intc2 DAG-based interrupt APIs
 * @ingroup isr_apis
 * @{
 */

struct intc2_node;

/**
 * @brief Node flag: CPU-root controller bridging the legacy tables.
 *
 * The generator lays the node's table out dense and full-size, fills
 * unconnected lines with z_irq_spurious entries, and aliases the
 * legacy _sw_isr_table symbol to it, so legacy and intc2 connects
 * share one dispatch table (see CONFIG_INTC2_LEGACY_BRIDGE).
 */
#define INTC2_NODE_ROOT_BRIDGE BIT(0)

/**
 * @brief Interrupt specification: one input line of one controller node.
 *
 * Obtain with INTC2_DT_SPEC_GET(); resolvable entirely at compile time.
 */
struct intc2_spec {
	/** Controller node the interrupt line enters */
	const struct intc2_node *node;
	/** Input line index on that controller */
	uint16_t line;
};

/**
 * @brief Dispatch table entry.
 *
 * @note Layout is intentionally identical to struct _isr_table_entry
 * (argument first) so that future migration phases can alias legacy
 * tables; see include/zephyr/sw_isr_table.h.
 */
struct intc2_entry {
	const void *arg;
	void (*isr)(const void *arg);
};

#ifdef CONFIG_INTC2_DYNAMIC
#define Z_INTC2_TABLE_CONST
#else
#define Z_INTC2_TABLE_CONST const
#endif

/**
 * @brief Interrupt controller node driver API.
 *
 * All ops take the node itself; drivers reach their register block through
 * node->config. Dispatch-path ops (get_pending, eoi) execute on the CPU
 * that took the interrupt.
 *
 * Drivers must declare their instance with the standard driver-class
 * convention so that it participates in DEVICE_API_IS() checks:
 *
 *     static DEVICE_API(intc2, my_driver_api) = { .enable = ..., };
 */
__subsystem struct intc2_driver_api {
	/** Unmask @a line */
	void (*enable)(const struct intc2_node *node, uint32_t line);
	/** Mask @a line */
	void (*disable)(const struct intc2_node *node, uint32_t line);
	/** Return non-zero when @a line is unmasked */
	int (*is_enabled)(const struct intc2_node *node, uint32_t line);
	/** Optional: program priority/flags of @a line */
	int (*set_priority)(const struct intc2_node *node, uint32_t line,
			    uint32_t prio, uint32_t flags);
	/** Claim the highest-precedence pending line, or a negative value */
	int32_t (*get_pending)(const struct intc2_node *node);
	/** Optional: complete/EOI a claimed @a line */
	void (*eoi)(const struct intc2_node *node, uint32_t line);
	/** Optional: one-time hardware init, called in topological order */
	void (*init)(const struct intc2_node *node);
};

/**
 * @brief Interrupt controller graph node.
 *
 * One const instance per devicetree interrupt controller, defined by the
 * controller driver with INTC2_NODE_DT_DEFINE(). Everything is in ROM
 * unless CONFIG_INTC2_DYNAMIC places the dispatch tables in RAM.
 */
struct intc2_node {
	const struct intc2_driver_api *api;
	/** Driver private constant configuration (register base etc.) */
	const void *config;
	/**
	 * Node-local dispatch table, defined by the build-time generator.
	 * NULL when no generator output exists (first link pass).
	 */
	Z_INTC2_TABLE_CONST struct intc2_entry *table;
	/**
	 * Sparse-table line directory: NULL for a dense table indexed by
	 * line; otherwise lines[0] is the entry count and lines[1..count]
	 * are the line numbers, ascending, matching table[] order.
	 */
	const uint16_t *lines;
	/** Number of input lines the hardware has */
	uint16_t nlines;
	/** INTC2_NODE_* flags */
	uint16_t flags;
	/** Upstream edge; .node is NULL for a CPU-root node */
	struct intc2_spec parent;
};

/** @cond INTERNAL_HIDDEN */

/* Generated-data contracts (emitted by gen_intc2_tables.py) */

struct z_intc2_prio_rec {
	uint16_t line;
	uint8_t prio;
	uint8_t flags;
};

struct z_intc2_boot_rec {
	const struct intc2_node *node;
	const struct z_intc2_prio_rec *recs;
	uint16_t count;
};

struct z_intc2_fanin {
	const struct intc2_entry *clients;
	uint16_t count;
};

/*
 * First-link-pass record, collected from the .intc2_list section by
 * gen_intc2_tables.py and discarded from the final image. Deliberately
 * pointer-free (integers only) so that pass-1/pass-2 layout differences
 * are irrelevant.
 */
struct z_intc2_list_rec {
	uint32_t tag;
	uint32_t ord;
	uint32_t line_or_nlines;
	uint32_t prio;
	uint32_t flags;
	uint32_t parent_ord;
	uint32_t parent_line;
	/*
	 * CONNECT: the __COUNTER__ value of the call site, naming the
	 * .intc2_entry section suffix and defining registration order
	 * within a translation unit (the compiler may emit same-section
	 * objects in any order, so section order cannot be relied on).
	 */
	uint32_t order;
};

#define Z_INTC2_REC_TAG_NODE    1U
#define Z_INTC2_REC_TAG_CONNECT 2U
#define Z_INTC2_NO_PARENT       0xffffffffU

BUILD_ASSERT(sizeof(struct z_intc2_list_rec) == 32, "record must be 8 x uint32_t");
BUILD_ASSERT(offsetof(struct intc2_entry, arg) == 0, "arg must be first (legacy ABI)");
BUILD_ASSERT(sizeof(struct intc2_entry) == 2 * sizeof(void *), "no padding allowed");

/* Symbol naming, mirroring the DEVICE_DT_NAME_GET() dep-ordinal pattern */
#define Z_INTC2_NODE_SYM(node_id)  _CONCAT(__intc2_node_dts_ord_, DT_DEP_ORD(node_id))
#define Z_INTC2_TABLE_SYM(node_id) _CONCAT(__intc2_table_dts_ord_, DT_DEP_ORD(node_id))
#define Z_INTC2_LINES_SYM(node_id) _CONCAT(__intc2_lines_dts_ord_, DT_DEP_ORD(node_id))

#define Z_INTC2_LIST_REC(name, tag_, ord_, lon_, prio_, flags_, pord_, pline_, order_)             \
	static const struct z_intc2_list_rec name Z_GENERIC_SECTION(.intc2_list) __used = {         \
		.tag = (tag_),                                                                     \
		.ord = (ord_),                                                                     \
		.line_or_nlines = (lon_),                                                          \
		.prio = (prio_),                                                                   \
		.flags = (flags_),                                                                 \
		.parent_ord = (pord_),                                                             \
		.parent_line = (pline_),                                                           \
		.order = (order_),                                                                 \
	}

#define Z_INTC2_HAS_PARENT(node_id) DT_IRQ_HAS_IDX(node_id, 0)

#define Z_INTC2_PARENT_SPEC(node_id)                                                               \
	COND_CODE_1(Z_INTC2_HAS_PARENT(node_id),                                                   \
		    ({.node = &Z_INTC2_NODE_SYM(DT_IRQ_INTC_BY_IDX(node_id, 0)),                   \
		      .line = DT_IRQ_BY_IDX(node_id, 0, irq)}),                                    \
		    ({.node = NULL, .line = 0}))

#define Z_INTC2_PARENT_ORD(node_id)                                                                \
	COND_CODE_1(Z_INTC2_HAS_PARENT(node_id),                                                   \
		    (DT_DEP_ORD(DT_IRQ_INTC_BY_IDX(node_id, 0))), (Z_INTC2_NO_PARENT))

#define Z_INTC2_PARENT_LINE(node_id)                                                               \
	COND_CODE_1(Z_INTC2_HAS_PARENT(node_id), (DT_IRQ_BY_IDX(node_id, 0, irq)), (0))

#define Z_INTC2_PARENT_PRIO(node_id)                                                               \
	COND_CODE_1(Z_INTC2_HAS_PARENT(node_id),                                                   \
		    (COND_CODE_1(DT_IRQ_HAS_CELL_AT_IDX(node_id, 0, priority),                     \
				 (DT_IRQ_BY_IDX(node_id, 0, priority)), (0))),                     \
		    (0))

/* Per-callsite entry section: .intc2_entry.<parent ord>.<line>.<counter> */
#define Z_INTC2_ENTRY_SECTION(ord_, line_, counter_)                                               \
	".intc2_entry." STRINGIFY(ord_) "." STRINGIFY(line_) "." STRINGIFY(counter_)

#define Z_INTC2_CONNECT(ord_, line_, prio_, isr_, arg_, flags_, counter_)                          \
	static Z_INTC2_TABLE_CONST struct intc2_entry _CONCAT(__intc2_conn_, counter_)             \
		__attribute__((section(Z_INTC2_ENTRY_SECTION(ord_, line_, counter_))))             \
		__used = {                                                                         \
			.arg = (const void *)(arg_),                                               \
			.isr = (void (*)(const void *))(isr_),                                     \
		};                                                                                 \
	Z_INTC2_LIST_REC(_CONCAT(__intc2_connrec_, counter_), Z_INTC2_REC_TAG_CONNECT, (ord_),     \
			 (line_), (prio_), (flags_), 0, 0, (counter_))

/* Extra layer so that __COUNTER__ and the DT macros expand exactly once */
#define Z_INTC2_CONNECT_C(ord_, line_, prio_, isr_, arg_, flags_, counter_)                        \
	Z_INTC2_CONNECT(ord_, line_, prio_, isr_, arg_, flags_, counter_)

/*
 * Extern declarations for every status-okay interrupt controller node, so
 * that INTC2_DT_SPEC_GET()/INTC2_NODE_DT_GET() work from any translation
 * unit (same approach as device.h's per-node device declarations).
 */
#define Z_INTC2_MAYBE_NODE_DECLARE(node_id)                                                        \
	IF_ENABLED(DT_NODE_HAS_PROP(node_id, interrupt_controller),                                \
		   (extern const struct intc2_node Z_INTC2_NODE_SYM(node_id);))

DT_FOREACH_STATUS_OKAY_NODE(Z_INTC2_MAYBE_NODE_DECLARE)

/** INTERNAL_HIDDEN @endcond */

/**
 * @brief Get the C identifier of a controller's intc2 node.
 */
#define INTC2_NODE_DT_NAME_GET(node_id) Z_INTC2_NODE_SYM(node_id)

/**
 * @brief Get a pointer to a controller's intc2 node.
 */
#define INTC2_NODE_DT_GET(node_id) (&INTC2_NODE_DT_NAME_GET(node_id))

/**
 * @brief Define the intc2 node for a devicetree interrupt controller.
 *
 * The controller's devicetree node must have the `interrupt-controller`
 * property (its binding must include interrupt-controller.yaml). The node
 * is const and lives in ROM; the dispatch table it points to is produced
 * by the build-time generator.
 *
 * @param node_id devicetree node identifier of the controller
 * @param api_ pointer to the driver's const struct intc2_driver_api
 * @param config_ driver private const configuration pointer
 * @param nlines_ number of input lines the controller has
 * @param flags_ node flags (reserved, pass 0)
 */
#define INTC2_NODE_DT_DEFINE(node_id, api_, config_, nlines_, flags_)                              \
	extern __weak Z_INTC2_TABLE_CONST struct intc2_entry Z_INTC2_TABLE_SYM(node_id)[];         \
	extern __weak const uint16_t Z_INTC2_LINES_SYM(node_id)[];                                 \
	const struct intc2_node INTC2_NODE_DT_NAME_GET(node_id) = {                                \
		.api = (api_),                                                                     \
		.config = (config_),                                                               \
		.table = Z_INTC2_TABLE_SYM(node_id),                                               \
		.lines = Z_INTC2_LINES_SYM(node_id),                                               \
		.nlines = (nlines_),                                                               \
		.flags = (flags_),                                                                 \
		.parent = Z_INTC2_PARENT_SPEC(node_id),                                            \
	};                                                                                         \
	Z_INTC2_LIST_REC(_CONCAT(__intc2_noderec_, Z_INTC2_NODE_SYM(node_id)),                     \
			 Z_INTC2_REC_TAG_NODE, DT_DEP_ORD(node_id), (nlines_),                     \
			 Z_INTC2_PARENT_PRIO(node_id), (flags_), Z_INTC2_PARENT_ORD(node_id),      \
			 Z_INTC2_PARENT_LINE(node_id), 0)

/**
 * @brief Initializer for the intc2 spec of a device's interrupt by index.
 *
 * @param node_id devicetree node identifier of the interrupt consumer
 * @param idx index into the node's interrupt specifiers
 */
#define INTC2_DT_SPEC_GET_BY_IDX(node_id, idx)                                                     \
	{                                                                                          \
		.node = INTC2_NODE_DT_GET(DT_IRQ_INTC_BY_IDX(node_id, idx)),                       \
		.line = DT_IRQ_BY_IDX(node_id, idx, irq),                                          \
	}

/**
 * @brief Initializer for the intc2 spec of a device's first interrupt.
 */
#define INTC2_DT_SPEC_GET(node_id) INTC2_DT_SPEC_GET_BY_IDX(node_id, 0)

/**
 * @brief Statically connect an ISR to a device's interrupt (file scope).
 *
 * Unlike the legacy IRQ_CONNECT(), this is used at file scope and has no
 * runtime component: the entry is placed into the parent controller's
 * dispatch table at build time and the priority is applied from generated
 * const data during intc2 boot init.
 *
 * @param node_id devicetree node identifier of the interrupt consumer
 * @param idx index into the node's interrupt specifiers
 * @param prio interrupt priority (controller-specific meaning)
 * @param isr interrupt service routine, void (*)(const void *)
 * @param arg argument passed to @a isr
 * @param flags controller-specific flags
 */
#define INTC2_DT_CONNECT_BY_IDX(node_id, idx, prio, isr, arg, flags)                               \
	Z_INTC2_CONNECT_C(DT_DEP_ORD(DT_IRQ_INTC_BY_IDX(node_id, idx)),                            \
			  DT_IRQ_BY_IDX(node_id, idx, irq), prio, isr, arg, flags, __COUNTER__)

/**
 * @brief Statically connect an ISR to a device's interrupt by name.
 */
#define INTC2_DT_CONNECT_BY_NAME(node_id, name, prio, isr, arg, flags)                             \
	Z_INTC2_CONNECT_C(DT_DEP_ORD(DT_IRQ_INTC_BY_NAME(node_id, name)),                          \
			  DT_IRQ_BY_NAME(node_id, name, irq), prio, isr, arg, flags, __COUNTER__)

/**
 * @brief Statically connect an ISR to a device's first interrupt.
 */
#define INTC2_DT_CONNECT(node_id, prio, isr, arg, flags)                                           \
	INTC2_DT_CONNECT_BY_IDX(node_id, 0, prio, isr, arg, flags)

/**
 * @brief Initializer for the intc2 spec of a device's interrupt by name.
 */
#define INTC2_DT_SPEC_GET_BY_NAME(node_id, name)                                                   \
	{                                                                                          \
		.node = INTC2_NODE_DT_GET(DT_IRQ_INTC_BY_NAME(node_id, name)),                     \
		.line = DT_IRQ_BY_NAME(node_id, name, irq),                                        \
	}

/**
 * @brief Function-position variants of INTC2_DT_CONNECT().
 *
 * Identical to the file-scope forms on this backend (the emitted table
 * entry and record are function-local statics); provided so that
 * migrated legacy call sites inside per-instance configuration
 * functions convert one-to-one.
 */
#define INTC2_DT_CONNECT_INLINE_BY_IDX(node_id, idx, prio, isr, arg, flags)                        \
	INTC2_DT_CONNECT_BY_IDX(node_id, idx, prio, isr, arg, flags)
#define INTC2_DT_CONNECT_INLINE_BY_NAME(node_id, name, prio, isr, arg, flags)                     \
	INTC2_DT_CONNECT_BY_NAME(node_id, name, prio, isr, arg, flags)
#define INTC2_DT_CONNECT_INLINE(node_id, prio, isr, arg, flags)                                    \
	INTC2_DT_CONNECT(node_id, prio, isr, arg, flags)

/**
 * @brief Spurious interrupt handler, __weak for test/SoC override.
 */
void z_intc2_spurious(const struct intc2_node *node, int32_t line);

/** ISR trampoline dispatching a child node (generated chain slots) */
void z_intc2_node_dispatch(const void *node);

/** ISR trampoline iterating a shared line's client list */
void z_intc2_fanin_isr(const void *fanin);

#ifdef CONFIG_TRACING_ISR
extern void sys_trace_isr_enter(void);
extern void sys_trace_isr_exit(void);
#endif

/**
 * @brief Enable (unmask) the interrupt line described by @a spec.
 */
static inline void intc2_enable(struct intc2_spec spec)
{
	spec.node->api->enable(spec.node, spec.line);
}

/**
 * @brief Disable (mask) the interrupt line described by @a spec.
 */
static inline void intc2_disable(struct intc2_spec spec)
{
	spec.node->api->disable(spec.node, spec.line);
}

/**
 * @brief Get the enable state of the interrupt line described by @a spec.
 */
static inline int intc2_is_enabled(struct intc2_spec spec)
{
	return spec.node->api->is_enabled(spec.node, spec.line);
}

/**
 * @brief Program the priority of the interrupt line described by @a spec.
 *
 * @retval -ENOSYS when the controller has no priority support
 */
static inline int intc2_set_priority(struct intc2_spec spec, uint32_t prio, uint32_t flags)
{
	const struct intc2_driver_api *api = spec.node->api;

	if (api->set_priority == NULL) {
		return -ENOSYS;
	}

	return api->set_priority(spec.node, spec.line, prio, flags);
}

/** @cond INTERNAL_HIDDEN */
static inline Z_INTC2_TABLE_CONST struct intc2_entry *
z_intc2_lookup(const struct intc2_node *node, uint32_t line)
{
	Z_INTC2_TABLE_CONST struct intc2_entry *table = node->table;

	if (table == NULL) {
		return NULL;
	}

	if (node->lines == NULL) {
		/* dense: indexed directly by line */
		if (line >= node->nlines) {
			return NULL;
		}

		return &table[line];
	}

	/* sparse: lines[1..lines[0]] ascending, matching table[] order */
	{
		uint16_t lo = 0;
		uint16_t hi = node->lines[0];

		while (lo < hi) {
			uint16_t mid = (uint16_t)((lo + hi) / 2U);

			if (node->lines[1U + mid] < line) {
				lo = (uint16_t)(mid + 1U);
			} else {
				hi = mid;
			}
		}

		if ((lo < node->lines[0]) && (node->lines[1U + lo] == line)) {
			return &table[lo];
		}

		return NULL;
	}
}
/** INTERNAL_HIDDEN @endcond */

/**
 * @brief Dispatch all pending interrupts of @a node.
 *
 * Claims lines via the node's get_pending op until none is pending,
 * invoking the connected ISR (or the spurious handler) and completing
 * each line via the optional eoi op. Runs in interrupt context.
 */
static inline void intc2_dispatch(const struct intc2_node *node)
{
	const struct intc2_driver_api *api = node->api;
	int32_t line;

	while ((line = api->get_pending(node)) >= 0) {
		Z_INTC2_TABLE_CONST struct intc2_entry *entry =
			z_intc2_lookup(node, (uint32_t)line);

		if ((entry == NULL) || (entry->isr == NULL)) {
			z_intc2_spurious(node, line);
		} else {
#ifdef CONFIG_TRACING_ISR
			sys_trace_isr_enter();
#endif
			entry->isr(entry->arg);
#ifdef CONFIG_TRACING_ISR
			sys_trace_isr_exit();
#endif
		}

		if (api->eoi != NULL) {
			api->eoi(node, (uint32_t)line);
		}
	}
}

#ifdef CONFIG_INTC2_DYNAMIC
/**
 * @brief Connect an ISR to a free interrupt line at runtime.
 *
 * @retval 0 on success
 * @retval -EINVAL when @a spec is out of range
 * @retval -EBUSY when the line already has an ISR connected
 * @retval -ENOTSUP when the node has no dispatch table
 */
int intc2_connect_dynamic(struct intc2_spec spec, uint32_t prio,
			  void (*isr)(const void *arg), const void *arg, uint32_t flags);

/**
 * @brief Disconnect a previously connected ISR at runtime.
 *
 * @retval 0 on success
 * @retval -EINVAL when the (isr, arg) pair does not match the connection
 */
int intc2_disconnect_dynamic(struct intc2_spec spec, void (*isr)(const void *arg),
			     const void *arg);
#endif /* CONFIG_INTC2_DYNAMIC */

/**
 * @}
 */

#endif /* CONFIG_INTC2 */

/* DT_DRV_COMPAT instance conveniences (identical on both backends) */

#define INTC2_DT_INST_SPEC_GET(inst)               INTC2_DT_SPEC_GET(DT_DRV_INST(inst))
#define INTC2_DT_INST_SPEC_GET_BY_IDX(inst, idx)   INTC2_DT_SPEC_GET_BY_IDX(DT_DRV_INST(inst), idx)
#define INTC2_DT_INST_SPEC_GET_BY_NAME(inst, name)                                                 \
	INTC2_DT_SPEC_GET_BY_NAME(DT_DRV_INST(inst), name)

#define INTC2_DT_INST_CONNECT(inst, prio, isr, arg, flags)                                         \
	INTC2_DT_CONNECT(DT_DRV_INST(inst), prio, isr, arg, flags)
#define INTC2_DT_INST_CONNECT_BY_IDX(inst, idx, prio, isr, arg, flags)                             \
	INTC2_DT_CONNECT_BY_IDX(DT_DRV_INST(inst), idx, prio, isr, arg, flags)
#define INTC2_DT_INST_CONNECT_BY_NAME(inst, name, prio, isr, arg, flags)                           \
	INTC2_DT_CONNECT_BY_NAME(DT_DRV_INST(inst), name, prio, isr, arg, flags)

#define INTC2_DT_INST_CONNECT_INLINE(inst, prio, isr, arg, flags)                                  \
	INTC2_DT_CONNECT_INLINE(DT_DRV_INST(inst), prio, isr, arg, flags)
#define INTC2_DT_INST_CONNECT_INLINE_BY_IDX(inst, idx, prio, isr, arg, flags)                      \
	INTC2_DT_CONNECT_INLINE_BY_IDX(DT_DRV_INST(inst), idx, prio, isr, arg, flags)
#define INTC2_DT_INST_CONNECT_INLINE_BY_NAME(inst, name, prio, isr, arg, flags)                    \
	INTC2_DT_CONNECT_INLINE_BY_NAME(DT_DRV_INST(inst), name, prio, isr, arg, flags)

#ifdef __cplusplus
}
#endif

#endif /* !_ASMLANGUAGE */
#endif /* ZEPHYR_INCLUDE_INTC2_H_ */
