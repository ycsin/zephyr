/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Private header shared between APLIC core and MSI module.
 */

#ifndef ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_INTC_RISCV_APLIC_PRIV_H_
#define ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_INTC_RISCV_APLIC_PRIV_H_

#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>
#include <zephyr/types.h>
#ifdef CONFIG_INTC2_APLIC_DIRECT
#include <zephyr/intc2.h>
#endif

typedef void (*riscv_aplic_irq_config_func_t)(void);

struct aplic_cfg {
	uintptr_t base;
	uint32_t num_sources;
#ifdef CONFIG_RISCV_APLIC_MSI
	uint32_t imsic_addr;
#endif
#ifdef CONFIG_RISCV_APLIC_DIRECT
	uint32_t max_prio;
#ifndef CONFIG_INTC2_APLIC_DIRECT
	riscv_aplic_irq_config_func_t irq_config_func;
	const struct _isr_table_entry *isr_table;
#endif
#endif
};

struct aplic_data {
	struct k_spinlock lock;
};

static inline uint32_t rd32(uintptr_t base, uint32_t off)
{
	return sys_read32(base + off);
}

static inline void wr32(uintptr_t base, uint32_t off, uint32_t v)
{
	sys_write32(v, base + off);
}

#ifdef CONFIG_RISCV_APLIC_MSI
/**
 * @brief MSI-specific APLIC initialisation.
 *
 * Called from aplic_init() when CONFIG_RISCV_APLIC_MSI is enabled.
 * Configures MSI address registers and enables MSI delivery.
 *
 * @param dev APLIC device
 * @return 0 on success, negative error code on failure
 */
int aplic_msi_init(const struct device *dev);
#endif /* CONFIG_RISCV_APLIC_MSI */

#ifdef CONFIG_INTC2_APLIC_DIRECT
/*
 * Defined in intc_riscv_aplic_direct.c, referenced by the
 * INTC2_NODE_DT_DEFINE() invocation in intc_riscv_aplic.c's APLIC_INIT.
 */
extern const struct intc2_driver_api aplic_intc2_api;
#endif /* CONFIG_INTC2_APLIC_DIRECT */

#endif /* ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_INTC_RISCV_APLIC_PRIV_H_ */
