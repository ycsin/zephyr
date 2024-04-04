/*
 * Copyright (c) 2019 Synopsys, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * west build -b qemu_riscv64/qemu_virt_riscv64/smp -p auto -t run zephyr/samples/arch/smp/log \
 * -DCONFIG_MP_MAX_NUM_CPUS=4 -DCONFIG_SMP=y -DCONFIG_PIN_THREADS=y
 */

/**
 *   no smp - 1520ms
 *   smp 1     -  1820  2050  1790 2070 1810 1950 1810
 *   smp 2     -  1750  1890  1760 1740 1740 1730 1960
 *   smp 3     -  3120  2290  hang 3040 2190 hang 2380
 *   smp 4     -  hang  2750 12390 4580 4470 hang hang
 *   smp 4 pin -  3010  3160  3030 5930 3320 2890 4030
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell_backend.h>
#include <stdio.h>

LOG_MODULE_REGISTER(main);

extern struct k_thread logging_thread;
extern struct k_work_q k_sys_work_q;

int main(void)
{
	if (IS_ENABLED(CONFIG_PIN_THREADS)) {
		const struct shell *shell_uart = shell_backend_get_by_name("shell_uart");

		LOG_WRN("Pin threads");
		k_thread_cpu_pin(k_current_get(), 0);
		k_thread_cpu_pin(shell_uart->thread, 1);
		k_thread_cpu_pin(&k_sys_work_q.thread, 2);
		k_thread_cpu_pin(&logging_thread, 3);
	}

	k_msleep(1000);

	int64_t uptime = k_uptime_get();

	for (int i = 0; i < 1000; i++) {
		LOG_WRN("%d: Hello World! %s                             ", i, CONFIG_BOARD_TARGET);
	}

	int64_t delta = k_uptime_delta(&uptime);

	LOG_WRN("\nTime taken: %lld ms\n", delta);

	return 0;
}
