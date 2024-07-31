/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/irq_offload.h>

#define IRQ_OFFLOAD 0
#define IRQ_PLIC 1

#define PLIC_LOCAL_IRQ (DT_IRQ_BY_IDX(DT_NODELABEL(uart0), 0, irq) + 2)
#define ZEPHYR_IRQN (IRQ_TO_L2(PLIC_LOCAL_IRQ) | DT_IRQN_BY_IDX(DT_NODELABEL(plic), 1))

void riscv_plic_sw_irq_set_pending(const struct device *dev, uint32_t irq);

void plic_sw_irq_set_pending(uint32_t local_irq)
{
	uint32_t pend;
	uint32_t irq_reg = local_irq >> 5;
	uintptr_t reg = ((uintptr_t)DT_REG_ADDR(DT_NODELABEL(plic)) + 0x1000 + (irq_reg << 2));

	pend = sys_read32(reg);
	pend |= BIT(local_irq % 32);
	sys_write32(pend, reg);

}

static void test_isr(const void *arg)
{
	uint32_t offload = POINTER_TO_UINT(arg);

	printf("Hello World! %s\n", (offload == IRQ_OFFLOAD) ? "irq_offload" : "plic_sw");
}

int main(void)
{
	printk("\n> SMP %s - # of CPU: %d\n", (IS_ENABLED(CONFIG_SMP) ? "enabled" : "disabled"),
	       arch_num_cpus());
	printf("> PLIC local IRQ: 0x%X\n", PLIC_LOCAL_IRQ);
	printf("> L1 IRQ: 0x%X\n", DT_IRQN_BY_IDX(DT_NODELABEL(plic), 1));
	printf("> Zephyr-encoded IRQN: 0x%X\n\n\n", ZEPHYR_IRQN);

	irq_offload(test_isr, UINT_TO_POINTER(IRQ_OFFLOAD));

	IRQ_CONNECT(ZEPHYR_IRQN, 0, test_isr, UINT_TO_POINTER(IRQ_PLIC), 0U);
	irq_enable(ZEPHYR_IRQN);
	// riscv_plic_sw_irq_set_pending(DEVICE_DT_GET(DT_NODELABEL(plic)), ZEPHYR_IRQN);
	plic_sw_irq_set_pending(PLIC_LOCAL_IRQ);

	return 0;
}
