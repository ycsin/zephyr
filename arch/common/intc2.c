/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/intc2.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>

LOG_MODULE_DECLARE(os, CONFIG_KERNEL_LOG_LEVEL);

/*
 * Header of the .intc2_list first-link-pass section, consumed and
 * validated by gen_intc2_tables.py, discarded from the final image.
 */
struct z_intc2_info_hdr {
	uint32_t magic;
	uint32_t version;
};

Z_GENERIC_SECTION(.intc2_info) __used const struct z_intc2_info_hdr _intc2_info = {
	.magic = 0x32636e69U, /* "inc2" */
	.version = 1U,
};

/*
 * Boot directory emitted by the generator in topological order (roots
 * first). Weak so that the core links before any generator output exists.
 */
extern __weak const struct z_intc2_boot_rec __intc2_boot[];
extern __weak const uint32_t __intc2_boot_cnt;

static int z_intc2_boot_init(void)
{
	if (&__intc2_boot_cnt == NULL) {
		return 0;
	}

	for (uint32_t i = 0; i < __intc2_boot_cnt; i++) {
		const struct z_intc2_boot_rec *rec = &__intc2_boot[i];
		const struct intc2_node *node = rec->node;

		if (node->api->init != NULL) {
			node->api->init(node);
		}

		if (node->api->set_priority != NULL) {
			for (uint16_t j = 0; j < rec->count; j++) {
				const struct z_intc2_prio_rec *prio = &rec->recs[j];

				node->api->set_priority(node, prio->line, prio->prio,
							prio->flags);
			}
		}

		/*
		 * Enable the node's upstream edge once its own init is
		 * done; the parent is already initialized because the
		 * directory is topologically sorted.
		 */
		if (node->parent.node != NULL) {
			intc2_enable(node->parent);
		}
	}

	return 0;
}

SYS_INIT(z_intc2_boot_init, PRE_KERNEL_1, 0);

void z_intc2_node_dispatch(const void *node)
{
	intc2_dispatch(node);
}

void z_intc2_fanin_isr(const void *fanin)
{
	const struct z_intc2_fanin *f = fanin;

	for (uint16_t i = 0; i < f->count; i++) {
		const struct intc2_entry *client = &f->clients[i];

		if (client->isr == NULL) {
			continue;
		}

#ifdef CONFIG_TRACING_ISR
		sys_trace_isr_enter();
#endif
		client->isr(client->arg);
#ifdef CONFIG_TRACING_ISR
		sys_trace_isr_exit();
#endif
	}
}

__weak void z_intc2_spurious(const struct intc2_node *node, int32_t line)
{
	LOG_ERR("intc2: spurious interrupt (node %p line %d)", node, line);

	k_fatal_halt(K_ERR_SPURIOUS_IRQ);
}

#ifdef CONFIG_INTC2_DYNAMIC

static struct k_spinlock z_intc2_lock;

int intc2_connect_dynamic(struct intc2_spec spec, uint32_t prio,
			  void (*isr)(const void *arg), const void *arg, uint32_t flags)
{
	const struct intc2_node *node = spec.node;
	struct intc2_entry *entry;
	k_spinlock_key_t key;

	/*
	 * Self-dispatching controllers (e.g. x86 IOAPIC/LOAPIC) have no
	 * intc2 software table; they install the ISR through their own
	 * connect op (typically the arch's runtime vector machinery).
	 */
	if (node->api->connect != NULL) {
		return node->api->connect(node, spec.line, prio, isr, arg, flags);
	}

	if ((node->table == NULL) || (node->lines != NULL)) {
		/* No generated table, or sparse (dynamic requires dense) */
		return -ENOTSUP;
	}

	if (spec.line >= node->nlines) {
		return -EINVAL;
	}

	key = k_spin_lock(&z_intc2_lock);

	entry = &node->table[spec.line];
	if (entry->isr != NULL) {
		k_spin_unlock(&z_intc2_lock, key);
		return -EBUSY;
	}

	entry->arg = arg;
	entry->isr = isr;

	k_spin_unlock(&z_intc2_lock, key);

	if (node->api->set_priority != NULL) {
		node->api->set_priority(node, spec.line, prio, flags);
	}

	return 0;
}

int intc2_disconnect_dynamic(struct intc2_spec spec, void (*isr)(const void *arg),
			     const void *arg)
{
	const struct intc2_node *node = spec.node;
	struct intc2_entry *entry;
	k_spinlock_key_t key;
	int ret = 0;

	if (node->api->disconnect != NULL) {
		ARG_UNUSED(isr);
		ARG_UNUSED(arg);
		return node->api->disconnect(node, spec.line);
	}

	if ((node->table == NULL) || (node->lines != NULL)) {
		return -ENOTSUP;
	}

	if (spec.line >= node->nlines) {
		return -EINVAL;
	}

	key = k_spin_lock(&z_intc2_lock);

	entry = &node->table[spec.line];
	if ((entry->isr != isr) || (entry->arg != arg)) {
		ret = -EINVAL;
	} else {
		entry->isr = NULL;
		entry->arg = NULL;
	}

	k_spin_unlock(&z_intc2_lock, key);

	return ret;
}

#endif /* CONFIG_INTC2_DYNAMIC */
