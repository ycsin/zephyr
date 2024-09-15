/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <zephyr/irq.h>

static void irq_handler(const struct device *dev)
{
	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);
}

int main(void)
{
	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

	IRQ_CONNECT((IRQ_TO_L2(11) | 11), 0, irq_handler, NULL, 0);
	irq_enable((IRQ_TO_L2(11) | 11));

	return 0;
}
