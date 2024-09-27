/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/devicetree.h>

#define DT_DRV_COMPAT zephyr_ipi_plic

#define IPI_PLIC DT_NODELABEL(ipi_plic)
DT_INST_NUM_IRQS
#define IPI_PLIC_CONNECT(n) IRQ_CONNECT(DT_INST_IRQN_BY_IDX(0, n), 1, plic_irq_handler, UINT_TO_POINTER(n), 0)

LISTIFY(DT_NUM_IRQS(IPI_PLIC), IPI_PLIC_CONNECT, (;));

DT_INST_FOREACH_PROP_ELEM(0, interrupts_extended, a)

int main(void)
{
	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

	return 0;
}
