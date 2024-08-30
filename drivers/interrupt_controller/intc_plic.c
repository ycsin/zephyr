/*
 * Copyright (c) 2017 Jean-Paul Etienne <fractalclone@gmail.com>
 * Copyright (c) 2023 Meta
 * Contributors: 2018 Antmicro <www.antmicro.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT sifive_plic_1_0_0

/**
 * @brief Platform Level Interrupt Controller (PLIC) driver
 *        for RISC-V processors
 */

#include <stdlib.h>

#include "sw_isr_common.h"

#include <zephyr/debug/symtab.h>
#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/devicetree/interrupt_controller.h>
#include <zephyr/shell/shell.h>

#include <zephyr/sw_isr_table.h>
#include <zephyr/drivers/interrupt_controller/riscv_plic.h>
#include <zephyr/irq.h>

#define PLIC_BASE_ADDR(n) DT_INST_REG_ADDR(n)
/*
 * These registers' offset are defined in the RISCV PLIC specs, see:
 * https://github.com/riscv/riscv-plic-spec
 */
#define CONTEXT_BASE 0x200000
#define CONTEXT_SIZE 0x1000
#define CONTEXT_THRESHOLD 0x00
#define CONTEXT_CLAIM 0x04
#define CONTEXT_ENABLE_BASE 0x2000
#define CONTEXT_ENABLE_SIZE 0x80
/*
 * Trigger type is mentioned, but not defined in the RISCV PLIC specs.
 * However, it is defined and supported by at least the Andes & Telink datasheet, and supported
 * in Linux's SiFive PLIC driver
 */
#define PLIC_TRIG_LEVEL ((uint32_t)0)
#define PLIC_TRIG_EDGE  ((uint32_t)1)
#define PLIC_DRV_HAS_COMPAT(compat)                                                                \
	DT_NODE_HAS_COMPAT(DT_COMPAT_GET_ANY_STATUS_OKAY(DT_DRV_COMPAT), compat)

#if PLIC_DRV_HAS_COMPAT(andestech_nceplic100)
#define PLIC_SUPPORTS_TRIG_TYPE 1
#define PLIC_REG_TRIG_TYPE_WIDTH 1
#define PLIC_REG_TRIG_TYPE_OFFSET 0x1080
#else
/* Trigger-type not supported */
#define PLIC_REG_TRIG_TYPE_WIDTH 0
#endif

/* PLIC registers are 32-bit memory-mapped */
#define PLIC_REG_SIZE 32
#define PLIC_REG_MASK BIT_MASK(LOG2(PLIC_REG_SIZE))

#ifdef CONFIG_TEST_INTC_PLIC
#define INTC_PLIC_STATIC
#else
#define INTC_PLIC_STATIC static inline
#endif

#ifdef CONFIG_PLIC_IRQ_AFFINITY
#if CONFIG_MP_MAX_NUM_CPUS <= 8
typedef uint8_t plic_cpumask_t;
#elif CONFIG_MP_MAX_NUM_CPUS <= 16
typedef uint16_t plic_cpumask_t;
#elif CONFIG_MP_MAX_NUM_CPUS <= 32
typedef uint32_t plic_cpumask_t;
#elif CONFIG_MP_MAX_NUM_CPUS <= 64
typedef uint64_t plic_cpumask_t;
#else
#error "Currently only supports up to 64 cores"
#endif
#endif /* CONFIG_PLIC_IRQ_AFFINITY */

typedef void (*riscv_plic_irq_config_func_t)(void);
struct plic_config {
	mem_addr_t prio;
	mem_addr_t irq_en;
	mem_addr_t reg;
	mem_addr_t trig;
	uint32_t max_prio;
	/* Number of IRQs that the PLIC physically supports */
	uint32_t riscv_ndev;
	/* Number of IRQs supported in this driver */
	uint32_t nr_irqs;
	uint32_t irq;
	riscv_plic_irq_config_func_t irq_config_func;
	struct _isr_table_entry *isr_table;
	const int *const hart_context;
};

struct plic_stats {
	uint16_t *const irq_count;
	const int irq_count_len;
};

struct plic_data {
	struct plic_stats stats;
#ifdef CONFIG_PLIC_IRQ_AFFINITY
	plic_cpumask_t *irq_cpumask;
#endif /* CONFIG_PLIC_IRQ_AFFINITY */
};

static uint32_t save_irq[CONFIG_MP_MAX_NUM_CPUS];
static const struct device *save_dev[CONFIG_MP_MAX_NUM_CPUS];

INTC_PLIC_STATIC uint32_t local_irq_to_reg_index(uint32_t local_irq)
{
	return local_irq >> LOG2(PLIC_REG_SIZE);
}

INTC_PLIC_STATIC uint32_t local_irq_to_reg_offset(uint32_t local_irq)
{
	return local_irq_to_reg_index(local_irq) * sizeof(uint32_t);
}

static inline uint32_t get_plic_enabled_size(const struct device *dev)
{
	const struct plic_config *config = dev->config;

	return local_irq_to_reg_index(config->nr_irqs) + 1;
}

static ALWAYS_INLINE uint32_t get_hart_context(const struct device *dev, uint32_t hartid)
{
#ifdef CONFIG_PLIC_IRQ_AFFINITY
	const struct plic_config *config = dev->config;

	return config->hart_context[hartid];
#else
	ARG_UNUSED(dev);
	ARG_UNUSED(hartid);

	return 0;
#endif /* CONFIG_PLIC_IRQ_AFFINITY */
}

static ALWAYS_INLINE uint32_t get_irq_cpumask(const struct device *dev, uint32_t local_irq)
{
#ifdef CONFIG_PLIC_IRQ_AFFINITY
	const struct plic_data *data = dev->data;

	return data->irq_cpumask[local_irq];
#else
	ARG_UNUSED(dev);
	ARG_UNUSED(local_irq);

	return 0x1;
#endif /* CONFIG_PLIC_IRQ_AFFINITY */
}

static inline mem_addr_t get_context_en_addr(const struct device *dev, uint32_t cpu_num)
{
	const struct plic_config *config = dev->config;
	uint32_t hartid;

#if CONFIG_SMP
	hartid = _kernel.cpus[cpu_num].arch.hartid;
#else
	hartid = arch_proc_id();
#endif
	return config->irq_en + get_hart_context(dev, hartid) * CONTEXT_ENABLE_SIZE;
}

static inline mem_addr_t get_claim_complete_addr(const struct device *dev)
{
	const struct plic_config *config = dev->config;

	return config->reg + get_hart_context(dev, arch_proc_id()) * CONTEXT_SIZE + CONTEXT_CLAIM;
}


static inline mem_addr_t get_threshold_priority_addr(const struct device *dev, uint32_t cpu_num)
{
	const struct plic_config *config = dev->config;
	uint32_t hartid;

#if CONFIG_SMP
	hartid = _kernel.cpus[cpu_num].arch.hartid;
#else
	hartid = arch_proc_id();
#endif

	return config->reg + (get_hart_context(dev, hartid) * CONTEXT_SIZE);
}

static ALWAYS_INLINE uint32_t local_irq_to_irq(const struct device *dev, uint32_t local_irq)
{
	const struct plic_config *config = dev->config;

	return irq_to_level_2(local_irq) | config->irq;
}

/**
 * @brief Determine the PLIC device from the IRQ
 *
 * @param irq IRQ number
 *
 * @return PLIC device of that IRQ
 */
static inline const struct device *get_plic_dev_from_irq(uint32_t irq)
{
#ifdef CONFIG_DYNAMIC_INTERRUPTS
	return z_get_sw_isr_device_from_irq(irq);
#else
	return DEVICE_DT_INST_GET(0);
#endif
}

/**
 * @brief Return the value of the trigger type register for the IRQ
 *
 * In the event edge irq is enable this will return the trigger
 * value of the irq. In the event edge irq is not supported this
 * routine will return 0
 *
 * @param dev PLIC-instance device
 * @param local_irq PLIC-instance IRQ number to add to the trigger
 *
 * @return Trigger type register value if PLIC supports trigger type, PLIC_TRIG_LEVEL otherwise
 */
static uint32_t __maybe_unused riscv_plic_irq_trig_val(const struct device *dev, uint32_t local_irq)
{
	if (!IS_ENABLED(PLIC_SUPPORTS_TRIG_TYPE)) {
		return PLIC_TRIG_LEVEL;
	}

	const struct plic_config *config = dev->config;
	mem_addr_t trig_addr = config->trig + local_irq_to_reg_offset(local_irq);
	uint32_t offset = local_irq * PLIC_REG_TRIG_TYPE_WIDTH;

	return sys_read32(trig_addr) & GENMASK(offset + PLIC_REG_TRIG_TYPE_WIDTH - 1, offset);
}

static void plic_irq_enable_set_state(uint32_t irq, bool enable)
{
	const struct device *dev = get_plic_dev_from_irq(irq);
	const uint32_t local_irq = irq_from_level_2(irq);

	for (uint32_t cpu_num = 0; cpu_num < arch_num_cpus(); cpu_num++) {
		mem_addr_t en_addr =
			get_context_en_addr(dev, cpu_num) + local_irq_to_reg_offset(local_irq);
		uint32_t en_value;

		en_value = sys_read32(en_addr);
		WRITE_BIT(en_value, local_irq & PLIC_REG_MASK,
			  enable ? (get_irq_cpumask(dev, local_irq) & BIT(cpu_num)) != 0 : false);
		sys_write32(en_value, en_addr);
	}
}

/**
 * @brief Enable a riscv PLIC-specific interrupt line
 *
 * This routine enables a RISCV PLIC-specific interrupt line.
 * riscv_plic_irq_enable is called by RISCV_PRIVILEGED
 * arch_irq_enable function to enable external interrupts for
 * IRQS level == 2, whenever CONFIG_RISCV_HAS_PLIC variable is set.
 *
 * @param irq IRQ number to enable
 */
void riscv_plic_irq_enable(uint32_t irq)
{
	uint32_t key = irq_lock();

	plic_irq_enable_set_state(irq, true);

	irq_unlock(key);
}

/**
 * @brief Disable a riscv PLIC-specific interrupt line
 *
 * This routine disables a RISCV PLIC-specific interrupt line.
 * riscv_plic_irq_disable is called by RISCV_PRIVILEGED
 * arch_irq_disable function to disable external interrupts, for
 * IRQS level == 2, whenever CONFIG_RISCV_HAS_PLIC variable is set.
 *
 * @param irq IRQ number to disable
 */
void riscv_plic_irq_disable(uint32_t irq)
{
	uint32_t key = irq_lock();

	plic_irq_enable_set_state(irq, false);

	irq_unlock(key);
}

/**
 * @brief Check if a riscv PLIC-specific interrupt line is enabled
 *
 * This routine checks if a RISCV PLIC-specific interrupt line is enabled.
 * @param irq IRQ number to check
 *
 * @return 1 or 0
 */
int riscv_plic_irq_is_enabled(uint32_t irq)
{
	const struct device *dev = get_plic_dev_from_irq(irq);
	const uint32_t local_irq = irq_from_level_2(irq);
	uint32_t bit_position = local_irq & PLIC_REG_MASK;
	uint32_t en_value;
	int is_enabled = IS_ENABLED(CONFIG_PLIC_IRQ_AFFINITY) ? 0 : 1;
	uint32_t key = irq_lock();

	for (uint32_t cpu_num = 0; cpu_num < arch_num_cpus(); cpu_num++) {
		mem_addr_t en_addr =
			get_context_en_addr(dev, cpu_num) + local_irq_to_reg_offset(local_irq);

		en_value = sys_read32(en_addr);
		if (IS_ENABLED(CONFIG_PLIC_IRQ_AFFINITY)) {
			is_enabled |= !!(en_value & BIT(bit_position));
		} else {
			is_enabled &= !!(en_value & BIT(bit_position));
		}
	}

	irq_unlock(key);

	return is_enabled;
}

/**
 * @brief Set priority of a riscv PLIC-specific interrupt line
 *
 * This routine set the priority of a RISCV PLIC-specific interrupt line.
 * riscv_plic_irq_set_prio is called by riscv arch_irq_priority_set to set
 * the priority of an interrupt whenever CONFIG_RISCV_HAS_PLIC variable is set.
 *
 * @param irq IRQ number for which to set priority
 * @param priority Priority of IRQ to set to
 */
void riscv_plic_set_priority(uint32_t irq, uint32_t priority)
{
	const struct device *dev = get_plic_dev_from_irq(irq);
	const struct plic_config *config = dev->config;
	const uint32_t local_irq = irq_from_level_2(irq);
	mem_addr_t prio_addr = config->prio + (local_irq * sizeof(uint32_t));

	if (priority > config->max_prio) {
		priority = config->max_prio;
	}

	sys_write32(priority, prio_addr);
}

/**
 * @brief Get riscv PLIC-specific interrupt line causing an interrupt
 *
 * This routine returns the RISCV PLIC-specific interrupt line causing an
 * interrupt.
 *
 * @param dev Optional device pointer to get the interrupt line's controller
 *
 * @return PLIC-specific interrupt line causing an interrupt.
 */
unsigned int riscv_plic_get_irq(void)
{
	return save_irq[arch_proc_id()];
}

/**
 * @brief Get riscv PLIC causing an interrupt
 *
 * This routine returns the RISCV PLIC device causing an interrupt.
 *
 * @return PLIC device causing an interrupt.
 */
const struct device *riscv_plic_get_dev(void)
{
	return save_dev[arch_proc_id()];
}

/**
 *
 * @brief Set riscv PLIC-specific interrupt enable by cpu bitmask
 *
 * @param irq IRQ number for which to set smp irq affinity
 * @param mask Bitmask to specific which cores can handle IRQ
 */
void riscv_plic_irq_set_affinity(uint32_t irq, uint32_t mask)
{
#ifdef CONFIG_PLIC_IRQ_AFFINITY
	const struct device *dev = get_plic_dev_from_irq(irq);
	const struct plic_data *data = dev->data;
	__maybe_unused const struct plic_config *config = dev->config;
	const uint32_t local_irq = irq_from_level_2(irq);

	__ASSERT(local_irq < config->nr_irqs, "overflow: irq %d, local_irq %d", irq, local_irq);
	__ASSERT((mask & ~BIT_MASK(arch_num_cpus())) == 0, "cpumask: 0x%X", mask);

	uint32_t key = irq_lock();

	/* Updated irq_cpumask for next time setting plic enable register */
	data->irq_cpumask[local_irq] = (plic_cpumask_t)mask;

	/* If irq is enabled, apply the new irq affinity */
	if (riscv_plic_irq_is_enabled(irq)) {
		riscv_plic_irq_enable(irq);
	}

	irq_unlock(key);
#else
	ARG_UNUSED(irq);
	ARG_UNUSED(mask);
#endif /* CONFIG_PLIC_IRQ_AFFINITY */
}

static void plic_irq_handler(const struct device *dev)
{
	const struct plic_config *config = dev->config;
	mem_addr_t claim_complete_addr = get_claim_complete_addr(dev);
	struct _isr_table_entry *ite;
	uint32_t __maybe_unused trig_val;
	uint32_t cpu_id = arch_proc_id();
	/* Get the IRQ number generating the interrupt */
	const uint32_t local_irq = sys_read32(claim_complete_addr);

#ifdef CONFIG_PLIC_SHELL_IRQ_COUNT
	const struct plic_data *data = dev->data;
	struct plic_stats stat = data->stats;
	uint32_t irq_index = cpu_id * config->nr_irqs + local_irq;
#if CONFIG_MP_NUM_CPUS > 1
	uint32_t irq_total = arch_num_cpus() * config->nr_irqs + local_irq;
#else
	uint32_t irq_total = irq_index;
#endif /* CONFIG_MP_NUM_CPUS > 1 */

	/* Cap the count at __UINT16_MAX__ */
	if (stat.irq_count[irq_total] < __UINT16_MAX__) {
		stat.irq_count[irq_index]++;
		if (CONFIG_MP_NUM_CPUS > 1) {
			stat.irq_count[irq_total]++;
		}
	}
#endif /* CONFIG_PLIC_SHELL_IRQ_COUNT */

	/*
	 * Note: Because PLIC only supports multicast of interrupt, all enabled
	 * targets will receive interrupt notification. Only the fastest target
	 * will claim this interrupt, and other targets will claim ID 0 if
	 * no other pending interrupt now.
	 *
	 * (by RISC-V Privileged Architecture v1.10)
	 */
	if (IS_ENABLED(CONFIG_SMP) && (local_irq == 0U)) {
		return;
	}

	/*
	 * Save IRQ in save_irq. To be used, if need be, by
	 * subsequent handlers registered in the _sw_isr_table table,
	 * as IRQ number held by the claim_complete register is
	 * cleared upon read.
	 */
	save_irq[cpu_id] = local_irq;
	save_dev[cpu_id] = dev;

	/*
	 * If the IRQ is out of range, call z_irq_spurious.
	 * A call to z_irq_spurious will not return.
	 */
	if ((local_irq == 0U) || (local_irq >= config->nr_irqs)) {
		z_irq_spurious(NULL);
	}

#if PLIC_DRV_HAS_COMPAT(andestech_nceplic100)
	trig_val = riscv_plic_irq_trig_val(dev, local_irq);
	/*
	 * Edge-triggered interrupts on Andes NCEPLIC100 have to be acknowledged first before
	 * getting handled so that we don't miss on the next edge-triggered interrupt.
	 */
	if (trig_val == PLIC_TRIG_EDGE) {
		sys_write32(local_irq, claim_complete_addr);
	}
#endif

	/* Call the corresponding IRQ handler in _sw_isr_table */
	ite = &config->isr_table[local_irq];
	ite->isr(ite->arg);

	/*
	 * Write to claim_complete register to indicate to
	 * PLIC controller that the IRQ has been handled
	 * for level triggered interrupts.
	 */
#if PLIC_DRV_HAS_COMPAT(andestech_nceplic100)
	/* For NCEPLIC100, handle only if level-triggered */
	if (trig_val == PLIC_TRIG_LEVEL) {
		sys_write32(local_irq, claim_complete_addr);
	}
#else
	sys_write32(local_irq, claim_complete_addr);
#endif
}

/**
 * @brief Initialize the Platform Level Interrupt Controller
 *
 * @param dev PLIC device struct
 *
 * @retval 0 on success.
 */
static int plic_init(const struct device *dev)
{
	const struct plic_config *config = dev->config;
	mem_addr_t en_addr, thres_prio_addr;
	mem_addr_t prio_addr = config->prio;

	/* Iterate through each of the contexts, HART + PRIV */
	for (uint32_t cpu_num = 0; cpu_num < arch_num_cpus(); cpu_num++) {
		en_addr = get_context_en_addr(dev, cpu_num);
		thres_prio_addr = get_threshold_priority_addr(dev, cpu_num);

		/* Ensure that all interrupts are disabled initially */
		for (uint32_t i = 0; i < get_plic_enabled_size(dev); i++) {
			sys_write32(0U, en_addr + (i * sizeof(uint32_t)));
		}

		/* Set threshold priority to 0 */
		sys_write32(0U, thres_prio_addr);
	}

	/* Set priority of each interrupt line to 0 initially */
	for (uint32_t i = 0; i < config->nr_irqs; i++) {
		sys_write32(0U, prio_addr + (i * sizeof(uint32_t)));
	}

	/* Configure IRQ for PLIC driver */
	config->irq_config_func();

	return 0;
}

#ifdef CONFIG_PLIC_SHELL
static inline int parse_device(const struct shell *sh, size_t argc, char *argv[],
			       const struct device **plic)
{
	ARG_UNUSED(argc);

	*plic = device_get_binding(argv[1]);
	if (*plic == NULL) {
		shell_error(sh, "PLIC device (%s) not found!\n", argv[1]);
		return -ENODEV;
	}

	return 0;
}

#ifdef CONFIG_PLIC_SHELL_IRQ_COUNT
static int cmd_stats_get(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev;
	int ret = parse_device(sh, argc, argv, &dev);
	uint16_t min_hit = 0;

	if (ret != 0) {
		return ret;
	}

	const struct plic_config *config = dev->config;
	const struct plic_data *data = dev->data;
	struct plic_stats stat = data->stats;

	if (argc > 2) {
		min_hit = (uint16_t)shell_strtoul(argv[2], 10, &ret);
		if (ret != 0) {
			shell_error(sh, "Failed to parse %s: %d", argv[2], ret);
			return ret;
		}
		shell_print(sh, "IRQ line with > %d hits:", min_hit);
	}

	shell_fprintf(sh, SHELL_NORMAL, "   IRQ");
	for (int cpu_id = 0; cpu_id < arch_num_cpus(); cpu_id++) {
		shell_fprintf(sh, SHELL_NORMAL, "  CPU%2d", cpu_id);
	}
	if (CONFIG_MP_NUM_CPUS > 1) {
		shell_fprintf(sh, SHELL_NORMAL, "  Total");
	}
	shell_fprintf(sh, SHELL_NORMAL, "\tISR(ARG)\n");

	for (int i = 0; i < config->nr_irqs; i++) {
		int offset = (CONFIG_MP_NUM_CPUS > 1) ? arch_num_cpus() * config->nr_irqs : 0;
		uint16_t total_hits = stat.irq_count[offset + i];

		if (total_hits <= min_hit) {
			continue;
		}

		shell_fprintf(sh, SHELL_NORMAL, "  %4d", i); /* IRQ number */
		for (int cpu_id = 0; cpu_id < arch_num_cpus(); cpu_id++) {
			uint32_t irq_index = cpu_id * config->nr_irqs + i;

			shell_fprintf(sh, SHELL_NORMAL, "  %5d", stat.irq_count[irq_index]);
		}
		if (CONFIG_MP_NUM_CPUS > 1) {
			shell_fprintf(sh, SHELL_NORMAL, "  %5d",
				      stat.irq_count[arch_num_cpus() * config->nr_irqs + i]);
		}
#ifdef CONFIG_SYMTAB
		const char *name =
			symtab_find_symbol_name((uintptr_t)config->isr_table[i].isr, NULL);

		shell_fprintf(sh, SHELL_NORMAL, "\t%s(%p)\n", name, config->isr_table[i].arg);
#else
		shell_fprintf(sh, SHELL_NORMAL, "\t%p(%p)\n", (void *)config->isr_table[i].isr,
			      config->isr_table[i].arg);
#endif /* CONFIG_SYMTAB */
	}
	shell_print(sh, "");

	return 0;
}

static int cmd_stats_clear(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev;
	int ret = parse_device(sh, argc, argv, &dev);

	if (ret != 0) {
		return ret;
	}

	const struct plic_data *data = dev->data;
	const struct plic_config *config = dev->config;
	struct plic_stats stat = data->stats;

	memset(stat.irq_count, 0,
	       config->nr_irqs *
		       COND_CODE_1(CONFIG_MP_MAX_NUM_CPUS, (1),
				   (UTIL_INC(CONFIG_MP_MAX_NUM_CPUS))) *
		       sizeof(uint16_t));

	shell_print(sh, "Cleared stats of %s.\n", dev->name);

	return 0;
}
#endif /* CONFIG_PLIC_SHELL_IRQ_COUNT */

#ifdef CONFIG_PLIC_SHELL_IRQ_AFFINITY
static int cmd_affinity_set(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	uint32_t local_irq, irq, mask;
	const struct device *dev;
	int rc = parse_device(sh, argc, argv, &dev);
	const struct plic_config *config = dev->config;

	if (rc != 0) {
		return rc;
	}

	local_irq = (uint32_t)shell_strtol(argv[2], 10, &rc);
	if (rc != 0) {
		shell_error(sh, "Failed to parse %s: %d", argv[2], rc);
	}

	if (local_irq >= config->nr_irqs) {
		shell_error(sh, "local_irq (%d) > nr_irqs (%d)", local_irq, config->nr_irqs);
		return -EINVAL;
	}

	mask = (uint32_t)shell_strtol(argv[3], 16, &rc);
	if (rc != 0) {
		shell_error(sh, "Failed to parse %s: %d", argv[3], rc);
	}

	if ((mask & ~BIT_MASK(arch_num_cpus())) != 0) {
		shell_error(sh, "cpumask: 0x%X num_cpus: %d", mask, arch_num_cpus());
		return -EINVAL;
	}

	if (local_irq != 0) {
		irq = local_irq_to_irq(dev, local_irq);
		riscv_plic_irq_set_affinity(irq, mask);
		shell_print(sh, "IRQ %d affinity set to 0x%X", local_irq, mask);
	} else {
		for (local_irq = 1; local_irq <= config->nr_irqs; local_irq++) {
			irq = local_irq_to_irq(dev, local_irq);
			shell_info(sh, "%d %d", irq, local_irq);
			riscv_plic_irq_set_affinity(irq, mask);
		}
		shell_print(sh, "All IRQ affinity set to 0x%X", mask);
	}

	return 0;
}

static int cmd_affinity_get(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	const struct device *dev;
	int rc = parse_device(sh, argc, argv, &dev);
	const struct plic_config *config = dev->config;

	if (rc != 0) {
		return rc;
	}

	shell_print(sh, " IRQ  MASK");
	if (argc == 2) {
		for (uint32_t local_irq = 0; local_irq < config->nr_irqs; local_irq++) {
			shell_print(sh, "%4d  0x%X", local_irq, get_irq_cpumask(dev, local_irq));
		}
	} else {
		uint32_t local_irq = (uint32_t)shell_strtol(argv[2], 10, &rc);

		if (rc != 0) {
			shell_error(sh, "Failed to parse %s: %d", argv[2], rc);
		}

		if (local_irq >= config->nr_irqs) {
			shell_error(sh, "local_irq (%d) > nr_irqs (%d)", local_irq,
				    config->nr_irqs);
			return -EINVAL;
		}

		shell_print(sh, "%4d  0x%X", local_irq, get_irq_cpumask(dev, local_irq));
	}

	return 0;
}
#endif /* CONFIG_PLIC_SHELL_IRQ_AFFINITY */

/* Device name autocompletion support */
static void device_name_get(size_t idx, struct shell_static_entry *entry)
{
	const struct device *dev = shell_device_lookup(idx, "interrupt-controller");

	entry->syntax = (dev != NULL) ? dev->name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;
}

SHELL_DYNAMIC_CMD_CREATE(dsub_device_name, device_name_get);

#ifdef CONFIG_PLIC_SHELL_IRQ_COUNT
SHELL_STATIC_SUBCMD_SET_CREATE(plic_stats_cmds,
	SHELL_CMD_ARG(get, &dsub_device_name,
		"Read PLIC's stats.\n"
		"Usage: plic stats get <device> [minimum hits]",
		cmd_stats_get, 2, 1),
	SHELL_CMD_ARG(clear, &dsub_device_name,
		"Reset PLIC's stats.\n"
		"Usage: plic stats clear <device>",
		cmd_stats_clear, 2, 0),
	SHELL_SUBCMD_SET_END
);
#endif /* CONFIG_PLIC_SHELL_IRQ_COUNT */

#ifdef CONFIG_PLIC_SHELL_IRQ_AFFINITY
SHELL_STATIC_SUBCMD_SET_CREATE(plic_affinity_cmds,
	SHELL_CMD_ARG(set, &dsub_device_name,
		      "Set IRQ affinity.\n"
		      "Usage: plic affinity set <device> <local_irq> <cpumask>",
		      cmd_affinity_set, 4, 0),
	SHELL_CMD_ARG(get, &dsub_device_name,
		      "Get IRQ affinity.\n"
		      "Usage: plic affinity get <device> <local_irq>",
		      cmd_affinity_get, 2, 1),
	SHELL_SUBCMD_SET_END);
#endif /* CONFIG_PLIC_SHELL_IRQ_AFFINITY */

SHELL_STATIC_SUBCMD_SET_CREATE(plic_cmds,
#ifdef CONFIG_PLIC_SHELL_IRQ_COUNT
	SHELL_CMD(stats, &plic_stats_cmds, "IRQ stats", NULL),
#endif /* CONFIG_PLIC_SHELL_IRQ_COUNT */
#ifdef CONFIG_PLIC_SHELL_IRQ_AFFINITY
	SHELL_CMD(affinity, &plic_affinity_cmds, "IRQ affinity", NULL),
#endif /* CONFIG_PLIC_SHELL_IRQ_AFFINITY */
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(plic, &plic_cmds, "PLIC shell commands", NULL);
#endif /* CONFIG_PLIC_SHELL */

#define PLIC_MIN_IRQ_NUM(n) MIN(DT_INST_PROP(n, riscv_ndev), CONFIG_MAX_IRQ_PER_AGGREGATOR)

#ifdef CONFIG_PLIC_SHELL
#define PLIC_INTC_IRQ_COUNT_BUF_DEFINE(n)                                                          \
	static uint16_t local_irq_count_##n[COND_CODE_1(CONFIG_MP_MAX_NUM_CPUS, (1),               \
							(UTIL_INC(CONFIG_MP_MAX_NUM_CPUS)))]       \
					   [PLIC_MIN_IRQ_NUM(n)];
#define PLIC_INTC_IRQ_COUNT_INIT(n)                                                                \
	.stats = {                                                                                 \
		.irq_count = &local_irq_count_##n[0][0],                                           \
	},

#else
#define PLIC_INTC_IRQ_COUNT_BUF_DEFINE(n)
#define PLIC_INTC_IRQ_COUNT_INIT(n)
#endif /* CONFIG_PLIC_SHELL */

#ifdef CONFIG_PLIC_IRQ_AFFINITY
#define PLIC_IRQ_CPUMASK_BUF_DECLARE(n)                                                            \
	static plic_cpumask_t irq_cpumask_##n[PLIC_MIN_IRQ_NUM(n)] = {                             \
		[0 ...(PLIC_MIN_IRQ_NUM(n) - 1)] = CONFIG_PLIC_IRQ_AFFINITY_MASK,                  \
	}
#define PLIC_IRQ_CPUMASK_BUF_INIT(n) .irq_cpumask = &irq_cpumask_##n[0],
#else
#define PLIC_IRQ_CPUMASK_BUF_DECLARE(n)
#define PLIC_IRQ_CPUMASK_BUF_INIT(n)
#endif /* CONFIG_PLIC_IRQ_AFFINITY */

#define PLIC_INTC_DATA_INIT(n)                                                                     \
	PLIC_INTC_IRQ_COUNT_BUF_DEFINE(n);                                                         \
	PLIC_IRQ_CPUMASK_BUF_DECLARE(n);                                                           \
	static struct plic_data plic_data_##n = {                                                  \
		PLIC_INTC_IRQ_COUNT_INIT(n)                                                        \
		PLIC_IRQ_CPUMASK_BUF_INIT(n)                                                       \
	};

#define PLIC_INTC_IRQ_FUNC_DECLARE(n) static void plic_irq_config_func_##n(void)

#define PLIC_INTC_IRQ_FUNC_DEFINE(n)                                                               \
	static void plic_irq_config_func_##n(void)                                                 \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), 0, plic_irq_handler, DEVICE_DT_INST_GET(n), 0);       \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}

#define HART_CONTEXTS(i, n) IF_ENABLED(IS_EQ(DT_INST_IRQN_BY_IDX(n, i), DT_INST_IRQN(n)), (i,))
#define PLIC_HART_CONTEXT_DECLARE(n)                                                               \
	static const int hart_contexts_##n[DT_CHILD_NUM(DT_PATH(cpus))] = {                        \
		LISTIFY(DT_INST_NUM_IRQS(n), HART_CONTEXTS, (), n)}

#define PLIC_INTC_CONFIG_INIT(n)                                                                   \
	PLIC_INTC_IRQ_FUNC_DECLARE(n);                                                             \
	PLIC_HART_CONTEXT_DECLARE(n);                                                              \
	static const struct plic_config plic_config_##n = {                                        \
		.prio = PLIC_BASE_ADDR(n),                                                         \
		.irq_en = PLIC_BASE_ADDR(n) + CONTEXT_ENABLE_BASE,                                 \
		.reg = PLIC_BASE_ADDR(n) + CONTEXT_BASE,                                           \
		IF_ENABLED(PLIC_SUPPORTS_TRIG_TYPE,                                                \
			   (.trig = PLIC_BASE_ADDR(n) + PLIC_REG_TRIG_TYPE_OFFSET,))               \
		.max_prio = DT_INST_PROP(n, riscv_max_priority),                                   \
		.riscv_ndev = DT_INST_PROP(n, riscv_ndev),                                         \
		.nr_irqs = PLIC_MIN_IRQ_NUM(n),                                                    \
		.irq = DT_INST_IRQN(n),                                                            \
		.irq_config_func = plic_irq_config_func_##n,                                       \
		.isr_table = &_sw_isr_table[INTC_INST_ISR_TBL_OFFSET(n)],                          \
		.hart_context = hart_contexts_##n,                                                 \
	};                                                                                         \
	PLIC_INTC_IRQ_FUNC_DEFINE(n)

#define PLIC_INTC_DEVICE_INIT(n)                                                                   \
	IRQ_PARENT_ENTRY_DEFINE(                                                                   \
		plic##n, DEVICE_DT_INST_GET(n), DT_INST_IRQN(n),                                   \
		INTC_INST_ISR_TBL_OFFSET(n),                                                       \
		DT_INST_INTC_GET_AGGREGATOR_LEVEL(n));                                             \
	PLIC_INTC_CONFIG_INIT(n)                                                                   \
	PLIC_INTC_DATA_INIT(n)                                                                     \
	DEVICE_DT_INST_DEFINE(n, &plic_init, NULL,                                                 \
			      &plic_data_##n, &plic_config_##n,                                    \
			      PRE_KERNEL_1, CONFIG_INTC_INIT_PRIORITY,                             \
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(PLIC_INTC_DEVICE_INIT)
