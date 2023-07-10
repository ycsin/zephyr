/*
 * Copyright (c) 2023 Meta
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_LIB_POSIX_SIGNAL_INTERNAL_H_
#define ZEPHYR_LIB_POSIX_SIGNAL_INTERNAL_H_

#ifdef CONFIG_POSIX_SIGNAL
#include <zephyr/posix/signal.h>
#include <zephyr/sys/slist.h>

struct sig_pending {
	sys_snode_t node;
	siginfo_t info;
};
#endif /* CONFIG_POSIX_SIGNAL */

#endif /* ZEPHYR_LIB_POSIX_SIGNAL_INTERNAL_H_ */
